// Pipeline key audit: which GX PipelineConfig fields can differ without changing the pipeline.
//
// Every distinct PipelineConfig is its own pipeline-cache key, so a field that reaches the key but
// not the pipeline (the shader source and the render pipeline descriptor) makes the same pipeline
// compile again under a new key, and a draw is skipped while it does (issue #78's dstAlpha). For each
// GX row of a pipeline cache this replaces one field at a time with the other values that field
// takes in the cache's rows and checks whether the pipeline changes. A field is dead in a row when
// none of its values change it. It also groups the rows whose pipelines are identical, the keys
// already wasted in that cache.
//
//   pipeline_key_audit <cache.db> [--rows N] [--detail field]
//   pipeline_key_audit <cache.db> --check-unused     pipeline_unused_inputs keeps every row's pipeline
//   pipeline_key_audit <cache.db> --rewrite <db>     rekey <db>'s rows as the cache load does (the seed)
//   pipeline_key_audit <cache.db> --nearest <db>     the fields that set cache.db's rows <db> lacks
//                                                    apart from <db>'s closest row (both rekeyed)
//
// Built from build-port-link: ninja -C build-port-link pipeline_key_audit. gx.cpp is compiled into
// this file for its descriptor helpers (anonymous namespace), so libaurora_gx's copy isn't linked.
#include "gx/gx.cpp"

#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace aurora::gx;

struct Field {
  size_t off;
  size_t size;
  u8 mask; // bitfield bits within the one byte at off, else 0
  std::string name;
};

