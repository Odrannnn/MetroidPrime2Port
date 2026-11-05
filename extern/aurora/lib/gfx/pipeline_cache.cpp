#include "pipeline_cache.hpp"

#include "clear.hpp"
#include "fx_pipelines.hpp"
#include "resources.hpp"
#include "hash.hpp"
#include "../gx/pipeline.hpp"
#include "../io.hpp"
#ifdef AURORA_ENABLE_RMLUI
#include "../rmlui/pipeline.hpp"
#endif
#include "../sqlite_utils.hpp"
#include "../webgpu/gpu.hpp"
#include <aurora/gfx.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include <SDL3/SDL_iostream.h>
#if defined(__GLIBC__) || defined(__ANDROID__)
#include <malloc.h>
#endif
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace aurora::gfx {
static Module Log("aurora::gfx::pipeline_cache");

constexpr int PipelineCacheSchema = 1;
constexpr const char* InitialPipelineCacheName = "initial_pipeline_cache.db";
// The name the seed VFS maps to AuroraConfig::pipelineCacheSeedData instead of a file.
constexpr const char* EmbeddedSeedName = ":embedded-initial-pipeline-cache:";
constexpr const char* SdlVfsName = "aurora_pipeline_cache_sdl_vfs";

// Frames begun by the draw thread (begin_pipeline_frame). Pipelines are stamped with it when a draw
// looks them up, which is what idle eviction goes by.
static std::atomic_uint32_t g_pipelineFrame = 0;

struct CachedPipeline {
  wgpu::RenderPipeline pipeline;
  uint32_t firstFrameUsed = UINT32_MAX;
  uint32_t lastUsedFrame = g_pipelineFrame.load(std::memory_order_relaxed);
};

struct PendingPipeline {
  PipelineRef hash;
  uint32_t firstFrameUsed = UINT32_MAX;
  NewPipelineCallback create;
};

struct PipelineCacheWrite {
  ShaderType type;
  PipelineRef hash;
  uint32_t configVersion;
  ByteBuffer config;
  uint32_t firstFrameUsed = UINT32_MAX;
};

struct SdlVfsSqliteFile {
  sqlite3_file base;
  SDL_IOStream* io = nullptr;
};

static std::mutex g_pipelineMutex;
static bool g_hasPipelineThread = false;
static size_t g_pipelinesPerFrame = 0;
// For synchronous pipeline fallback (OpenGL)
#ifdef NDEBUG
constexpr size_t BuildPipelinesPerFrame = 5;
#else
constexpr size_t BuildPipelinesPerFrame = 1;
#endif
static std::vector<std::thread> g_pipelineThreads;
static std::atomic_bool g_pipelineThreadEnd = false;
static std::condition_variable g_pipelineQueueCv;
static std::condition_variable g_pipelineReadyCv;
static absl::flat_hash_map<PipelineRef, CachedPipeline> g_pipelines;
static std::deque<PendingPipeline> g_pipelineQueue;
static std::deque<PendingPipeline> g_backgroundPipelineQueue;
// Set by set_background_pipelines_paused: background builds wait, lookups still compile.
static bool g_backgroundPipelinesPaused = false;
static absl::flat_hash_set<PipelineRef> g_pendingPipelines;
static std::atomic_bool g_gpuCachePrunePending = false;
static std::chrono::steady_clock::time_point g_pipelineLoadStart;

static sqlite3* g_pipelineCacheDb = nullptr;
static sqlite3_stmt* g_pipelineCacheLoadStmt = nullptr;
static sqlite3_stmt* g_pipelineCacheUpsertStmt = nullptr;
static bool g_pipelineCacheBroken = false;
// A failed operation found the file damaged; pipeline_cache_abort() deletes it.
static std::atomic_bool g_pipelineCacheCorrupt = false;
static std::thread g_pipelineCacheWriterThread;
static std::condition_variable g_pipelineCacheWriterCv;
static std::mutex g_pipelineCacheWriterMutex;
static std::deque<PipelineCacheWrite> g_pipelineCacheWriteQueue;
static bool g_pipelineCacheWriterStop = false;
static int g_sdlVfsRegisterResult = SQLITE_ERROR;

// Per-context pipeline use (issue #187). Draws record (context, hash) in pipeline_use; a lazy cache
// builds a context's rows in the background when the game asks (aurora_pipeline_prefetch).
static sqlite3_stmt* g_pipelineUseInsertStmt = nullptr;
static sqlite3_stmt* g_pipelineContextLoadStmt = nullptr;
static sqlite3_stmt* g_pipelineSharedLoadStmt = nullptr;
static sqlite3_stmt* g_pipelineWorldLoadStmt = nullptr;
static std::atomic_uint64_t g_pipelineContext = AURORA_PIPELINE_CONTEXT_FRONTEND;
// The uses recorded this session; any draw thread (require_pipeline records from the render thread).
static std::mutex g_recordedUsesMutex;
static absl::flat_hash_set<std::pair<uint64_t, PipelineRef>> g_recordedUses;
// Under g_pipelineMutex: GLOBAL pipelines, never evicted; and (lazy only) the hashes the cache has rows for.
static absl::flat_hash_set<PipelineRef> g_pinnedPipelines;
static absl::flat_hash_set<PipelineRef> g_knownPipelines;
// Under g_pipelineCacheWriterMutex, written by the writer thread.
static std::deque<std::pair<uint64_t, PipelineRef>> g_pipelineUseQueue;
static std::deque<uint64_t> g_pipelinePrefetchQueue;
static absl::flat_hash_map<uint64_t, uint32_t> g_prefetchFrames;
constexpr uint32_t PrefetchRepeatFrames = 600;
constexpr uint64_t MaxLoggedNewMisses = 64;
constexpr size_t MaxRecordedUses = 65536;
constexpr size_t MaxKnownPipelines = 65536;
static std::atomic_uint64_t g_skippedDraws = 0;
static std::atomic_uint64_t g_newMisses = 0;
static std::atomic_uint64_t g_knownMisses = 0;
static std::atomic_uint64_t g_lateMisses = 0;
static std::atomic_uint64_t g_prefetches = 0;
static std::atomic_uint64_t g_prefetchQueued = 0;

static SdlVfsSqliteFile* sdl_vfs_file(sqlite3_file* file) { return reinterpret_cast<SdlVfsSqliteFile*>(file); }

static sqlite3_vfs* default_vfs(sqlite3_vfs* vfs) { return static_cast<sqlite3_vfs*>(vfs->pAppData); }

static int sdl_vfs_close(sqlite3_file* file) {
  auto* vfsFile = sdl_vfs_file(file);
  if (vfsFile->io != nullptr) {
    SDL_CloseIO(vfsFile->io);
    vfsFile->io = nullptr;
  }
  return SQLITE_OK;
}

static int sdl_vfs_read(sqlite3_file* file, void* buffer, int amount, sqlite3_int64 offset) {
  auto* vfsFile = sdl_vfs_file(file);
  if (vfsFile->io == nullptr || offset < 0 || amount < 0) {
    return SQLITE_IOERR_READ;
  }
  if (SDL_SeekIO(vfsFile->io, offset, SDL_IO_SEEK_SET) < 0) {
    return SQLITE_IOERR_SEEK;
  }

  auto* dst = static_cast<uint8_t*>(buffer);
  int total = 0;
  while (total < amount) {
    const size_t read = SDL_ReadIO(vfsFile->io, dst + total, static_cast<size_t>(amount - total));
    if (read == 0) {
      if (SDL_GetIOStatus(vfsFile->io) == SDL_IO_STATUS_EOF) {
        std::memset(dst + total, 0, static_cast<size_t>(amount - total));
        return SQLITE_IOERR_SHORT_READ;
      }
      return SQLITE_IOERR_READ;
    }
    if (read > static_cast<size_t>(std::numeric_limits<int>::max() - total)) {
      return SQLITE_IOERR_READ;
    }
    total += static_cast<int>(read);
  }
  return SQLITE_OK;
}

static int sdl_vfs_write(sqlite3_file*, const void*, int, sqlite3_int64) { return SQLITE_READONLY; }

static int sdl_vfs_truncate(sqlite3_file*, sqlite3_int64) { return SQLITE_READONLY; }

static int sdl_vfs_sync(sqlite3_file*, int) { return SQLITE_OK; }

static int sdl_vfs_file_size(sqlite3_file* file, sqlite3_int64* size) {
  auto* vfsFile = sdl_vfs_file(file);
  if (vfsFile->io == nullptr || size == nullptr) {
    return SQLITE_IOERR_FSTAT;
  }

  const auto ioSize = SDL_GetIOSize(vfsFile->io);
  if (ioSize < 0) {
    return SQLITE_IOERR_FSTAT;
  }
  *size = static_cast<sqlite3_int64>(ioSize);
  return SQLITE_OK;
}

static int sdl_vfs_lock(sqlite3_file*, int) { return SQLITE_OK; }

static int sdl_vfs_unlock(sqlite3_file*, int) { return SQLITE_OK; }

static int sdl_vfs_check_reserved_lock(sqlite3_file*, int* reserved) {
  if (reserved != nullptr) {
    *reserved = 0;
  }
  return SQLITE_OK;
}

static int sdl_vfs_file_control(sqlite3_file*, int op, void* arg) {
  switch (op) {
  case SQLITE_FCNTL_LOCKSTATE:
    *static_cast<int*>(arg) = SQLITE_LOCK_NONE;
    return SQLITE_OK;
  case SQLITE_FCNTL_HAS_MOVED:
    *static_cast<int*>(arg) = 0;
    return SQLITE_OK;
  case SQLITE_FCNTL_SIZE_HINT:
    return SQLITE_OK;
  default:
    return SQLITE_NOTFOUND;
  }
}

static int sdl_vfs_sector_size(sqlite3_file*) { return 4096; }

static int sdl_vfs_device_characteristics(sqlite3_file*) {
  return SQLITE_IOCAP_IMMUTABLE | SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN;
}

static constexpr sqlite3_io_methods SdlVfsIoMethods{
    .iVersion = 1,
    .xClose = sdl_vfs_close,
    .xRead = sdl_vfs_read,
    .xWrite = sdl_vfs_write,
    .xTruncate = sdl_vfs_truncate,
    .xSync = sdl_vfs_sync,
    .xFileSize = sdl_vfs_file_size,
    .xLock = sdl_vfs_lock,
    .xUnlock = sdl_vfs_unlock,
    .xCheckReservedLock = sdl_vfs_check_reserved_lock,
    .xFileControl = sdl_vfs_file_control,
    .xSectorSize = sdl_vfs_sector_size,
    .xDeviceCharacteristics = sdl_vfs_device_characteristics,
};

static int sdl_vfs_open(sqlite3_vfs*, sqlite3_filename name, sqlite3_file* file, int flags, int* outFlags) {
  auto* vfsFile = sdl_vfs_file(file);
  vfsFile->base.pMethods = nullptr;
  vfsFile->io = nullptr;

  if (name == nullptr || (flags & SQLITE_OPEN_READWRITE) != 0 || (flags & SQLITE_OPEN_READONLY) == 0) {
    return SQLITE_CANTOPEN;
  }

  if (std::strcmp(name, EmbeddedSeedName) == 0) {
    vfsFile->io = g_config.pipelineCacheSeedData != nullptr
                      ? SDL_IOFromConstMem(g_config.pipelineCacheSeedData, g_config.pipelineCacheSeedSize)
                      : nullptr;
  } else {
    vfsFile->io = SDL_IOFromFile(name, "rb");
  }
  if (vfsFile->io == nullptr) {
    return SQLITE_CANTOPEN;
  }

  vfsFile->base.pMethods = &SdlVfsIoMethods;
  if (outFlags != nullptr) {
    *outFlags = SQLITE_OPEN_READONLY;
  }
  return SQLITE_OK;
}

static int sdl_vfs_delete(sqlite3_vfs*, const char*, int) { return SQLITE_READONLY; }

static int sdl_vfs_access(sqlite3_vfs*, const char* name, int flags, int* result) {
  if (result == nullptr) {
    return SQLITE_IOERR_ACCESS;
  }
  if (name == nullptr || flags == SQLITE_ACCESS_READWRITE) {
    *result = 0;
    return SQLITE_OK;
  }

  auto* io = SDL_IOFromFile(name, "rb");
  *result = io != nullptr ? 1 : 0;
  if (io != nullptr) {
    SDL_CloseIO(io);
  }
  return SQLITE_OK;
}

static int sdl_vfs_full_pathname(sqlite3_vfs*, const char* name, int outSize, char* out) {
  if (name == nullptr || out == nullptr || outSize <= 0) {
    return SQLITE_CANTOPEN;
  }
  sqlite3_snprintf(outSize, out, "%s", name);
  return SQLITE_OK;
}

static void* sdl_vfs_dl_open(sqlite3_vfs*, const char*) { return nullptr; }

static void sdl_vfs_dl_error(sqlite3_vfs*, int bytes, char* message) {
  if (message != nullptr && bytes > 0) {
    sqlite3_snprintf(bytes, message, "%s", "Dynamic loading is unsupported");
  }
}

static void (*sdl_vfs_dl_sym(sqlite3_vfs*, void*, const char*))(void) { return nullptr; }

static void sdl_vfs_dl_close(sqlite3_vfs*, void*) {}

static int sdl_vfs_randomness(sqlite3_vfs* vfs, int bytes, char* out) {
  if (auto* base = default_vfs(vfs); base != nullptr && base->xRandomness != nullptr) {
    return base->xRandomness(base, bytes, out);
  }
  if (out != nullptr && bytes > 0) {
    std::memset(out, 0, static_cast<size_t>(bytes));
  }
  return bytes;
}

static int sdl_vfs_sleep(sqlite3_vfs* vfs, int microseconds) {
  if (auto* base = default_vfs(vfs); base != nullptr && base->xSleep != nullptr) {
    return base->xSleep(base, microseconds);
  }
  return microseconds;
}

static int sdl_vfs_current_time(sqlite3_vfs* vfs, double* time) {
  if (auto* base = default_vfs(vfs); base != nullptr && base->xCurrentTime != nullptr) {
    return base->xCurrentTime(base, time);
  }
  if (time != nullptr) {
    *time = 2440587.5;
  }
  return SQLITE_OK;
}

static int sdl_vfs_get_last_error(sqlite3_vfs*, int, char*) { return SQLITE_OK; }

static bool register_sdl_vfs() {
  static std::once_flag registerOnce;
  std::call_once(registerOnce, [] {
    auto* baseVfs = sqlite3_vfs_find(nullptr);
    static sqlite3_vfs sdlVfs{
        .iVersion = 1,
        .szOsFile = static_cast<int>(sizeof(SdlVfsSqliteFile)),
        .mxPathname = 4096,
        .pNext = nullptr,
        .zName = SdlVfsName,
        .pAppData = baseVfs,
        .xOpen = sdl_vfs_open,
        .xDelete = sdl_vfs_delete,
        .xAccess = sdl_vfs_access,
        .xFullPathname = sdl_vfs_full_pathname,
        .xDlOpen = sdl_vfs_dl_open,
        .xDlError = sdl_vfs_dl_error,
        .xDlSym = sdl_vfs_dl_sym,
        .xDlClose = sdl_vfs_dl_close,
        .xRandomness = sdl_vfs_randomness,
        .xSleep = sdl_vfs_sleep,
        .xCurrentTime = sdl_vfs_current_time,
        .xGetLastError = sdl_vfs_get_last_error,
    };
    g_sdlVfsRegisterResult = sqlite3_vfs_register(&sdlVfs, 0);
  });
  return g_sdlVfsRegisterResult == SQLITE_OK;
}

#if defined(__cpp_lib_atomic_ref)
static std::atomic_ref queuedPipelines{detail::resources().stats.queuedPipelines};
static std::atomic_ref createdPipelines{detail::resources().stats.createdPipelines};
#else
struct AtomicStatRef {
  uint32_t& ref;
  uint32_t operator++() { return __atomic_add_fetch(&ref, 1, __ATOMIC_RELAXED); }
  uint32_t operator--() { return __atomic_sub_fetch(&ref, 1, __ATOMIC_RELAXED); }
  uint32_t operator++(int) { return __atomic_fetch_add(&ref, 1, __ATOMIC_RELAXED); }
  uint32_t operator--(int) { return __atomic_fetch_sub(&ref, 1, __ATOMIC_RELAXED); }
  uint32_t operator=(uint32_t val) {
    __atomic_store_n(&ref, val, __ATOMIC_RELAXED);
    return val;
  }
};
static AtomicStatRef queuedPipelines{detail::resources().stats.queuedPipelines};
static AtomicStatRef createdPipelines{detail::resources().stats.createdPipelines};
#endif

template <typename PipelineConfig>
static PipelineCacheWrite make_pipeline_cache_write(ShaderType type, PipelineRef hash, const PipelineConfig& config,
                                                    uint32_t firstFrameUsed) {
  static_assert(std::has_unique_object_representations_v<PipelineConfig>);

  PipelineCacheWrite write{
      .type = type,
      .hash = hash,
      .configVersion = config.version,
      .config = ByteBuffer(sizeof(config)),
      .firstFrameUsed = firstFrameUsed,
  };
  std::memcpy(write.config.data(), &config, sizeof(config));
  return write;
}

static void enqueue_pipeline_cache_write(PipelineCacheWrite write) {
  if (g_pipelineCacheBroken || g_pipelineCacheDb == nullptr) {
    return;
  }

  {
    std::lock_guard lock{g_pipelineCacheWriterMutex};
    g_pipelineCacheWriteQueue.emplace_back(std::move(write));
  }
  g_pipelineCacheWriterCv.notify_one();
}

template <typename Queue>
static auto find_pending_pipeline(Queue& queue, PipelineRef hash) {
  return std::find_if(queue.begin(), queue.end(), [=](const PendingPipeline& pending) { return pending.hash == hash; });
}

static PendingPipeline* touch_pending_pipeline(PipelineRef hash, PipelinePriority priority) {
  auto priorityIt = find_pending_pipeline(g_pipelineQueue, hash);
  if (priorityIt != g_pipelineQueue.end()) {
    return &*priorityIt;
  }

  auto backgroundIt = find_pending_pipeline(g_backgroundPipelineQueue, hash);
  if (backgroundIt == g_backgroundPipelineQueue.end()) {
    return nullptr;
  }
  switch (priority) {
  case PipelinePriority::Background:
    return &*backgroundIt;
  case PipelinePriority::Normal:
    // A draw wants a pipeline that a startup load or prefetch queued but hasn't built yet.
    g_lateMisses.fetch_add(1, std::memory_order_relaxed);
    g_pipelineQueue.emplace_back(std::move(*backgroundIt));
    g_backgroundPipelineQueue.erase(backgroundIt);
    return &g_pipelineQueue.back();
  case PipelinePriority::Blocking:
    g_lateMisses.fetch_add(1, std::memory_order_relaxed);
    g_pipelineQueue.emplace_front(std::move(*backgroundIt));
    g_backgroundPipelineQueue.erase(backgroundIt);
    return &g_pipelineQueue.front();
  }
  return nullptr;
}

static std::optional<PendingPipeline> take_pending_pipeline(PipelineRef hash) {
  auto priorityIt = find_pending_pipeline(g_pipelineQueue, hash);
  if (priorityIt != g_pipelineQueue.end()) {
    PendingPipeline pending = std::move(*priorityIt);
    g_pipelineQueue.erase(priorityIt);
    g_pendingPipelines.erase(hash);
    return pending;
  }

  auto backgroundIt = find_pending_pipeline(g_backgroundPipelineQueue, hash);
  if (backgroundIt != g_backgroundPipelineQueue.end()) {
    PendingPipeline pending = std::move(*backgroundIt);
    g_backgroundPipelineQueue.erase(backgroundIt);
    g_pendingPipelines.erase(hash);
    return pending;
  }

  return std::nullopt;
}

// Slow-pipeline diagnostics (issue #8 hangs in pipeline creation): every GX
// render pipeline creation is timed here, slow ones are named, and a summary
// lands in the log as the count crosses 100, 500 and 1000.
static std::atomic<uint64_t> g_compileCount = 0;
static std::atomic<uint64_t> g_compileTotalMs = 0;
static std::atomic<uint64_t> g_compileSlowestMs = 0;
static std::atomic<uint64_t> g_compileFailures = 0;
constexpr int64_t kSlowPipelineMs = 200;

static wgpu::RenderPipeline create_pipeline_timed(PipelineRef hash, NewPipelineCallback&& create) {
  const auto start = std::chrono::steady_clock::now();
  wgpu::RenderPipeline pipeline = create();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  const uint64_t count = g_compileCount.fetch_add(1, std::memory_order_relaxed) + 1;
  g_compileTotalMs.fetch_add(static_cast<uint64_t>(ms.count()), std::memory_order_relaxed);
  uint64_t slowest = g_compileSlowestMs.load(std::memory_order_relaxed);
  while (static_cast<uint64_t>(ms.count()) > slowest &&
         !g_compileSlowestMs.compare_exchange_weak(slowest, static_cast<uint64_t>(ms.count()),
                                                   std::memory_order_relaxed)) {
  }
  if (!pipeline) {
    g_compileFailures.fetch_add(1, std::memory_order_relaxed);
    Log.error("pipeline: creation failed (hash {:016x})", hash);
  } else if (ms.count() > kSlowPipelineMs) {
    Log.warn("pipeline: compile took {} ms (hash {:016x})", ms.count(), hash);
  }
  if (count == 100 || count == 500 || count == 1000) {
    Log.info("pipelines: {} compiled, total {} ms, slowest {} ms, {} failed", count,
             g_compileTotalMs.load(std::memory_order_relaxed), g_compileSlowestMs.load(std::memory_order_relaxed),
             g_compileFailures.load(std::memory_order_relaxed));
  }
  return pipeline;
}

static void notify_pipeline_ready(bool queued) {
  ++createdPipelines;
  if (queued && --queuedPipelines == 0 && g_gpuCachePrunePending.exchange(false, std::memory_order_acq_rel)) {
    Log.info("pipeline cache: {} pipelines ready {} ms after startup", static_cast<uint32_t>(createdPipelines),
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                   g_pipelineLoadStart)
                 .count());
    // Prune GPU cache entries after fully loading the pipeline cache.
    webgpu::cache_prune();
  }
  g_pipelineReadyCv.notify_all();
}

