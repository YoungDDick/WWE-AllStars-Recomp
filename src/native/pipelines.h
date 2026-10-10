// Graphics pipelines of the native renderer, keyed by the draw state that
// shapes them. Keys and the driver's pipeline cache persist in the cache
// folder, tagged with the GPU and driver (pipeline_store.h); at startup the
// workers build the pipelines of the shipped key list and of earlier
// sessions (prebuild_plan.h), so first use in a match does not stutter.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/ui/vulkan/api.h>

#include "native/texture_formats.h"

namespace wwe::native {

constexpr uint32_t kMaxAttributes = 16;
constexpr uint32_t kMaxStreams = 8;
// Vertex binding that feeds zeros to shader inputs a declaration leaves out.
constexpr uint32_t kDummyBinding = 15;

struct VertexAttribute {
  uint32_t location;
  uint32_t stream;
  VkFormat format;
  uint32_t offset;
};

// Plain data (zero the padding): hashed, compared and stored byte-wise.
// Shaders are named by their container hash, so a key stays valid across
// sessions as long as its shaders stay in the shader pack.
struct PipelineKey {
  uint64_t vertex_shader;
  uint64_t pixel_shader;
  uint32_t spec_constants;
  uint32_t topology;
  VkFormat color_format;
  VkFormat depth_format;
  uint32_t blend_control;
  uint32_t color_mask;
  uint32_t depth_control;
  uint32_t cull;
  uint32_t strides[kMaxStreams];
  uint32_t dummy_locations;
  uint32_t attribute_count;
  VertexAttribute attributes[kMaxAttributes];
};

// Xenos draws without a pixel shader are depth-only (depth pre-pass, shadow
// maps): no fragment stage, no colour writes.
constexpr bool IsDepthOnly(const PipelineKey& key) { return key.pixel_shader == 0; }
constexpr void MakeDepthOnly(PipelineKey& key) {
  key.pixel_shader = 0;
  key.color_mask = 0;
  key.blend_control = 0;
}

// Stored keys this build can still use: those whose shaders (vertex, and
// pixel unless depth-only) are all in the shader pack. Adding shaders to
// the pack keeps every stored key.
template <typename HasShader>
std::vector<PipelineKey> UsableKeys(const std::vector<PipelineKey>& keys,
                                    HasShader has_shader) {
  std::vector<PipelineKey> usable;
  for (const PipelineKey& key : keys) {
    if (has_shader(key.vertex_shader) && (IsDepthOnly(key) || has_shader(key.pixel_shader))) {
      usable.push_back(key);
    }
  }
  return usable;
}

namespace pipelines {

// Loads the stored cache from cache_root (or --wwe_native_pipeline_dir) and
// the key list shipped beside the exe, and has the workers prebuild the plan
// (prebuild_plan.h). converter_id names the XenosRecomp patch the shaders
// were converted with: a store from another converter is not loaded; from
// another GPU or driver only its driver cache is dropped (its keys are kept);
// keys whose shaders left the pack are dropped (UsableKeys' rule). colour is
// the session's Colour setting: the plan builds that mode's variants.
void Initialize(uint64_t converter_id, const std::filesystem::path& cache_root,
                ColourMode colour);

// The pipeline for a key. On the library path (graphics pipeline libraries
// with fast linking, pipeline_parts.h) a pipeline not built yet is fast-linked
// here from the libraries the workers built, and its optimized pipeline is
// queued and swapped in when ready; dynamic is then set, and the draw sets
// strides, culling and depth/stencil as dynamic state (parts::DynamicStateOf).
// Otherwise a new one is queued for the workers and Get returns
// VK_NULL_HANDLE with pending set until it is ready; with wait set (an
// off-screen draw, which may be drawn only once) a worker builds it first, at
// high priority, and Get waits for it (TakeWaitStats counts the wait). Only
// with async compiling off is it compiled here. VK_NULL_HANDLE without
// pending means creation failed. Render thread.
VkPipeline Get(const PipelineKey& key, bool wait, bool& pending, bool& dynamic);

// Pipelines and shader libraries the render thread compiled itself since the
// last call, and the time it spent on them.
void TakeSyncStats(uint64_t& count, uint64_t& ns);

// Pipelines the render thread waited for a worker to build since the last
// call, and the time it waited: a hitch cause.
void TakeWaitStats(uint64_t& count, uint64_t& ns);

// The library path's counters since the last call, as statistics fields
// (parts::LinkCounters::Fields plus fast-link time), or "libraries=off".
std::string TakeLinkStats();

size_t Count();

// The prepare-pipelines mode (--wwe_prepare_pipelines): workers on every
// core, and nothing else of the game runs.
bool PrepareMode();

// How far the startup work is: the pack's modules, on the library path the
// shader libraries, then the planned pipelines.
struct PrebuildStatus {
  size_t modules_built = 0;
  size_t modules_total = 0;
  size_t libraries_built = 0;  // failed builds included
  size_t libraries_total = 0;  // 0 off the library path
  size_t libraries_failed = 0;  // known once every library is built
  size_t pipelines_done = 0;
  size_t pipelines_total = 0;
  size_t pipelines_failed = 0;  // planned pipelines the driver could not build
};
PrebuildStatus Status();

// Writes the keys and the driver's cache data if pipelines were added since
// the last save (always when force is set, so a prepare leaves a store).
void Save(bool force = false);

// Removes the "pipelines stale" marker (pipeline_store.h), after a prepare.
void ClearStaleMarker();

// Has a worker thread Save, off the render thread.
void RequestSave();

// A shader converted live (live_topup.h): on the library path, builds its
// shader libraries for every spec-constant value, so its draws only fast-link.
// Returns the libraries built. Any thread but the render thread.
size_t PrepareShader(uint64_t hash);

// Stops the worker threads (before the device goes away) and saves.
void Shutdown();

}  // namespace pipelines

}  // namespace wwe::native