std::vector<Field> make_fields() {
  std::vector<Field> f;
  const auto add = [&](size_t off, size_t size, std::string name) { f.push_back({off, size, 0, std::move(name)}); };
  const size_t sc = offsetof(PipelineConfig, shaderConfig);
#define PF(m) add(offsetof(PipelineConfig, m), sizeof(PipelineConfig::m), #m)
#define SF(m) add(sc + offsetof(ShaderConfig, m), sizeof(ShaderConfig::m), "sc." #m)
  PF(msaaSamples);
  SF(fogType);
  SF(vtxStride);
  const size_t bits = sc + offsetof(ShaderConfig, vtxStride) + 1;
  const std::pair<u8, const char*> bitNames[] = {{0x03, "sc.lineMode"},  {0x04, "sc.fogRangeEnabled"},
                                                 {0x08, "sc.drawId"},    {0x10, "sc.shadow"},
                                                 {0x20, "sc.pbrBindPos"}, {0x40, "sc.pbrBindLe"},
                                                 {0x80, "sc.sceneHdr"}};
  for (const auto& [mask, name] : bitNames) {
    f.push_back({bits, 1, mask, name});
  }
  SF(pbr);
  SF(sdf);
  SF(depthOnly);
  SF(pbrKind);
  SF(volFog);
  SF(pbrLightmapAttr);
  SF(hudSample);
  SF(perPixelLight);
  SF(actorDecal);
  for (u32 i = 0; i < MaxVtxAttr; ++i) {
    const size_t b = sc + offsetof(ShaderConfig, attrs) + i * sizeof(AttrConfig);
    const auto n = fmt::format("sc.attrs[{}].", i);
#define AF(m) add(b + offsetof(AttrConfig, m), sizeof(AttrConfig::m), n + #m)
    AF(attrType); AF(cnt); AF(compType); AF(offset); AF(stride); AF(frac); AF(le); AF(nbt3);
  }
  for (u32 i = 0; i < MaxTevSwap; ++i) {
    add(sc + offsetof(ShaderConfig, tevSwapTable) + i * sizeof(TevSwap), sizeof(TevSwap), fmt::format("sc.tevSwapTable[{}]", i));
  }
  for (u32 i = 0; i < MaxTevStages; ++i) {
    const size_t b = sc + offsetof(ShaderConfig, tevStages) + i * sizeof(TevStage);
    const auto n = fmt::format("sc.tevStages[{}].", i);
    for (u32 j = 0; j < 4; ++j) {
      add(b + offsetof(TevStage, colorPass) + j * sizeof(GXTevColorArg), sizeof(GXTevColorArg), n + "colorPass." + "abcd"[j]);
      add(b + offsetof(TevStage, alphaPass) + j * sizeof(GXTevAlphaArg), sizeof(GXTevAlphaArg), n + "alphaPass." + "abcd"[j]);
    }
    for (const auto [opOff, opName] : {std::pair{offsetof(TevStage, colorOp), "colorOp."}, {offsetof(TevStage, alphaOp), "alphaOp."}}) {
#define OF(m) add(b + opOff + offsetof(TevOp, m), sizeof(TevOp::m), n + opName + #m)
      OF(op); OF(bias); OF(scale); OF(outReg); OF(clamp);
    }
#define TF(m) add(b + offsetof(TevStage, m), sizeof(TevStage::m), n + #m)
    TF(kcSel); TF(kaSel); TF(texCoordId); TF(texMapId); TF(channelId); TF(tevSwapRas); TF(tevSwapTex);
    TF(indTexStage); TF(indTexFormat); TF(indTexBiasSel); TF(indTexAlphaSel); TF(indTexMtxId); TF(indTexWrapS);
    TF(indTexWrapT); TF(indTexUseOrigLOD); TF(indTexAddPrev);
  }
  SF(tevStageCount);
  for (u32 i = 0; i < MaxColorChannels; ++i) {
    const size_t b = sc + offsetof(ShaderConfig, colorChannels) + i * sizeof(ColorChannelConfig);
    const auto n = fmt::format("sc.colorChannels[{}].", i);
#define CF(m) add(b + offsetof(ColorChannelConfig, m), sizeof(ColorChannelConfig::m), n + #m)
    CF(matSrc); CF(ambSrc); CF(diffFn); CF(attnFn); CF(lightingEnabled);
  }
  for (u32 i = 0; i < MaxTexCoord; ++i) {
    const size_t b = sc + offsetof(ShaderConfig, tcgs) + i * sizeof(TcgConfig);
    const auto n = fmt::format("sc.tcgs[{}].", i);
#define GF(m) add(b + offsetof(TcgConfig, m), sizeof(TcgConfig::m), n + #m)
    GF(type); GF(src); GF(mtx); GF(postMtx); GF(normalize); GF(embossSrc);
  }
  {
    const size_t b = sc + offsetof(ShaderConfig, alphaCompare);
#define KF(m) add(b + offsetof(AlphaCompare, m), sizeof(AlphaCompare::m), "sc.alphaCompare." #m)
    KF(comp0); KF(ref0); KF(op); KF(comp1); KF(ref1);
  }
  for (u32 i = 0; i < MaxIndStages; ++i) {
    const size_t b = sc + offsetof(ShaderConfig, indStages) + i * sizeof(IndStage);
    const auto n = fmt::format("sc.indStages[{}].", i);
#define IF(m) add(b + offsetof(IndStage, m), sizeof(IndStage::m), n + #m)
    IF(texCoordId); IF(texMapId); IF(scaleS); IF(scaleT);
  }
  SF(numIndStages);
  PF(depthFunc); PF(cullMode); PF(blendMode); PF(blendFacSrc); PF(blendFacDst); PF(blendOp); PF(dstAlpha);
  PF(polygonOffsetBits); PF(polygonOffsetScaleBits); PF(polygonOffsetClampBits); PF(shadowPass);
  PF(depthCompare); PF(depthUpdate); PF(alphaUpdate); PF(colorUpdate);
  return f;
}