static PipelineRef g_lastPipelineRef = std::numeric_limits<PipelineRef>::max();
// The frame g_lastPipelineRef was last stamped in: the same-pipeline shortcut only holds within it.
static uint32_t g_lastPipelineStampFrame = 0;
static std::atomic_bool g_dropPipelines = false;
// Idle eviction (see evict_idle_pipelines): never evicts anything used in the last MinEvictIdleFrames.
constexpr uint32_t MinEvictIdleFrames = 16;
constexpr uint32_t EvictSweepFrames = 600;
constexpr uint32_t NoTrimRequest = UINT32_MAX;
static std::atomic_uint32_t g_pipelineEvictFrames = 0;
static std::atomic_uint32_t g_pipelineTrimRequest = NoTrimRequest;

// drop_pipelines() only raises a flag; the next lookup (on the thread that builds draws) does the
// work, so no draw loses its pipeline in the middle of one.
static void apply_pipeline_drop() {
  std::scoped_lock guard{g_pipelineMutex};
  g_pipelines.clear();
  g_pipelineQueue.clear();
  g_backgroundPipelineQueue.clear();
  g_pendingPipelines.clear();
  g_lastPipelineRef = std::numeric_limits<PipelineRef>::max();
}

// Records that a draw in the current context uses `hash`: once per (context, hash) a session, queued
// for the writer thread. A GLOBAL use also pins the pipeline. Takes g_pipelineMutex for a pin, so
// never call it with that held.
static void record_pipeline_use(PipelineRef hash) {
  if (g_pipelineCacheBroken || g_pipelineCacheDb == nullptr) {
    return;
  }
  const uint64_t context = g_pipelineContext.load(std::memory_order_relaxed);
  {
    std::lock_guard lock{g_recordedUsesMutex};
    // Bounds the set over a long session: forgetting only re-queues uses the DB already has
    // (INSERT OR IGNORE) and re-pins GLOBAL ones.
    if (g_recordedUses.size() >= MaxRecordedUses) {
      g_recordedUses.clear();
    }
    if (!g_recordedUses.emplace(context, hash).second) {
      return;
    }
  }
  if (context == AURORA_PIPELINE_CONTEXT_GLOBAL) {
    std::lock_guard lock{g_pipelineMutex};
    g_pinnedPipelines.insert(hash);
  }
  {
    std::lock_guard lock{g_pipelineCacheWriterMutex};
    g_pipelineUseQueue.emplace_back(context, hash);
  }
  g_pipelineCacheWriterCv.notify_one();
}

