#ifndef AURORA_GFX_H
#define AURORA_GFX_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include "stddef.h"
#include "stdint.h"
#endif

#if !defined(NDEBUG) && !defined(AURORA_GFX_DEBUG_GROUPS)
#define AURORA_GFX_DEBUG_GROUPS
#endif

void push_debug_group(const char* label);
void pop_debug_group();

typedef struct {
  uint32_t queuedPipelines;
  uint32_t createdPipelines;
  uint32_t drawCallCount;
  uint32_t mergedDrawCallCount;
  uint32_t renderPassCount; // render passes the frame's EFB recording encoded
  uint32_t lastVertSize;
  uint32_t lastUniformSize;
  uint32_t lastIndexSize;
  uint32_t lastStorageSize;
  uint32_t lastTextureUploadSize;
} AuroraStats;

const AuroraStats* aurora_get_stats();
// Live GPU textures: [0] sampled only, [1] render targets/copies, [2] Remastered lighting (room cubes,
// ambient volumes, baked lightmaps and the fixed BRDF/empty ones). Bytes are the mip chain's size.
typedef struct {
  uint32_t count[3];
  uint64_t bytes[3];
} AuroraTextureStats;
void aurora_get_texture_stats(AuroraTextureStats* out);
// Dawn's own estimate of the device's GPU memory (all textures and buffers, whoever made them) and what its
// backend allocator holds. false until the device exists.
typedef struct {
  uint64_t total, textures, buffers; // ComputeEstimatedMemoryUsageInfo
  uint64_t allocatorUsed, allocatorAllocated; // GetAllocatorMemoryInfo
} AuroraDawnMemory;
bool aurora_get_dawn_memory(AuroraDawnMemory* out);
// Any thread: within a second, hand the GPU memory Dawn keeps for reuse back to the driver. The submit
// thread also does it on its own once that memory grows by 64 MiB.
void aurora_reduce_gpu_memory(void);
// Windows: the adapter's local memory budget and this process's use of it (DXGI QueryVideoMemoryInfo;
// on an integrated GPU, shared system memory). False elsewhere, or when DXGI can't say.
bool aurora_get_gpu_memory_budget(uint64_t* budget, uint64_t* usage);
// Any thread: the memory left before the system runs short (the smallest of free RAM, free commit
// and the GPU budget, as the caller measured it), and the levels below which texture replacements
// lighten their load. Under `moderateBytes` replacements idle for 10 s are freed and new loads stop
// at 2048 px; under `severeBytes` all but the ones the last second drew are freed, new loads stop at
// 1024 px and drawn ones above that reload smaller. The bytes replacements publish count against
// `headroomBytes` until the next call. A level of 0 is off; both 0 (the default) turn this off.
void aurora_set_memory_pressure(int64_t headroomBytes, int64_t moderateBytes, int64_t severeBytes);
// Pipeline eviction (the pipeline cache keeps its rows, so an evicted pipeline rebuilds on its next draw).
// Idle eviction: built pipelines no draw used for `idleFrames` frames are released; 0 turns it off.
void aurora_set_pipeline_eviction(uint32_t idleFrames);
// Any thread: at the next frame start, release the built pipelines unused for `idleFrames` frames
// (at least 16 are always kept). Android onTrimMemory and the console use it.
void aurora_trim_pipelines(uint32_t idleFrames);
// Built pipelines held now.
uint32_t aurora_get_pipeline_count(void);
// Pipeline use contexts. Draws record the pipelines they use under the current context, so a later
// session can build them before they are needed. GLOBAL pipelines (player, gun, HUD) are built at
// startup and never evicted; FRONTEND is the default; an area is (uint64_t(mlvl) << 32) | areaIdx.
#define AURORA_PIPELINE_CONTEXT_GLOBAL 0ull
#define AURORA_PIPELINE_CONTEXT_FRONTEND 1ull
#define AURORA_PIPELINE_CONTEXT_AREA(mlvl, area) ((((uint64_t)(uint32_t)(mlvl)) << 32) | (uint32_t)(area))
// Prefetch only: every area of world `mlvl`. Draws never record under it.
#define AURORA_PIPELINE_CONTEXT_WORLD(mlvl) AURORA_PIPELINE_CONTEXT_AREA(mlvl, 0xFFFFFFFFu)
// Draw thread: the context the draws that follow record under; returns the previous one. Every frame
// starts in FRONTEND.
uint64_t aurora_pipeline_set_context(uint64_t context);
// Any thread: queue background builds of the pipelines `context` used before, plus those shared by
// several areas. Lazy pipeline cache only; a context prefetched in the last 600 frames is skipped.
// An AREA's rows still queued by a WORLD prefetch move ahead of the rest.
void aurora_pipeline_prefetch(uint64_t context);
typedef struct {
  uint64_t skippedDraws; // draws dropped because their pipeline was not built yet
  uint64_t newMisses;    // draw lookups of a pipeline the cache had never recorded
  uint64_t knownMisses;  // draw lookups of a recorded pipeline that was not built or queued
  uint64_t lateMisses;   // draw lookups of a pipeline still waiting in the background queue
  uint64_t prefetches;   // prefetch requests run
  uint64_t prefetchQueued; // rows those requests queued for a build
  uint32_t pinned;       // GLOBAL pipelines kept from eviction
  uint32_t known;        // pipelines the cache has rows for (lazy only)
  uint32_t pending;      // pipelines queued or building
} AuroraPipelineStats;
void aurora_get_pipeline_stats(AuroraPipelineStats* out);
// Live sampled/target TextureRefs grouped by creation label, size and format, most numerous first. Fills up to
// `cap` groups and returns how many exist; `totals` (optional) gets the count and bytes over all of them.
typedef struct {
  const char* label;
  char format[24];
  uint32_t width, height, mips;
  uint32_t count;
  uint64_t bytes; // all of the group's textures together
} AuroraTextureGroup;
uint32_t aurora_get_texture_groups(AuroraTextureGroup* out, uint32_t cap);
// Textures made by each direct CreateTexture call site that is not a TextureRef (cumulative, not live).
typedef struct AuroraTextureSite {
  const char* site; // file:line under extern/aurora/lib
  uint64_t count;
  uint64_t bytes;
} AuroraTextureSite;
// Returns the number of sites; fills up to `cap`.
uint32_t aurora_get_texture_sites(AuroraTextureSite* out, uint32_t cap);
// The AuroraConfig::frameBufferScale in use: the device may allow less than was asked for.
uint32_t aurora_get_frame_buffer_scale();
// The AuroraConfig::frameStorageScale in use (the array buffer's, in units of its 8 MiB at 1).
uint32_t aurora_get_frame_storage_scale();
// The MiB set aside for AuroraConfig::residentGeometryMiB: the device may allow less.
uint32_t aurora_get_resident_geometry_mib();
// The bytes of it in use, as of the last frame processed.
uint64_t aurora_get_resident_geometry_used();
float aurora_get_fps();
// Whether the device samples BC and ASTC 4x4 compressed textures (false until it exists).
void aurora_get_texture_support(bool* bc, bool* astc);
// A GX texture object's (GXTexObj*) base level decoded to RGBA8 into `out` (cap bytes); false for
// palette or compressed PC formats and when it does not fit.
bool aurora_gx_texobj_rgba8(const void* obj, uint32_t* width, uint32_t* height, uint8_t* out, size_t cap);

void aurora_enable_vsync(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