// What the GPU gets from a config: the shader module's source and the descriptor build_pipeline makes.
u64 pipeline_signature(const PipelineConfig& config) {
  auto mode = dst_alpha_mode(config);
  auto source = build_shader_source(config.shaderConfig);
  if (!apply_dst_alpha_mode(source, mode)) {
    mode = DstAlphaMode::None;
  }
  std::string desc;
  const auto prim = to_primitive_state(config.cullMode);
  desc += fmt::format("prim {} {} {};", underlying(prim.topology), underlying(prim.frontFace), underlying(prim.cullMode));
  if (config.shadowPass != 0) {
    desc += fmt::format("shadow {};", config.shadowPass);
  } else {
    const bool depthWrite = config.depthCompare && config.depthUpdate;
    const auto cmp = config.depthCompare ? to_compare_function(config.depthFunc) : wgpu::CompareFunction::Always;
    const float bias = std::bit_cast<float>(config.polygonOffsetBits);
    desc += fmt::format("depth {} {} {} {} {};", depthWrite, underlying(cmp), round_away_from_zero<int32_t>(bias),
                        std::bit_cast<float>(config.polygonOffsetScaleBits),
                        std::bit_cast<float>(config.polygonOffsetClampBits));
    const auto blend = to_blend_state(config.blendMode, config.blendFacSrc, config.blendFacDst, config.blendOp,
                                      config.dstAlpha, mode == DstAlphaMode::DualSource);
    desc += fmt::format("blend {} {} {} {} {} {};", underlying(blend.color.operation), underlying(blend.color.srcFactor),
                        underlying(blend.color.dstFactor), underlying(blend.alpha.operation),
                        underlying(blend.alpha.srcFactor), underlying(blend.alpha.dstFactor));
    desc += fmt::format("mask {} hdr {} layout {} msaa {};",
                        static_cast<u32>(to_write_mask(config.colorUpdate, config.alphaUpdate)),
                        static_cast<int>(config.shaderConfig.sceneHdr), shadow_receives(config.shaderConfig),
                        config.msaaSamples);
  }
  return XXH3_64bits_withSeed(source.data(), source.size(), XXH3_64bits(desc.data(), desc.size()));
}

std::vector<u8> field_value(const u8* row, const Field& f) {
  std::vector<u8> v(row + f.off, row + f.off + f.size);
  if (f.mask) {
    v[0] &= f.mask;
  }
  return v;
}

void set_field(u8* row, const Field& f, const std::vector<u8>& v) {
  if (f.mask) {
    row[f.off] = (row[f.off] & ~f.mask) | (v[0] & f.mask);
  } else {
    std::memcpy(row + f.off, v.data(), f.size);
  }
}