// Under g_pipelineMutex: a draw lookup found `hash` neither built nor queued. Returns true for a new
// miss the caller should log (the first MaxLoggedNewMisses only).
static bool note_pipeline_miss(PipelineRef hash) {
  if (g_knownPipelines.contains(hash)) {
    g_knownMisses.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  // Past the bound a repeat miss counts as new again; only the stats tell the two apart.
  if (g_config.pipelineCacheLazy && g_knownPipelines.size() < MaxKnownPipelines) {
    g_knownPipelines.insert(hash);
  }
  return g_newMisses.fetch_add(1, std::memory_order_relaxed) < MaxLoggedNewMisses;
}

template <typename PipelineConfig>
static PipelineRef find_pipeline_impl(ShaderType type, const PipelineConfig& config, NewPipelineCallback&& cb,
                                      PipelinePriority priority = PipelinePriority::Normal,
                                      std::optional<uint32_t> firstFrameUsedOverride = std::nullopt,
                                      bool renderThread = false) {
  ZoneScoped;

  // The pipeline drop and g_lastPipelineRef belong to the thread that records draws.
  if (!renderThread && g_dropPipelines.exchange(false, std::memory_order_acq_rel)) {
    apply_pipeline_drop();
  }
  const PipelineRef hash = xxh3_hash(config, static_cast<HashType>(type));
  const bool blocking = priority == PipelinePriority::Blocking;
  const uint32_t frameNow = g_pipelineFrame.load(std::memory_order_relaxed);
  if (!blocking && !renderThread && hash == g_lastPipelineRef && g_lastPipelineStampFrame == frameNow) {
    return g_lastPipelineRef;
  }
  if (!renderThread) {
    g_lastPipelineRef = hash;
    g_lastPipelineStampFrame = frameNow;
  }
  const uint32_t firstFrameUsed = firstFrameUsedOverride.value_or(current_frame());
  bool notifyWorker = false;
  bool persist = priority != PipelinePriority::Background;
  if (persist) {
    record_pipeline_use(hash);
  }
  bool pipelineReady = false;
  bool createdPipeline = false;
  bool queued = false;
  bool logNewMiss = false;
  std::optional<PipelineCacheWrite> cacheWrite;
  {
    std::scoped_lock guard{g_pipelineMutex};
    auto pipelineIt = g_pipelines.find(hash);
    if (pipelineIt != g_pipelines.end()) {
      pipelineReady = true;
      pipelineIt->second.lastUsedFrame = frameNow;
      if (persist && firstFrameUsed < pipelineIt->second.firstFrameUsed) {
        pipelineIt->second.firstFrameUsed = firstFrameUsed;
        cacheWrite = make_pipeline_cache_write(type, hash, config, firstFrameUsed);
      }
    } else if (g_pendingPipelines.contains(hash)) {
      if (blocking && !g_hasPipelineThread) {
        auto pending = take_pending_pipeline(hash);
        if (pending) {
          if (firstFrameUsed < pending->firstFrameUsed) {
            pending->firstFrameUsed = firstFrameUsed;
            if (persist) {
              cacheWrite = make_pipeline_cache_write(type, hash, config, firstFrameUsed);
            }
          }
          g_pipelines.try_emplace(hash, CachedPipeline{
                                            .pipeline = create_pipeline_timed(hash, std::move(pending->create)),
                                            .firstFrameUsed = pending->firstFrameUsed,
                                        });
          pipelineReady = true;
          ++g_pipelinesPerFrame;
          createdPipeline = true;
          queued = true;
        }
      } else {
        auto* pending = touch_pending_pipeline(hash, priority);
        if (pending != nullptr && firstFrameUsed < pending->firstFrameUsed) {
          pending->firstFrameUsed = firstFrameUsed;
          if (persist) {
            cacheWrite = make_pipeline_cache_write(type, hash, config, firstFrameUsed);
          }
        } else if (pending == nullptr && persist) {
          cacheWrite = make_pipeline_cache_write(type, hash, config, firstFrameUsed);
        }
        notifyWorker = priority != PipelinePriority::Background;
      }
    } else if (!g_hasPipelineThread && (blocking || g_pipelinesPerFrame < BuildPipelinesPerFrame)) {
      if (persist) {
        logNewMiss = note_pipeline_miss(hash);
      }
      g_pipelines.try_emplace(hash, CachedPipeline{
                                        .pipeline = create_pipeline_timed(hash, std::move(cb)),
                                        .firstFrameUsed = firstFrameUsed,
                                    });
      pipelineReady = true;
      if (persist) {
        cacheWrite = make_pipeline_cache_write(type, hash, config, firstFrameUsed);
      }
      ++g_pipelinesPerFrame;
      createdPipeline = true;
    } else {
      PendingPipeline pending{
          .hash = hash,
          .firstFrameUsed = firstFrameUsed,
          .create = std::move(cb),
      };
      switch (priority) {
      case PipelinePriority::Background:
        g_backgroundPipelineQueue.emplace_back(std::move(pending));
        break;
      case PipelinePriority::Normal:
        g_pipelineQueue.emplace_back(std::move(pending));
        break;
      case PipelinePriority::Blocking:
        g_pipelineQueue.emplace_front(std::move(pending));
        break;
      }
      g_pendingPipelines.insert(hash);
      if (persist) {
        cacheWrite = make_pipeline_cache_write(type, hash, config, firstFrameUsed);
        logNewMiss = note_pipeline_miss(hash);
      }
      ++queuedPipelines;
      notifyWorker = true;
    }
  }

  if (logNewMiss) {
    Log.info("pipeline cache: new pipeline {:016x} (type {}, context {:x})", hash, underlying(type),
             g_pipelineContext.load(std::memory_order_relaxed));
  }

  if (cacheWrite) {
    enqueue_pipeline_cache_write(std::move(*cacheWrite));
    cacheWrite.reset();
  }

  if (createdPipeline) {
    notify_pipeline_ready(queued);
  }

  if (notifyWorker) {
    g_pipelineQueueCv.notify_one();
  }

  if (blocking && !pipelineReady) {
    const auto waitStart = std::chrono::steady_clock::now();
    std::unique_lock lock{g_pipelineMutex};
    g_pipelineReadyCv.wait(lock, [=] { return g_pipelines.contains(hash) || g_pipelineThreadEnd; });
    // A blocking lookup (particles, water, UI) stalls the draw thread for the whole compile.
    const auto waitMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - waitStart).count();
    if (waitMs >= 50) {
      Log.warn("pipeline: draw thread waited {} ms for blocking pipeline {:016x} (type {})", waitMs, hash,
               static_cast<int>(type));
    }
    auto pipelineIt = g_pipelines.find(hash);
    if (pipelineIt != g_pipelines.end() && persist && firstFrameUsed < pipelineIt->second.firstFrameUsed) {
      pipelineIt->second.firstFrameUsed = firstFrameUsed;
      cacheWrite = make_pipeline_cache_write(type, hash, config, firstFrameUsed);
    }
  }

  if (cacheWrite) {
    enqueue_pipeline_cache_write(std::move(*cacheWrite));
  }

  return hash;
}

static std::string pipeline_cache_path() {
  return io::fs_path_to_string(io::fs_path_from_string(g_config.cachePath) / "pipeline_cache.db");
}

static void note_pipeline_cache_corrupt() {
  if (g_pipelineCacheDb != nullptr && sqlite::is_corrupt(g_pipelineCacheDb)) {
    g_pipelineCacheCorrupt = true;
  }
}