// --rewrite: gives a cache's GX rows the config and key the cache load makes of them (per-pixel
// lighting off, as the seed is recorded), merging rows that become one (earliest first use kept), and
// moves pipeline_use's area prefetch lists to the new keys.
bool rewrite_unused(const char* path) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
    std::fprintf(stderr, "cannot open %s\n", path);
    return false;
  }
  set_per_pixel_lighting(false);
  struct Out {
    std::vector<u8> config;
    sqlite3_int64 firstFrame;
  };
  std::map<u64, Out> out;
  std::map<u64, u64> remap;
  u32 staleKeys = 0;
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT hash, config, first_frame_used FROM pipeline_cache WHERE type = 1", -1, &stmt,
                     nullptr);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const u64 hash = static_cast<u64>(sqlite3_column_int64(stmt, 0));
    PipelineConfig config;
    if (sqlite3_column_bytes(stmt, 1) != sizeof(config)) {
      std::fprintf(stderr, "row %016llx: config size %d\n", static_cast<unsigned long long>(hash),
                   sqlite3_column_bytes(stmt, 1));
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return false;
    }
    std::memcpy(&config, sqlite3_column_blob(stmt, 1), sizeof(config));
    if (aurora::xxh3_hash(config, 1) != hash) {
      ++staleKeys;
    }
    auto& sc = config.shaderConfig;
    pipeline_fog(sc);
    config.dstAlpha = pipeline_dst_alpha(config.dstAlpha);
    sc.alphaCompare = pipeline_alpha_compare(sc.alphaCompare);
    pipeline_tev_konst(sc);
    pipeline_unused_inputs(sc);
    sc.perPixelLight = sc.actorDecal == 0 && wants_per_pixel_light(sc) ? 1 : 0;
    const u64 key = aurora::xxh3_hash(config, 1);
    remap[hash] = key;
    const auto firstFrame = sqlite3_column_int64(stmt, 2);
    const auto* p = reinterpret_cast<const u8*>(&config);
    auto [it, added] = out.try_emplace(key, Out{{p, p + sizeof(config)}, firstFrame});
    if (!added) {
      it->second.firstFrame = std::min(it->second.firstFrame, firstFrame);
    }
  }
  sqlite3_finalize(stmt);

  bool ok = sqlite3_exec(db, "BEGIN; DELETE FROM pipeline_cache WHERE type = 1;", nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_prepare_v2(db,
                     "INSERT INTO pipeline_cache (type, hash, config_version, config_size, config, first_frame_used) "
                     "VALUES (1, ?1, ?2, ?3, ?4, ?5)",
                     -1, &stmt, nullptr);
  for (const auto& [key, row] : out) {
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(key));
    sqlite3_bind_int(stmt, 2, GXPipelineConfigVersion);
    sqlite3_bind_int(stmt, 3, static_cast<int>(row.config.size()));
    sqlite3_bind_blob(stmt, 4, row.config.data(), static_cast<int>(row.config.size()), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, row.firstFrame);
    ok = ok && sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
  // A context listing both of two merged rows keeps one entry.
  sqlite3_stmt* move = nullptr;
  sqlite3_stmt* drop = nullptr;
  sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO pipeline_use (context, hash) SELECT context, ?2 FROM pipeline_use WHERE hash = ?1",
                     -1, &move, nullptr);
  sqlite3_prepare_v2(db, "DELETE FROM pipeline_use WHERE hash = ?1", -1, &drop, nullptr);
  u32 moved = 0;
  for (const auto& [from, to] : remap) {
    if (from == to) {
      continue;
    }
    ++moved;
    sqlite3_bind_int64(move, 1, static_cast<sqlite3_int64>(from));
    sqlite3_bind_int64(move, 2, static_cast<sqlite3_int64>(to));
    sqlite3_bind_int64(drop, 1, static_cast<sqlite3_int64>(from));
    ok = ok && sqlite3_step(move) == SQLITE_DONE && sqlite3_step(drop) == SQLITE_DONE;
    sqlite3_reset(move);
    sqlite3_reset(drop);
  }
  sqlite3_finalize(move);
  sqlite3_finalize(drop);
  ok = ok && sqlite3_exec(db, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK;
  if (!ok) {
    std::fprintf(stderr, "rewrite failed: %s\n", sqlite3_errmsg(db));
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
  }
  sqlite3_close(db);
  std::printf("rewrite: %zu GX rows -> %zu, %u rekeyed, %u had a stale key\n", remap.size(), out.size(), moved,
              staleKeys);
  return ok;
}

// A cache's GX rows of the current config version, in key order.
std::vector<std::vector<u8>> read_rows(const char* path, size_t maxRows) {
  std::vector<std::vector<u8>> rows;
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    std::fprintf(stderr, "cannot open %s\n", path);
    std::exit(1);
  }
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db, "SELECT hash, config FROM pipeline_cache WHERE type = 1 AND config_version = ?1 ORDER BY hash", -1,
                     &stmt, nullptr);
  sqlite3_bind_int(stmt, 1, GXPipelineConfigVersion);
  while (sqlite3_step(stmt) == SQLITE_ROW && rows.size() < maxRows) {
    if (sqlite3_column_bytes(stmt, 1) != sizeof(PipelineConfig)) {
      continue;
    }
    const auto* p = static_cast<const u8*>(sqlite3_column_blob(stmt, 1));
    rows.emplace_back(p, p + sizeof(PipelineConfig));
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return rows;
}