// Stops using the cache for this session. A damaged database is deleted so the
// next start rebuilds it.
static void pipeline_cache_abort() {
  g_pipelineCacheBroken = true;
  note_pipeline_cache_corrupt();
  if (g_pipelineCacheLoadStmt != nullptr) {
    sqlite3_finalize(g_pipelineCacheLoadStmt);
    g_pipelineCacheLoadStmt = nullptr;
  }
  if (g_pipelineCacheUpsertStmt != nullptr) {
    sqlite3_finalize(g_pipelineCacheUpsertStmt);
    g_pipelineCacheUpsertStmt = nullptr;
  }
  for (sqlite3_stmt** stmt :
       {&g_pipelineUseInsertStmt, &g_pipelineContextLoadStmt, &g_pipelineSharedLoadStmt, &g_pipelineWorldLoadStmt}) {
    if (*stmt != nullptr) {
      sqlite3_finalize(*stmt);
      *stmt = nullptr;
    }
  }
  if (g_pipelineCacheDb != nullptr) {
    sqlite3_close(g_pipelineCacheDb);
    g_pipelineCacheDb = nullptr;
  }
  if (g_pipelineCacheCorrupt.exchange(false)) {
    Log.warn("Pipeline cache is damaged; deleting it");
    sqlite::delete_db_files(pipeline_cache_path(), Log);
  }
}

static bool write_pipeline_cache_record(const PipelineCacheWrite& write);
static bool write_pipeline_use_record(uint64_t context, PipelineRef hash);

static std::string pipeline_cache_seed_path() {
  if (g_config.pipelineCacheSeedData != nullptr && g_config.pipelineCacheSeedSize != 0) {
    return EmbeddedSeedName;
  }
  if (g_config.resourcesPath == nullptr || g_config.resourcesPath[0] == '\0') {
    return InitialPipelineCacheName;
  }

  std::string path{g_config.resourcesPath};
  if (path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  path += InitialPipelineCacheName;
  return path;
}

static sqlite3* open_pipeline_cache_seed_db(const std::string& path) {
  if (!register_sdl_vfs()) {
    Log.warn("Failed to register SDL pipeline cache seed VFS");
    return nullptr;
  }

  sqlite3* seedDb = nullptr;
  const auto ret = sqlite3_open_v2(path.c_str(), &seedDb, SQLITE_OPEN_READONLY | SQLITE_OPEN_PRIVATECACHE, SdlVfsName);
  if (ret != SQLITE_OK) {
    if (seedDb != nullptr) {
      sqlite3_close(seedDb);
    }
    Log.info("No bundled initial pipeline cache found at '{}'", path);
    return nullptr;
  }

  bool schemaMatch = false;
  const auto schemaQuery = fmt::format("SELECT 1 FROM aurora_schema WHERE value = {}", PipelineCacheSchema);
  const auto schemaRet =
      sqlite::exec(seedDb, schemaQuery.c_str(), [&schemaMatch](int, char**, char**) { schemaMatch = true; });
  if (schemaRet != SQLITE_OK) {
    Log.warn("Failed to read bundled pipeline cache schema from '{}': {}", path, sqlite3_errmsg(seedDb));
    sqlite3_close(seedDb);
    return nullptr;
  }
  if (!schemaMatch) {
    Log.warn("Bundled pipeline cache '{}' does not use schema version {}", path, PipelineCacheSchema);
    sqlite3_close(seedDb);
    return nullptr;
  }

  return seedDb;
}

static void seed_pipeline_cache() {
  if (g_config.pipelineCacheNoSeed || g_pipelineCacheBroken || g_pipelineCacheDb == nullptr ||
      g_pipelineCacheUpsertStmt == nullptr) {
    return;
  }

  const auto seedPath = pipeline_cache_seed_path();
  sqlite3* seedDb = open_pipeline_cache_seed_db(seedPath);
  if (seedDb == nullptr) {
    return;
  }

  sqlite3_stmt* seedStmt = nullptr;
  auto closeSeed = [&] {
    if (seedStmt != nullptr) {
      sqlite3_finalize(seedStmt);
      seedStmt = nullptr;
    }
    sqlite3_close(seedDb);
  };

  auto ret = sqlite3_prepare_v3(seedDb,
                                "SELECT type, hash, config_version, config_size, config, first_frame_used "
                                "FROM pipeline_cache",
                                -1, 0, &seedStmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.warn("Failed to read bundled pipeline cache rows from '{}': {}", seedPath, sqlite3_errmsg(seedDb));
    closeSeed();
    return;
  }

  bool writeFailed = false;
  bool readFailed = false;
  uint32_t mergedRows = 0;
  uint32_t skippedRows = 0;
  uint32_t mergedUses = 0;
  {
    sqlite::Transaction tx(g_pipelineCacheDb, Log, true);
    if (!tx) {
      Log.error("Failed to begin pipeline cache seed transaction");
      closeSeed();
      pipeline_cache_abort();
      return;
    }

    while ((ret = sqlite3_step(seedStmt)) == SQLITE_ROW) {
      const auto typeValue = sqlite3_column_int(seedStmt, 0);
      const auto hashValue = sqlite3_column_int64(seedStmt, 1);
      const auto configVersionValue = sqlite3_column_int(seedStmt, 2);
      const auto configSizeValue = sqlite3_column_int(seedStmt, 3);
      const auto* configBlob = static_cast<const uint8_t*>(sqlite3_column_blob(seedStmt, 4));
      const auto configBlobSize = sqlite3_column_bytes(seedStmt, 4);
      const auto firstFrameUsedValue = sqlite3_column_int64(seedStmt, 5);
      constexpr auto MaxShaderTypeValue = std::numeric_limits<std::underlying_type_t<ShaderType>>::max();
      if (typeValue < 0 || typeValue > MaxShaderTypeValue || configVersionValue < 0 || configSizeValue < 0 ||
          configSizeValue != configBlobSize || (configBlobSize > 0 && configBlob == nullptr) ||
          firstFrameUsedValue < 0 || firstFrameUsedValue > std::numeric_limits<uint32_t>::max()) {
        ++skippedRows;
        continue;
      }

      PipelineCacheWrite write{
          .type = static_cast<ShaderType>(typeValue),
          .hash = static_cast<PipelineRef>(hashValue),
          .configVersion = static_cast<uint32_t>(configVersionValue),
          .config = ByteBuffer(static_cast<size_t>(configBlobSize)),
          .firstFrameUsed = static_cast<uint32_t>(firstFrameUsedValue),
      };
      if (configBlobSize > 0) {
        std::memcpy(write.config.data(), configBlob, static_cast<size_t>(configBlobSize));
      }

      if (!write_pipeline_cache_record(write)) {
        writeFailed = true;
        break;
      }
      ++mergedRows;
    }

    if (!writeFailed && ret != SQLITE_DONE) {
      Log.warn("Failed while reading bundled pipeline cache rows from '{}': {}", seedPath, sqlite3_errmsg(seedDb));
      readFailed = true;
    }

    // A seed recorded before pipeline_use existed has no such table; it then seeds no uses.
    sqlite3_stmt* useStmt = nullptr;
    if (!writeFailed && !readFailed &&
        sqlite3_prepare_v3(seedDb, "SELECT context, hash FROM pipeline_use", -1, 0, &useStmt, nullptr) == SQLITE_OK) {
      while ((ret = sqlite3_step(useStmt)) == SQLITE_ROW) {
        if (!write_pipeline_use_record(static_cast<uint64_t>(sqlite3_column_int64(useStmt, 0)),
                                       static_cast<PipelineRef>(sqlite3_column_int64(useStmt, 1)))) {
          writeFailed = true;
          break;
        }
        ++mergedUses;
      }
      if (!writeFailed && ret != SQLITE_DONE) {
        Log.warn("Failed while reading bundled pipeline uses from '{}': {}", seedPath, sqlite3_errmsg(seedDb));
        readFailed = true;
      }
    }
    if (useStmt != nullptr) {
      sqlite3_finalize(useStmt);
    }

    if (!writeFailed && !readFailed) {
      tx.commit();
    }
  }

  closeSeed();

  if (writeFailed) {
    pipeline_cache_abort();
    return;
  }

  if (!readFailed) {
    Log.info("Seeded pipeline cache from '{}' ({} rows merged, {} rows skipped, {} uses)", seedPath, mergedRows,
             skippedRows, mergedUses);
  }
}

static bool prepare_pipeline_cache_db() {
  if (g_pipelineCacheBroken) {
    return false;
  }
  if (g_pipelineCacheDb != nullptr) {
    return true;
  }

  const auto path = pipeline_cache_path();
  auto ret = sqlite::open_cache_db(path, &g_pipelineCacheDb, Log);
  if (ret != SQLITE_OK) {
    Log.error("Failed to open pipeline cache database {}", path);
    pipeline_cache_abort();
    return false;
  }

  ret = sqlite::exec(g_pipelineCacheDb, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
  if (ret != SQLITE_OK) {
    Log.error("Failed to set pipeline cache pragmas: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return false;
  }

  bool schemaFailed = false;
  {
    sqlite::Transaction tx(g_pipelineCacheDb, Log, true);
    if (!tx) {
      Log.error("Failed to begin pipeline cache schema transaction");
      schemaFailed = true;
    } else {
      ret = sqlite::exec(g_pipelineCacheDb, "CREATE TABLE IF NOT EXISTS aurora_schema(value INTEGER);");
      if (ret != SQLITE_OK) {
        Log.error("Failed to create pipeline cache schema table: {}", sqlite3_errmsg(g_pipelineCacheDb));
        schemaFailed = true;
      }
    }

    bool schemaMatch = false;
    if (!schemaFailed) {
      const auto schemaQuery = fmt::format("SELECT 1 FROM aurora_schema WHERE value = {}", PipelineCacheSchema);
      ret = sqlite::exec(g_pipelineCacheDb, schemaQuery.c_str(),
                         [&schemaMatch](int, char**, char**) { schemaMatch = true; });
      if (ret != SQLITE_OK) {
        Log.error("Failed to read pipeline cache schema version: {}", sqlite3_errmsg(g_pipelineCacheDb));
        schemaFailed = true;
      }
    }

    if (!schemaFailed && !schemaMatch) {
      const auto schemaSql = fmt::format(
          R"(DROP TABLE IF EXISTS pipeline_use;
DROP TABLE IF EXISTS pipeline_cache;
CREATE TABLE pipeline_cache (
  type INTEGER NOT NULL,
  hash INTEGER NOT NULL,
  config_version INTEGER NOT NULL,
  config_size INTEGER NOT NULL,
  config BLOB NOT NULL,
  first_frame_used INTEGER NOT NULL,
  PRIMARY KEY (type, hash)
);
CREATE INDEX pipeline_cache_load_order_idx
  ON pipeline_cache(type, config_version, first_frame_used);
DELETE FROM aurora_schema;
INSERT INTO aurora_schema VALUES ({});)",
          PipelineCacheSchema);
      ret = sqlite::exec(g_pipelineCacheDb, schemaSql.c_str());
      if (ret != SQLITE_OK) {
        Log.error("Failed to initialize pipeline cache schema: {}", sqlite3_errmsg(g_pipelineCacheDb));
        schemaFailed = true;
      }
    }

    if (!schemaFailed) {
      tx.commit();
    }
  }

  if (schemaFailed) {
    pipeline_cache_abort();
    return false;
  }

  // Added within schema 1: older files gain the table and index, and older builds ignore them.
  ret = sqlite::exec(g_pipelineCacheDb,
                     "CREATE TABLE IF NOT EXISTS pipeline_use ("
                     "  context INTEGER NOT NULL,"
                     "  hash INTEGER NOT NULL,"
                     "  PRIMARY KEY (context, hash)"
                     ") WITHOUT ROWID;"
                     "CREATE INDEX IF NOT EXISTS pipeline_cache_hash_idx ON pipeline_cache(hash);");
  if (ret != SQLITE_OK) {
    Log.error("Failed to create pipeline use table: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return false;
  }

  // load_rows reads every row statement as (type, config_version, config, first_frame_used).
  ret = sqlite3_prepare_v3(g_pipelineCacheDb,
                           "SELECT type, config_version, config, first_frame_used FROM pipeline_cache "
                           "WHERE type = ? AND config_version = ? "
                           "ORDER BY first_frame_used ASC, rowid ASC",
                           -1, SQLITE_PREPARE_PERSISTENT, &g_pipelineCacheLoadStmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare pipeline cache load statement: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return false;
  }

  ret = sqlite3_prepare_v3(g_pipelineCacheDb, "INSERT OR IGNORE INTO pipeline_use (context, hash) VALUES (?, ?)", -1,
                           SQLITE_PREPARE_PERSISTENT, &g_pipelineUseInsertStmt, nullptr);
  if (ret == SQLITE_OK) {
    ret = sqlite3_prepare_v3(g_pipelineCacheDb,
                             "SELECT p.type, p.config_version, p.config, p.first_frame_used "
                             "FROM pipeline_use u JOIN pipeline_cache p ON p.hash = u.hash "
                             "WHERE u.context = ? ORDER BY p.first_frame_used",
                             -1, SQLITE_PREPARE_PERSISTENT, &g_pipelineContextLoadStmt, nullptr);
  }
  if (ret == SQLITE_OK) {
    // Pipelines at least three areas used: likely wanted by an area never visited yet.
    ret = sqlite3_prepare_v3(g_pipelineCacheDb,
                             "SELECT p.type, p.config_version, p.config, p.first_frame_used "
                             "FROM pipeline_cache p WHERE p.hash IN "
                             "(SELECT hash FROM pipeline_use WHERE context > 1 GROUP BY hash HAVING COUNT(*) >= 3) "
                             "ORDER BY p.first_frame_used",
                             -1, SQLITE_PREPARE_PERSISTENT, &g_pipelineSharedLoadStmt, nullptr);
  }
  if (ret == SQLITE_OK) {
    // Every area of one world: the contexts from (mlvl << 32) through (mlvl << 32) | 0xFFFFFFFF.
    ret = sqlite3_prepare_v3(g_pipelineCacheDb,
                             "SELECT p.type, p.config_version, p.config, p.first_frame_used "
                             "FROM pipeline_cache p WHERE p.hash IN "
                             "(SELECT hash FROM pipeline_use WHERE context BETWEEN ?1 AND ?2) "
                             "ORDER BY p.first_frame_used",
                             -1, SQLITE_PREPARE_PERSISTENT, &g_pipelineWorldLoadStmt, nullptr);
  }
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare pipeline use statements: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return false;
  }

  ret = sqlite3_prepare_v3(
      g_pipelineCacheDb,
      "INSERT INTO pipeline_cache (type, hash, config_version, config_size, config, first_frame_used) "
      "VALUES (?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(type, hash) DO UPDATE SET "
      "config_version = excluded.config_version, "
      "config_size = excluded.config_size, "
      "config = excluded.config, "
      "first_frame_used = MIN(pipeline_cache.first_frame_used, excluded.first_frame_used)",
      -1, SQLITE_PREPARE_PERSISTENT, &g_pipelineCacheUpsertStmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare pipeline cache upsert statement: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return false;
  }

  seed_pipeline_cache();
  if (g_pipelineCacheBroken) {
    return false;
  }

  return true;
}

static void prune_old_pipeline_cache_versions() {
  if (!prepare_pipeline_cache_db()) {
    return;
  }

  const auto clearDelete = fmt::format("DELETE FROM pipeline_cache WHERE type = {} AND config_version < {}",
                                       underlying(ShaderType::Clear), clear::ClearPipelineConfigVersion);
  auto ret = sqlite::exec(g_pipelineCacheDb, clearDelete.c_str());
  if (ret != SQLITE_OK) {
    Log.error("Failed to prune clear pipeline cache rows: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return;
  }

  const auto gxDelete = fmt::format("DELETE FROM pipeline_cache WHERE type = {} AND config_version < {}",
                                    underlying(ShaderType::GX), gx::GXPipelineConfigVersion);
  ret = sqlite::exec(g_pipelineCacheDb, gxDelete.c_str());
  if (ret != SQLITE_OK) {
    Log.error("Failed to prune GX pipeline cache rows: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return;
  }

#ifdef AURORA_ENABLE_RMLUI
  const auto rmlDelete = fmt::format("DELETE FROM pipeline_cache WHERE type = {} AND config_version < {}",
                                     underlying(ShaderType::Rml), rmlui::RmlPipelineConfigVersion);
  ret = sqlite::exec(g_pipelineCacheDb, rmlDelete.c_str());
  if (ret != SQLITE_OK) {
    Log.error("Failed to prune RmlUi pipeline cache rows: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return;
  }
#endif

  const auto vfxDelete = fmt::format("DELETE FROM pipeline_cache WHERE type = {} AND config_version < {}",
                                     underlying(ShaderType::Vfx), vfx::VfxPipelineConfigVersion);
  ret = sqlite::exec(g_pipelineCacheDb, vfxDelete.c_str());
  if (ret != SQLITE_OK) {
    Log.error("Failed to prune VFX pipeline cache rows: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return;
  }

  const auto waterDelete = fmt::format("DELETE FROM pipeline_cache WHERE type = {} AND config_version < {}",
                                       underlying(ShaderType::Water), water::WaterPipelineConfigVersion);
  ret = sqlite::exec(g_pipelineCacheDb, waterDelete.c_str());
  if (ret != SQLITE_OK) {
    Log.error("Failed to prune water pipeline cache rows: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return;
  }

  ret = sqlite::exec(g_pipelineCacheDb, "DELETE FROM pipeline_use WHERE hash NOT IN (SELECT hash FROM pipeline_cache)");
  if (ret != SQLITE_OK) {
    Log.error("Failed to prune pipeline use rows: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
  }
}

static bool write_pipeline_use_record(uint64_t context, PipelineRef hash) {
  auto ret = sqlite3_bind_int64(g_pipelineUseInsertStmt, 1, static_cast<sqlite3_int64>(context));
  if (ret == SQLITE_OK) {
    ret = sqlite3_bind_int64(g_pipelineUseInsertStmt, 2, static_cast<sqlite3_int64>(hash));
  }
  if (ret == SQLITE_OK) {
    ret = sqlite3_step(g_pipelineUseInsertStmt);
  }
  const bool ok = ret == SQLITE_DONE;
  if (!ok) {
    Log.error("Failed to insert pipeline use row: {}", sqlite3_errmsg(g_pipelineCacheDb));
    note_pipeline_cache_corrupt();
  }
  sqlite3_reset(g_pipelineUseInsertStmt);
  sqlite3_clear_bindings(g_pipelineUseInsertStmt);
  return ok;
}

static bool write_pipeline_cache_record(const PipelineCacheWrite& write) {
  const auto fail = [&]() {
    note_pipeline_cache_corrupt();
    sqlite3_reset(g_pipelineCacheUpsertStmt);
    sqlite3_clear_bindings(g_pipelineCacheUpsertStmt);
    return false;
  };

  auto ret = sqlite3_bind_int(g_pipelineCacheUpsertStmt, 1, underlying(write.type));
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline cache type: {}", sqlite3_errmsg(g_pipelineCacheDb));
    return fail();
  }
  ret = sqlite3_bind_int64(g_pipelineCacheUpsertStmt, 2, static_cast<sqlite3_int64>(write.hash));
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline cache hash: {}", sqlite3_errmsg(g_pipelineCacheDb));
    return fail();
  }
  ret = sqlite3_bind_int(g_pipelineCacheUpsertStmt, 3, static_cast<int>(write.configVersion));
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline cache config version: {}", sqlite3_errmsg(g_pipelineCacheDb));
    return fail();
  }
  ret = sqlite3_bind_int(g_pipelineCacheUpsertStmt, 4, static_cast<int>(write.config.size()));
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline cache config size: {}", sqlite3_errmsg(g_pipelineCacheDb));
    return fail();
  }
  ret = sqlite3_bind_blob64(g_pipelineCacheUpsertStmt, 5, write.config.data(), write.config.size(), SQLITE_TRANSIENT);
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline cache config blob: {}", sqlite3_errmsg(g_pipelineCacheDb));
    return fail();
  }
  ret = sqlite3_bind_int64(g_pipelineCacheUpsertStmt, 6, static_cast<sqlite3_int64>(write.firstFrameUsed));
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline cache first-frame-used: {}", sqlite3_errmsg(g_pipelineCacheDb));
    return fail();
  }

  ret = sqlite3_step(g_pipelineCacheUpsertStmt);
  if (ret != SQLITE_DONE) {
    Log.error("Failed to upsert pipeline cache row: {}", sqlite3_errmsg(g_pipelineCacheDb));
    return fail();
  }

  sqlite3_reset(g_pipelineCacheUpsertStmt);
  sqlite3_clear_bindings(g_pipelineCacheUpsertStmt);
  return true;
}

static size_t prefetch_pipeline_context(uint64_t context, bool withShared, bool& ok);

static void pipeline_cache_writer() {
#ifdef TRACY_ENABLE
  tracy::SetThreadName("Pipeline cache writer thread");
#endif

  while (true) {
    std::deque<PipelineCacheWrite> batch;
    std::deque<std::pair<uint64_t, PipelineRef>> uses;
    std::deque<uint64_t> prefetches;
    bool stopping = false;
    {
      std::unique_lock lock{g_pipelineCacheWriterMutex};
      g_pipelineCacheWriterCv.wait(lock, [] {
        return g_pipelineCacheWriterStop || !g_pipelineCacheWriteQueue.empty() || !g_pipelineUseQueue.empty() ||
               !g_pipelinePrefetchQueue.empty();
      });
      stopping = g_pipelineCacheWriterStop;
      if (stopping && g_pipelineCacheWriteQueue.empty() && g_pipelineUseQueue.empty()) {
        return;
      }
      batch.swap(g_pipelineCacheWriteQueue);
      uses.swap(g_pipelineUseQueue);
      prefetches.swap(g_pipelinePrefetchQueue);
    }

    bool writeFailed = false;
    if (!batch.empty() || !uses.empty()) {
      sqlite::Transaction tx(g_pipelineCacheDb, Log, true);
      if (!tx) {
        Log.error("Failed to begin pipeline cache write transaction");
        writeFailed = true;
      } else {
        // Rows before uses, so a prefetch's join already sees a new pipeline's row.
        for (const auto& write : batch) {
          if (!write_pipeline_cache_record(write)) {
            writeFailed = true;
            break;
          }
        }
        for (size_t i = 0; !writeFailed && i < uses.size(); ++i) {
          writeFailed = !write_pipeline_use_record(uses[i].first, uses[i].second);
        }

        if (!writeFailed) {
          tx.commit();
        }
      }
    }

    // Prefetches only read and queue Background builds; nothing waits on them.
    for (size_t i = 0; !writeFailed && !stopping && i < prefetches.size(); ++i) {
      bool ok = true;
      const size_t queued = prefetch_pipeline_context(prefetches[i], i == 0, ok);
      writeFailed = !ok;
      g_prefetches.fetch_add(1, std::memory_order_relaxed);
      g_prefetchQueued.fetch_add(queued, std::memory_order_relaxed);
    }

    if (writeFailed) {
      pipeline_cache_abort();
      return;
    }
  }
}

static void pipeline_worker() {
#ifdef TRACY_ENABLE
  tracy::SetThreadName("Pipeline compilation thread");
#endif

  while (g_hasPipelineThread || g_pipelinesPerFrame < BuildPipelinesPerFrame) {
    PendingPipeline pending;
    {
      std::unique_lock lock{g_pipelineMutex};
      if (g_hasPipelineThread) {
        // Several workers share the queues, so check them again under the lock every time.
        g_pipelineQueueCv.wait(lock, [] {
          return !g_pipelineQueue.empty() || (!g_backgroundPipelinesPaused && !g_backgroundPipelineQueue.empty()) ||
                 g_pipelineThreadEnd;
        });
      } else if (g_pipelineQueue.empty() && (g_backgroundPipelinesPaused || g_backgroundPipelineQueue.empty())) {
        return;
      }
      if (g_pipelineThreadEnd) {
        break;
      }
      auto& source = !g_pipelineQueue.empty() ? g_pipelineQueue : g_backgroundPipelineQueue;
      pending = std::move(source.front());
      source.pop_front();
    }
    auto result = create_pipeline_timed(pending.hash, std::move(pending.create));
    {
      std::lock_guard lock{g_pipelineMutex};
      g_pipelines.try_emplace(pending.hash, CachedPipeline{
                                                .pipeline = std::move(result),
                                                .firstFrameUsed = pending.firstFrameUsed,
                                            });
      g_pendingPipelines.erase(pending.hash);
    }
    if (!g_hasPipelineThread) {
      ++g_pipelinesPerFrame;
    }
    notify_pipeline_ready(true);
  }
}

// Pipelines compile on a pool: Tint and the driver's compiler are single threaded, so one
// worker left a cold cache (a new install, a driver update) compiling for over a minute on
// phones. GL contexts are per thread: GLES keeps one worker, and desktop GL defaults to one
// (Dawn has up to 4 shared pipeline contexts for MP2_PIPELINE_THREADS to use). MP2_PIPELINE_THREADS
// overrides the count.
static size_t pipeline_thread_count() {
  // OpenGL: one compile context. AMD's Windows driver runs links on shared contexts one at a time
  // (four threads made each ~4x slower) and crashed in its own threads with four of them.
  if (webgpu::g_backendType == wgpu::BackendType::OpenGLES ||
      (webgpu::g_backendType == wgpu::BackendType::OpenGL && std::getenv("MP2_PIPELINE_THREADS") == nullptr)) {
    return 1;
  }
  if (const char* env = std::getenv("MP2_PIPELINE_THREADS"); env != nullptr && *env != '\0') {
    return std::clamp<size_t>(std::strtoul(env, nullptr, 10), 1, 16);
  }
  // D3D11 is only picked where D3D12 and Vulkan failed: old GPUs, whose drivers often can't
  // create objects concurrently, so the runtime runs every Create* under one lock. More workers
  // only queue on it, and the game thread's buffer and texture creation waits behind them all.
  if (webgpu::g_backendType == wgpu::BackendType::D3D11) {
    return 1;
  }
  return std::clamp<size_t>(std::thread::hardware_concurrency() / 2, 1, 8);
}

struct CachedRowOptions {
  // Queued from a thread other than the draw thread (the writer's prefetch).
  bool renderThread = false;
  // Stamp rows already built as used now, so idle eviction keeps them a while longer.
  bool touchBuilt = false;
  bool pin = false;
  // Move rows still waiting in the background queue ahead of it: an area's prefetch overtakes the
  // backlog of its world's.
  bool promote = false;
};

struct CachedRowStats {
  size_t accepted = 0;
  size_t incompatible = 0;
  size_t pbr = 0;
  size_t queued = 0;
  size_t promoted = 0;
};

template <typename PipelineConfig, typename CreateFn>
static void queue_cached_config(ShaderType type, const PipelineConfig& config, const CreateFn& create,
                                uint32_t firstFrameUsed, const CachedRowOptions& opts, CachedRowStats& stats) {
  const PipelineRef hash = xxh3_hash(config, static_cast<HashType>(type));
  {
    std::scoped_lock guard{g_pipelineMutex};
    if (opts.pin) {
      g_pinnedPipelines.insert(hash);
    }
    if (const auto it = g_pipelines.find(hash); it != g_pipelines.end()) {
      if (opts.touchBuilt) {
        it->second.lastUsedFrame = g_pipelineFrame.load(std::memory_order_relaxed);
      }
      return;
    }
    if (g_pendingPipelines.contains(hash)) {
      if (opts.promote) {
        // To the front of the background queue, in this prefetch's order (workers popping meanwhile
        // only shift it by a few). Not the priority queue: that one is for draws waiting now.
        if (auto it = find_pending_pipeline(g_backgroundPipelineQueue, hash); it != g_backgroundPipelineQueue.end()) {
          PendingPipeline pending = std::move(*it);
          g_backgroundPipelineQueue.erase(it);
          const size_t at = std::min(stats.promoted, g_backgroundPipelineQueue.size());
          g_backgroundPipelineQueue.insert(g_backgroundPipelineQueue.begin() + at, std::move(pending));
          ++stats.promoted;
        }
      }
      return;
    }
  }
  find_pipeline_impl(type, config, [=] { return create(config); }, PipelinePriority::Background, firstFrameUsed,
                     opts.renderThread);
  ++stats.queued;
}

template <typename PipelineConfig, typename CreateFn>
static void queue_cached_row(ShaderType type, uint32_t configVersion, CreateFn&& create, const uint8_t* configBlob,
                             int configSize, uint32_t firstFrameUsed, const CachedRowOptions& opts,
                             CachedRowStats& stats) {
  if (configSize != static_cast<int>(sizeof(PipelineConfig)) || (configSize != 0 && configBlob == nullptr)) {
    return;
  }

  PipelineConfig config;
  std::memcpy(&config, configBlob, sizeof(config));
  if (config.version != configVersion) {
    return;
  }

  if constexpr (std::is_same_v<PipelineConfig, gx::PipelineConfig>) {
    if (!gx::pipeline_config_compatible(config, webgpu::g_lightmapBinding)) {
      ++stats.incompatible;
      return;
    }
    // A row recorded before the fog curve, the alpha compare references, the konst selections and
    // the dst alpha constant moved to uniforms (or the blend constant) names them.
    config.shaderConfig.fogType = gx::pipeline_fog_type(config.shaderConfig.fogType);
    config.dstAlpha = gx::pipeline_dst_alpha(config.dstAlpha);
    config.shaderConfig.alphaCompare = gx::pipeline_alpha_compare(config.shaderConfig.alphaCompare);
    gx::pipeline_tev_konst(config.shaderConfig);
    // A row recorded at the other aurora_set_per_pixel_lighting setting (the embedded seed is
    // recorded with it off) misses every lit draw, and each room's first visit drops them while
    // they build (issue #44). It is set before the cache loads, so the row takes the running one.
    config.shaderConfig.perPixelLight =
        config.shaderConfig.actorDecal == 0 && gx::wants_per_pixel_light(config.shaderConfig) ? 1 : 0;
    // Left in the database, and built by find_pipeline on the first draw that wants it.
    if (g_config.pipelineCacheSkipPbr && config.shaderConfig.pbr != 0) {
      ++stats.pbr;
      return;
    }
  }

  ++stats.accepted;
  // A row holds the sample count of the pass it was recorded in, so a cache (or the embedded seed)
  // recorded at another MSAA setting misses every scene draw, and each first-use effect drops its
  // draws while it builds (the flicker of issue #44). The scene's variant queues as well. A 1x row
  // may also be an offscreen pass (1x at any setting), so it stays; a 4x row is always the scene's.
  // Shadow passes are 1x on purpose.
  if constexpr (requires { config.msaaSamples; }) {
    const uint32_t samples = webgpu::g_graphicsConfig.msaaSamples;
    bool scenePass = samples != 0 && config.msaaSamples != samples;
    if constexpr (requires { config.shadowPass; }) {
      scenePass = scenePass && config.shadowPass == 0;
    }
    if (scenePass) {
      const bool keep = config.msaaSamples == 1;
      PipelineConfig variant = config;
      variant.msaaSamples = samples;
      queue_cached_config(type, variant, create, firstFrameUsed, opts, stats);
      if (!keep) {
        return;
      }
    }
  }
  queue_cached_config(type, config, create, firstFrameUsed, opts, stats);
}

static void queue_cached_row_any(int type, uint32_t configVersion, const uint8_t* blob, int size, uint32_t firstFrame,
                                 const CachedRowOptions& opts, CachedRowStats& stats) {
  switch (static_cast<ShaderType>(type)) {
#ifdef AURORA_ENABLE_RMLUI
  case ShaderType::Rml:
    if (configVersion == rmlui::RmlPipelineConfigVersion) {
      queue_cached_row<rmlui::PipelineConfig>(ShaderType::Rml, configVersion, rmlui::create_pipeline, blob, size,
                                              firstFrame, opts, stats);
    }
    break;
#endif
  case ShaderType::Clear:
    if (configVersion == clear::ClearPipelineConfigVersion) {
      queue_cached_row<clear::PipelineConfig>(ShaderType::Clear, configVersion, clear::create_pipeline, blob, size,
                                              firstFrame, opts, stats);
    }
    break;
  case ShaderType::GX:
    if (configVersion == gx::GXPipelineConfigVersion) {
      queue_cached_row<gx::PipelineConfig>(ShaderType::GX, configVersion, gx::create_pipeline, blob, size, firstFrame,
                                           opts, stats);
    }
    break;
  case ShaderType::Vfx:
    if (configVersion == vfx::VfxPipelineConfigVersion) {
      queue_cached_row<vfx::PipelineConfig>(ShaderType::Vfx, configVersion, vfx::create_pipeline, blob, size,
                                            firstFrame, opts, stats);
    }
    break;
  case ShaderType::Water:
    if (configVersion == water::WaterPipelineConfigVersion) {
      queue_cached_row<water::PipelineConfig>(ShaderType::Water, configVersion, water::create_pipeline, blob, size,
                                              firstFrame, opts, stats);
    }
    break;
  default:
    break;
  }
}

// Steps a statement whose columns are (type, config_version, config, first_frame_used), with its
// parameters already bound, and queues each row; resets the statement. False on a read failure.
static bool load_rows(sqlite3_stmt* stmt, const CachedRowOptions& opts, CachedRowStats& stats) {
  int ret;
  while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
    const int type = sqlite3_column_int(stmt, 0);
    const auto configVersion = static_cast<uint32_t>(sqlite3_column_int64(stmt, 1));
    const auto* configBlob = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, 2));
    const auto configSize = sqlite3_column_bytes(stmt, 2);
    const auto firstFrameUsed = static_cast<uint32_t>(sqlite3_column_int64(stmt, 3));
    queue_cached_row_any(type, configVersion, configBlob, configSize, firstFrameUsed, opts, stats);
  }
  const bool ok = ret == SQLITE_DONE;
  if (!ok) {
    Log.error("Failed to read pipeline cache rows: {}", sqlite3_errmsg(g_pipelineCacheDb));
  }
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  return ok;
}