// Result codes a row's child writes per field.
// DeadSet: dead, and the row's value isn't a value-initialised config's (a field populate_pipeline_config
// set that only splits keys, not one it left at its default).
enum : u8 { NoAlt = 0, Dead = 1, Live = 2, Mixed = 3, Crash = 4, DeadSet = 5 };
} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <cache.db> [--rows N] [--detail field] [--no-dual-source] [--check-unused] [--rewrite db] [--nearest db]\n", argv[0]);
    return 2;
  }
  size_t maxRows = SIZE_MAX;
  std::string detail;
  bool checkUnused = false;
  std::string rewrite;
  std::string nearest;
  aurora::webgpu::g_dualSourceBlendingSupported = true;
  aurora::webgpu::g_hasCoreFeatures = true;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--rows" && i + 1 < argc) {
      maxRows = std::stoul(argv[++i]);
    } else if (a == "--detail" && i + 1 < argc) {
      detail = argv[++i];
    } else if (a == "--no-dual-source") {
      aurora::webgpu::g_dualSourceBlendingSupported = false;
    } else if (a == "--check-unused") {
      checkUnused = true;
    } else if (a == "--rewrite" && i + 1 < argc) {
      rewrite = argv[++i];
    } else if (a == "--nearest" && i + 1 < argc) {
      nearest = argv[++i];
    }
  }

  const auto rows = read_rows(argv[1], maxRows);
  std::printf("%zu GX rows (version %u), dual source %d\n", rows.size(), GXPipelineConfigVersion,
              aurora::webgpu::g_dualSourceBlendingSupported);

  if (checkUnused) {
    // pipeline_unused_inputs must keep every row's pipeline, at both per-pixel lighting settings
    // (the cache load recomputes perPixelLight after it, as here).
    u32 changed = 0;
    std::set<u64> before, after;
    for (const bool ppl : {false, true}) {
      set_per_pixel_lighting(ppl);
      for (size_t r = 0; r < rows.size(); ++r) {
        PipelineConfig config;
        std::memcpy(&config, rows[r].data(), sizeof(config));
        auto& sc = config.shaderConfig;
        sc.perPixelLight = sc.actorDecal == 0 && wants_per_pixel_light(sc) ? 1 : 0;
        const u64 sig = pipeline_signature(config);
        const u64 key = aurora::xxh3_hash(config, 1);
        pipeline_unused_inputs(sc);
        sc.perPixelLight = sc.actorDecal == 0 && wants_per_pixel_light(sc) ? 1 : 0;
        if (pipeline_signature(config) != sig) {
          ++changed;
          std::printf("row %zu (per-pixel %d): pipeline changed\n", r, ppl);
        }
        before.insert(key ^ ppl);
        after.insert(aurora::xxh3_hash(config, 1) ^ ppl);
      }
    }
    std::printf("check-unused: %u changed; keys %zu -> %zu (both lighting settings)\n", changed, before.size(),
                after.size());
    return changed != 0;
  }

  if (!rewrite.empty()) {
    return rewrite_unused(rewrite.c_str()) ? 0 : 1;
  }

  const auto fields = make_fields();
  if (!nearest.empty()) {
    // Each row the other cache lacks, against that cache's closest row (fewest fields differ): what
    // sets the missing pipelines apart, so the seed's recorder knows which state it never reaches.
    const auto ref = read_rows(nearest.c_str(), SIZE_MAX);
    std::set<u64> refKeys;
    for (const auto& row : ref) {
      refKeys.insert(aurora::xxh3_hash(*reinterpret_cast<const PipelineConfig*>(row.data()), 1));
    }
    std::map<std::string, u32> byFields;
    u32 missing = 0;
    for (const auto& row : rows) {
      if (refKeys.contains(aurora::xxh3_hash(*reinterpret_cast<const PipelineConfig*>(row.data()), 1))) {
        continue;
      }
      ++missing;
      size_t best = SIZE_MAX;
      std::string bestNames;
      for (const auto& other : ref) {
        size_t n = 0;
        std::string names;
        for (const auto& f : fields) {
          // The cache load queues each row at the running MSAA too.
          if (f.name == "msaaSamples") {
            continue;
          }
          const auto mine = field_value(row.data(), f);
          const auto theirs = field_value(other.data(), f);
          if (mine != theirs) {
            if (++n > best) {
              break;
            }
            names += (names.empty() ? "" : " ") + f.name;
            if (f.size == 1) {
              names += fmt::format(" {}>{}", theirs[0], mine[0]);
            }
          }
        }
        if (n < best) {
          best = n;
          bestNames = std::move(names);
        }
      }
      ++byFields[best > 6 ? fmt::format("({} fields)", best) : bestNames];
    }
    std::vector<std::pair<u32, std::string>> sorted;
    for (const auto& [names, n] : byFields) {
      sorted.emplace_back(n, names);
    }
    std::sort(sorted.rbegin(), sorted.rend());
    std::printf("nearest: %u rows not in %s\n", missing, nearest.c_str());
    for (const auto& [n, names] : sorted) {
      std::printf("%5u  %s\n", n, names.c_str());
    }
    return 0;
  }
  // The values each field takes across the rows, its perturbations.
  std::vector<std::set<std::vector<u8>>> values(fields.size());
  for (const auto& row : rows) {
    for (size_t i = 0; i < fields.size(); ++i) {
      values[i].insert(field_value(row.data(), fields[i]));
    }
  }
  constexpr size_t MaxAlts = 6;
  static const PipelineConfig kDefault{};

  // Rows whose pipelines are the same: keys the cache already holds twice.
  std::map<u64, std::vector<size_t>> bySignature;
  // Per field: rows where it is dead, live, mixed, aborted (indexed by the result codes).
  std::vector<std::array<u32, 6>> counts(fields.size());
  u32 crashed = 0;
  std::vector<std::string> detailLines;
  for (size_t r = 0; r < rows.size(); ++r) {
    // A child tries the fields from `first` on and writes one result byte each; one that aborts (a
    // value invalid with the row's other fields, such as a direct attribute without a component
    // type) counts as Crash and the next child goes on after it.
    std::vector<u8> res;
    u64 base = 0;
    bool haveBase = false;
    while (res.size() < fields.size()) {
      const size_t first = res.size();
      int fds[2];
      if (pipe(fds) != 0) {
        return 1;
      }
      const pid_t pid = fork();
      if (pid == 0) {
        close(fds[0]);
        PipelineConfig config;
        std::memcpy(&config, rows[r].data(), sizeof(config));
        const u64 sig = pipeline_signature(config);
        (void)!write(fds[1], &sig, sizeof(sig));
        for (size_t i = first; i < fields.size(); ++i) {
          const auto cur = field_value(rows[r].data(), fields[i]);
          u8 out = NoAlt;
          size_t tried = 0;
          for (const auto& v : values[i]) {
            if (v == cur) {
              continue;
            }
            if (tried++ == MaxAlts) {
              break;
            }
            auto* bytes = reinterpret_cast<u8*>(&config);
            set_field(bytes, fields[i], v);
            const bool live = pipeline_signature(config) != sig;
            set_field(bytes, fields[i], cur);
            out |= live ? Live : Dead;
          }
          if (out == Dead && cur != field_value(reinterpret_cast<const u8*>(&kDefault), fields[i])) {
            out = DeadSet;
          }
          (void)!write(fds[1], &out, 1);
        }
        _exit(0);
      }
      close(fds[1]);
      std::vector<u8> all;
      u8 buf[4096];
      ssize_t n;
      while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        all.insert(all.end(), buf, buf + n);
      }
      close(fds[0]);
      int status = 0;
      waitpid(pid, &status, 0);
      if (all.size() < sizeof(base)) {
        std::fprintf(stderr, "row %zu: unchanged config aborts (status %d)\n", r, status);
        ++crashed;
        break;
      }
      if (!haveBase) {
        std::memcpy(&base, all.data(), sizeof(base));
        haveBase = true;
      }
      res.insert(res.end(), all.begin() + sizeof(base), all.end());
      if (res.size() < fields.size()) {
        res.push_back(Crash);
      }
    }
    if (!haveBase) {
      continue;
    }
    bySignature[base].push_back(r);
    for (size_t i = 0; i < res.size(); ++i) {
      const u8 v = res[i];
      ++counts[i][v];
      if (!detail.empty() && fields[i].name == detail && v != NoAlt) {
        PipelineConfig c;
        std::memcpy(&c, rows[r].data(), sizeof(c));
        detailLines.push_back(fmt::format(
            "row {} {}: tevStageCount {} blendMode {} src {} dst {} alphaUpdate {} colorUpdate {} dstAlpha {:x} shadowPass {}",
            r, v == Dead ? "dead" : v == DeadSet ? "deadSet" : v == Live ? "live" : v == Mixed ? "mixed" : "crash", c.shaderConfig.tevStageCount,
            underlying(c.blendMode), underlying(c.blendFacSrc), underlying(c.blendFacDst), c.alphaUpdate, c.colorUpdate,
            c.dstAlpha, c.shadowPass));
      }
    }
  }

  // Fields grouped by their name without indices.
  std::map<std::string, std::array<u32, 6>> grouped;
  for (size_t i = 0; i < fields.size(); ++i) {
    std::string g;
    bool inIndex = false;
    for (char ch : fields[i].name) {
      if (ch == '[') {
        inIndex = true;
        g += "[]";
      } else if (ch == ']') {
        inIndex = false;
      } else if (!inIndex) {
        g += ch;
      }
    }
    for (int k = 0; k < 6; ++k) {
      grouped[g][k] += counts[i][k];
    }
  }
  std::printf("\n%-40s %8s %8s %8s %8s %8s\n", "field", "deadSet", "dead", "live", "mixed", "crash");
  std::printf("(row x index counts; dead: no tried value changed the pipeline; deadSet: dead with a non-default value)\n");
  for (const auto& [g, c] : grouped) {
    if (c[Dead] + c[DeadSet] + c[Mixed] + c[Live] + c[Crash] == 0) {
      continue;
    }
    std::printf("%-40s %8u %8u %8u %8u %8u\n", g.c_str(), c[DeadSet], c[Dead], c[Live], c[Mixed], c[Crash]);
  }

  // Duplicate pipelines and the fields their rows differ in.
  size_t dupRows = 0;
  std::map<std::string, u32> dupFields;
  for (const auto& [sig, rs] : bySignature) {
    if (rs.size() < 2) {
      continue;
    }
    dupRows += rs.size() - 1;
    std::set<std::string> diff;
    for (size_t k = 1; k < rs.size(); ++k) {
      for (const auto& f : fields) {
        if (field_value(rows[rs[0]].data(), f) != field_value(rows[rs[k]].data(), f)) {
          diff.insert(f.name);
        }
      }
    }
    std::string key;
    for (const auto& d : diff) {
      key += (key.empty() ? "" : " ") + d;
    }
    dupFields[key] += rs.size() - 1;
  }
  std::printf("\n%zu rows, %zu distinct pipelines, %zu redundant keys; rows that abort unchanged %u\n", rows.size(),
              bySignature.size(), dupRows, crashed);
  for (const auto& [k, n] : dupFields) {
    std::printf("  %4u  differ in: %s\n", n, k.c_str());
  }
  for (const auto& l : detailLines) {
    std::printf("%s\n", l.c_str());
  }
  return 0;
}