static size_t load_pipeline_cache_entries(ShaderType type, uint32_t configVersion) {
  if (!prepare_pipeline_cache_db()) {
    return 0;
  }

  auto ret = sqlite3_bind_int(g_pipelineCacheLoadStmt, 1, underlying(type));
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline cache load type: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return 0;
  }
  ret = sqlite3_bind_int(g_pipelineCacheLoadStmt, 2, static_cast<int>(configVersion));
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline cache load config version: {}", sqlite3_errmsg(g_pipelineCacheDb));
    pipeline_cache_abort();
    return 0;
  }

  CachedRowStats stats;
  if (!load_rows(g_pipelineCacheLoadStmt, {}, stats)) {
    pipeline_cache_abort();
  }
  if (type == ShaderType::GX) {
    Log.info("pipeline cache: skipped {} incompatible GX rows", stats.incompatible);
    if (g_config.pipelineCacheSkipPbr) {
      Log.info("pipeline cache: {} PBR rows skipped ({})", stats.pbr,
               g_config.pipelineCacheSkipPbrReason != nullptr ? g_config.pipelineCacheSkipPbrReason : "no PBR");
    }
  }
  return stats.accepted;
}

// Queues the Background builds for one context's recorded rows. The startup lazy load runs it on
// the init thread; aurora_pipeline_prefetch's requests run it on the writer thread.
static bool is_world_context(uint64_t context) {
  return (context & 0xFFFFFFFFull) == 0xFFFFFFFFull && context > AURORA_PIPELINE_CONTEXT_FRONTEND;
}

static CachedRowStats queue_pipeline_context(uint64_t context, const CachedRowOptions& opts, bool& ok) {
  CachedRowStats stats;
  // A WORLD context stands for all of that world's areas. Contexts are stored as signed 64-bit
  // integers; the low 32 bits only add to the high ones, so one world's range stays contiguous.
  const bool world = is_world_context(context);
  sqlite3_stmt* stmt = world ? g_pipelineWorldLoadStmt : g_pipelineContextLoadStmt;
  auto ret = sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(world ? context & ~0xFFFFFFFFull : context));
  if (ret == SQLITE_OK && world) {
    ret = sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(context));
  }
  if (ret != SQLITE_OK) {
    Log.error("Failed to bind pipeline use context: {}", sqlite3_errmsg(g_pipelineCacheDb));
    sqlite3_clear_bindings(stmt);
    ok = false;
    return stats;
  }
  ok = load_rows(stmt, opts, stats);
  return stats;
}

static size_t prefetch_pipeline_context(uint64_t context, bool withShared, bool& ok) {
  // An area's rows go ahead of whatever its world's prefetch still has queued.
  const auto contextStats = queue_pipeline_context(
      context, {.renderThread = true, .touchBuilt = true, .pin = false, .promote = !is_world_context(context)}, ok);
  CachedRowStats sharedStats;
  if (ok && withShared) {
    ok = load_rows(g_pipelineSharedLoadStmt, {.renderThread = true, .touchBuilt = false, .pin = false}, sharedStats);
  }
  Log.info("pipeline cache: prefetch context {:x}: {} rows, {} queued, {} promoted; shared {} rows, {} queued",
           context, contextStats.accepted, contextStats.queued, contextStats.promoted, sharedStats.accepted,
           sharedStats.queued);
  return contextStats.queued + sharedStats.queued;
}

static size_t load_pipeline_cache() {
  if (!prepare_pipeline_cache_db()) {
    return 0;
  }
  prune_old_pipeline_cache_versions();
  if (g_pipelineCacheBroken) {
    return 0;
  }

  g_pipelineEvictFrames.store(g_config.pipelineEvictFrames, std::memory_order_relaxed);
  g_pipelineTrimRequest.store(NoTrimRequest, std::memory_order_relaxed);
  if (g_config.pipelineCacheLazy) {
    // Only GLOBAL (pinned) and FRONTEND rows queue now, in the background; an area's rows queue when
    // the game prefetches it. Returns 0 either way: nothing waits on a startup compile.
    {
      sqlite3_stmt* stmt = nullptr;
      if (sqlite3_prepare_v2(g_pipelineCacheDb, "SELECT hash FROM pipeline_cache", -1, &stmt, nullptr) == SQLITE_OK) {
        std::scoped_lock guard{g_pipelineMutex};
        while (sqlite3_step(stmt) == SQLITE_ROW) {
          g_knownPipelines.insert(static_cast<PipelineRef>(sqlite3_column_int64(stmt, 0)));
        }
      }
      sqlite3_finalize(stmt);
    }
    bool ok = true;
    const auto global = queue_pipeline_context(AURORA_PIPELINE_CONTEXT_GLOBAL,
                                               {.renderThread = false, .touchBuilt = true, .pin = true}, ok);
    CachedRowStats frontend;
    if (ok) {
      frontend = queue_pipeline_context(AURORA_PIPELINE_CONTEXT_FRONTEND,
                                        {.renderThread = false, .touchBuilt = true, .pin = false}, ok);
    }
    if (!ok) {
      pipeline_cache_abort();
      return 0;
    }
    size_t known = 0;
    {
      std::scoped_lock guard{g_pipelineMutex};
      known = g_knownPipelines.size();
    }
    Log.info("pipeline cache: lazy, {} rows known; queued {} global (pinned) and {} frontend in the background", known,
             global.queued, frontend.queued);
    return 0;
  }

  size_t acceptedRows = 0;
#ifdef AURORA_ENABLE_RMLUI
  acceptedRows += load_pipeline_cache_entries(ShaderType::Rml, rmlui::RmlPipelineConfigVersion);
#endif
  acceptedRows += load_pipeline_cache_entries(ShaderType::Clear, clear::ClearPipelineConfigVersion);
  acceptedRows += load_pipeline_cache_entries(ShaderType::GX, gx::GXPipelineConfigVersion);
  acceptedRows += load_pipeline_cache_entries(ShaderType::Vfx, vfx::VfxPipelineConfigVersion);
  acceptedRows += load_pipeline_cache_entries(ShaderType::Water, water::WaterPipelineConfigVersion);
  return acceptedRows;
}

static void start_pipeline_cache_writer() {
  if (!prepare_pipeline_cache_db()) {
    return;
  }

  g_pipelineCacheWriterStop = false;
  g_pipelineCacheWriterThread = std::thread(pipeline_cache_writer);
}

static void stop_pipeline_cache_writer() {
  if (g_pipelineCacheWriterThread.joinable()) {
    {
      std::lock_guard lock{g_pipelineCacheWriterMutex};
      g_pipelineCacheWriterStop = true;
    }
    g_pipelineCacheWriterCv.notify_one();
    g_pipelineCacheWriterThread.join();
  } else {
    g_pipelineCacheWriterStop = false;
  }

  g_pipelineCacheWriteQueue.clear();
}

template <>
PipelineRef find_pipeline(ShaderType type, const clear::PipelineConfig& config, NewPipelineCallback&& cb) {
  return find_pipeline_impl(type, config, std::move(cb));
}

template <>
PipelineRef find_pipeline(ShaderType type, const gx::PipelineConfig& config, NewPipelineCallback&& cb) {
  return find_pipeline_impl(type, config, std::move(cb));
}

PipelineRef find_pipeline(ShaderType type, const gx::PipelineConfig& config, NewPipelineCallback&& cb,
                          PipelinePriority priority) {
  return find_pipeline_impl(type, config, std::move(cb), priority);
}

bool touch_pipeline(PipelineRef ref) {
  if (g_dropPipelines.exchange(false, std::memory_order_acq_rel)) {
    apply_pipeline_drop();
  }
  const uint32_t frameNow = g_pipelineFrame.load(std::memory_order_relaxed);
  if (ref == g_lastPipelineRef && g_lastPipelineStampFrame == frameNow) {
    return true;
  }
  {
    std::scoped_lock guard{g_pipelineMutex};
    if (const auto it = g_pipelines.find(ref); it != g_pipelines.end()) {
      it->second.lastUsedFrame = frameNow;
    } else {
      if (!g_pendingPipelines.contains(ref)) {
        return false;
      }
      touch_pending_pipeline(ref, PipelinePriority::Normal);
    }
  }
  g_lastPipelineRef = ref;
  g_lastPipelineStampFrame = frameNow;
  // A memoised pipeline drawn in another context is a use of that context too.
  record_pipeline_use(ref);
  return true;
}

template <typename Config>
static wgpu::RenderPipeline require_pipeline_impl(ShaderType type, const Config& config, NewPipelineCallback&& cb) {
  const PipelineRef ref =
      find_pipeline_impl(type, config, std::move(cb), PipelinePriority::Blocking, std::nullopt, true);
  wgpu::RenderPipeline pipeline;
  get_pipeline(ref, pipeline);
  return pipeline;
}

template <>
wgpu::RenderPipeline require_pipeline(ShaderType type, const vfx::PipelineConfig& config, NewPipelineCallback&& cb) {
  return require_pipeline_impl(type, config, std::move(cb));
}

template <>
wgpu::RenderPipeline require_pipeline(ShaderType type, const water::PipelineConfig& config, NewPipelineCallback&& cb) {
  return require_pipeline_impl(type, config, std::move(cb));
}

#ifdef AURORA_ENABLE_RMLUI
template <>
PipelineRef find_pipeline(ShaderType type, const rmlui::PipelineConfig& config, NewPipelineCallback&& cb) {
  return find_pipeline_impl(type, config, std::move(cb), PipelinePriority::Blocking, 0);
}
#endif

void initialize_pipeline_cache() {
  g_pipelineCacheBroken = false;
  g_compileCount.store(0, std::memory_order_relaxed);
  g_compileTotalMs.store(0, std::memory_order_relaxed);
  g_compileSlowestMs.store(0, std::memory_order_relaxed);
  g_compileFailures.store(0, std::memory_order_relaxed);
  for (auto* counter : {&g_skippedDraws, &g_newMisses, &g_knownMisses, &g_lateMisses, &g_prefetches, &g_prefetchQueued}) {
    counter->store(0, std::memory_order_relaxed);
  }
  g_pipelineContext.store(AURORA_PIPELINE_CONTEXT_FRONTEND, std::memory_order_relaxed);
  g_pipelineCacheWriterStop = false;
  g_pipelineThreadEnd = false;
  g_gpuCachePrunePending = false;
  // See pipeline_thread_count: under D3D11 a build blocks the game thread's buffer and texture
  // creation, so the seed waits until the app resumes background builds (in game, for the port).
  g_backgroundPipelinesPaused = webgpu::g_backendType == wgpu::BackendType::D3D11;

  g_pipelineLoadStart = std::chrono::steady_clock::now();
  if (webgpu::g_backendType == wgpu::BackendType::WebGPU) {
    g_hasPipelineThread = false;
  } else {
    g_hasPipelineThread = true;
    const size_t threads = pipeline_thread_count();
    for (size_t i = 0; i < threads; ++i) {
      g_pipelineThreads.emplace_back(pipeline_worker);
    }
    Log.info("pipeline cache: {} compile thread(s)", threads);
  }

  const size_t loadedCount = load_pipeline_cache();
  if (!g_pipelineCacheBroken && loadedCount > 0) {
    g_gpuCachePrunePending = true;
  }

  if (!g_pipelineCacheBroken) {
    start_pipeline_cache_writer();
  }
}

void shutdown_pipeline_cache() {
  if (g_hasPipelineThread) {
    g_pipelineThreadEnd = true;
    g_pipelineQueueCv.notify_all();
    g_pipelineReadyCv.notify_all();
    for (auto& thread : g_pipelineThreads) {
      thread.join();
    }
    g_pipelineThreads.clear();
  }
  g_hasPipelineThread = false;

  stop_pipeline_cache_writer();
  pipeline_cache_abort();
  g_pipelineCacheBroken = false;
  g_pipelinesPerFrame = 0;
  g_gpuCachePrunePending = false;
  g_pipelines.clear();
  g_pipelineQueue.clear();
  g_backgroundPipelineQueue.clear();
  g_pendingPipelines.clear();
  g_pinnedPipelines.clear();
  g_knownPipelines.clear();
  {
    std::lock_guard lock{g_recordedUsesMutex};
    g_recordedUses.clear();
  }
  g_pipelineUseQueue.clear();
  g_pipelinePrefetchQueue.clear();
  g_prefetchFrames.clear();
  g_pipelineContext.store(AURORA_PIPELINE_CONTEXT_FRONTEND, std::memory_order_relaxed);

  queuedPipelines = 0;
  createdPipelines = 0;
}

void drop_pipelines() { g_dropPipelines.store(true, std::memory_order_release); }

void set_background_pipelines_paused(bool paused) {
  {
    std::lock_guard lock{g_pipelineMutex};
    if (g_backgroundPipelinesPaused == paused) {
      return;
    }
    g_backgroundPipelinesPaused = paused;
  }
  Log.info("pipeline cache: background builds {}", paused ? "paused" : "resumed");
  if (!paused) {
    g_pipelineQueueCv.notify_all();
  }
}

// Releases the built pipelines no draw has looked up for `minIdle` frames. Their cache rows stay, so
// the next draw that wants one rebuilds it. Draw thread, at frame start only.
//
// Why a pipeline in use is never released: every draw stamps its pipeline (find_pipeline,
// touch_pipeline) with the frame it records in, and a draw reaches the render worker at most a few
// frames later, where get_pipeline reads it. minIdle is at least MinEvictIdleFrames, well above the
// frames in flight, so a pipeline a pending frame draws with was stamped within that window. A
// reference kept across frames (the command processor's memo) goes through touch_pipeline, which
// stamps it or reports it gone so the caller looks the config up again. Queued builds are not in
// g_pipelines and are untouched; a build finishing later is stamped at insertion.
static uint32_t evict_idle_pipelines(uint32_t minIdle, uint32_t* totalOut) {
  const uint32_t frameNow = g_pipelineFrame.load(std::memory_order_relaxed);
  uint32_t evicted = 0;
  std::scoped_lock guard{g_pipelineMutex};
  for (auto it = g_pipelines.begin(); it != g_pipelines.end();) {
    if (frameNow - it->second.lastUsedFrame >= minIdle && !g_pinnedPipelines.contains(it->first)) {
      g_pipelines.erase(it++);
      ++evicted;
    } else {
      ++it;
    }
  }
  *totalOut = static_cast<uint32_t>(g_pipelines.size());
  if (evicted != 0) {
    g_lastPipelineRef = std::numeric_limits<PipelineRef>::max();
  }
  return evicted;
}

static void log_eviction(const char* reason, uint32_t minIdle) {
  uint32_t kept = 0;
  const uint32_t evicted = evict_idle_pipelines(minIdle, &kept);
#if defined(__GLIBC__) || defined(__ANDROID__)
  // Dawn and the driver free the pipelines' state into the malloc heap, which keeps the pages
  // unless asked to give them back.
  if (evicted != 0) {
#if defined(__ANDROID__)
    mallopt(M_PURGE, 0); // bionic has no malloc_trim
#else
    malloc_trim(0);
#endif
  }
#endif
  Log.info("pipeline cache: evicted {} pipelines unused for {} frames ({}), {} kept", evicted, minIdle, reason, kept);
}

void begin_pipeline_frame() {
  if (!g_hasPipelineThread) {
    g_pipelinesPerFrame = 0;
  }
  const uint32_t frame = g_pipelineFrame.fetch_add(1, std::memory_order_relaxed) + 1;
  // Each frame starts outside any area; the game sets the context while it draws one.
  g_pipelineContext.store(AURORA_PIPELINE_CONTEXT_FRONTEND, std::memory_order_relaxed);
  if (const uint32_t trim = g_pipelineTrimRequest.exchange(NoTrimRequest, std::memory_order_acq_rel);
      trim != NoTrimRequest) {
    log_eviction(trim == MinEvictIdleFrames ? "memory trim" : "requested", std::max(trim, MinEvictIdleFrames));
  }
  const uint32_t idle = g_pipelineEvictFrames.load(std::memory_order_relaxed);
  if (idle != 0 && frame % EvictSweepFrames == 0) {
    log_eviction("idle", std::max(idle, MinEvictIdleFrames));
  }
}

void end_pipeline_frame() {
  if (!g_hasPipelineThread) {
    pipeline_worker();
  }
}

void note_skipped_draw() { g_skippedDraws.fetch_add(1, std::memory_order_relaxed); }

bool get_pipeline(PipelineRef ref, wgpu::RenderPipeline& pipeline) {
  std::lock_guard guard{g_pipelineMutex};
  const auto it = g_pipelines.find(ref);
  // A failed build stays cached as a null pipeline so it isn't retried every draw; report it as
  // unavailable, or the caller binds null and Dawn invalidates the pass (other draws then render
  // with stale state: wrong textures, black surfaces).
  if (it == g_pipelines.end() || !it->second.pipeline) {
    return false;
  }
  pipeline = it->second.pipeline;
  return true;
}

} // namespace aurora::gfx

extern "C" {
void aurora_set_pipeline_eviction(uint32_t idleFrames) {
  aurora::gfx::g_pipelineEvictFrames.store(idleFrames, std::memory_order_relaxed);
}

void aurora_trim_pipelines(uint32_t idleFrames) {
  aurora::gfx::g_pipelineTrimRequest.store(idleFrames, std::memory_order_release);
}

uint32_t aurora_get_pipeline_count(void) {
  std::lock_guard guard{aurora::gfx::g_pipelineMutex};
  return static_cast<uint32_t>(aurora::gfx::g_pipelines.size());
}

uint64_t aurora_pipeline_set_context(uint64_t context) {
  using namespace aurora::gfx;
  const uint64_t prev = g_pipelineContext.exchange(context, std::memory_order_relaxed);
  if (prev != context) {
    // The same-pipeline shortcut skips recording; a new context must see its first lookup.
    g_lastPipelineRef = std::numeric_limits<PipelineRef>::max();
  }
  return prev;
}

void aurora_pipeline_prefetch(uint64_t context) {
  using namespace aurora::gfx;
  if (!aurora::g_config.pipelineCacheLazy || g_pipelineCacheBroken || g_pipelineCacheDb == nullptr || !g_hasPipelineThread) {
    return;
  }
  const uint32_t frame = g_pipelineFrame.load(std::memory_order_relaxed);
  {
    std::lock_guard lock{g_pipelineCacheWriterMutex};
    if (g_pipelineCacheWriterStop) {
      return;
    }
    const auto [it, inserted] = g_prefetchFrames.try_emplace(context, frame);
    if (!inserted) {
      if (frame - it->second < PrefetchRepeatFrames) {
        return;
      }
      it->second = frame;
    }
    g_pipelinePrefetchQueue.push_back(context);
  }
  g_pipelineCacheWriterCv.notify_one();
}

void aurora_get_pipeline_stats(AuroraPipelineStats* stats) {
  using namespace aurora::gfx;
  if (stats == nullptr) {
    return;
  }
  stats->skippedDraws = g_skippedDraws.load(std::memory_order_relaxed);
  stats->newMisses = g_newMisses.load(std::memory_order_relaxed);
  stats->knownMisses = g_knownMisses.load(std::memory_order_relaxed);
  stats->lateMisses = g_lateMisses.load(std::memory_order_relaxed);
  stats->prefetches = g_prefetches.load(std::memory_order_relaxed);
  stats->prefetchQueued = g_prefetchQueued.load(std::memory_order_relaxed);
  std::lock_guard guard{g_pipelineMutex};
  stats->pinned = static_cast<uint32_t>(g_pinnedPipelines.size());
  stats->known = static_cast<uint32_t>(g_knownPipelines.size());
  stats->pending = static_cast<uint32_t>(g_pendingPipelines.size());
}
}
