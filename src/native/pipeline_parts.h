// A pipeline key split the way graphics pipeline libraries split a pipeline
// (VK_EXT_graphics_pipeline_library, as DXVK does): vertex input, the vertex
// shader (pre-rasterization), the pixel shader (fragment shader) and the
// fragment output. Strides, culling, front face and depth/stencil are dynamic
// state (Vulkan 1.3) on that path, so they leave the key a pipeline is linked
// for. Pure: no Vulkan calls.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

#include "native/bytes_key.h"
#include "native/pipelines.h"
#include "native/prebuild_plan.h"

namespace wwe::native::parts {

// The key with the state that is dynamic on the library path cleared:
// strides become 1 for each stream that exists, culling and depth/stencil 0.
// Keys that differ only in that state share one linked pipeline.
PipelineKey LinkKey(const PipelineKey& key);

struct VertexInputPart {
  uint32_t topology;
  uint32_t stream_mask;  // streams with a binding (their strides are dynamic)
  uint32_t dummy_locations;
  uint32_t attribute_count;
  VertexAttribute attributes[kMaxAttributes];
};

struct OutputPart {
  VkFormat color_format;
  VkFormat depth_format;
  uint32_t blend_control;
  uint32_t color_mask;
};

struct Parts {
  VertexInputPart vertex_input;
  prebuild::ShaderVariant vertex;
  prebuild::ShaderVariant pixel;  // shader 0: depth-only, no fragment stage
  OutputPart output;
};

// vertex_mask / pixel_mask: the spec-constant bits each shader reads (the
// shader pack's mask). Spec constants are fixed when a library is compiled,
// so each shader library is per (shader, its own bits of the key's value).
// The parts are zero-padded, so they can be hashed and compared byte-wise.
Parts Split(const PipelineKey& key, uint32_t vertex_mask, uint32_t pixel_mask);

struct StencilOps {
  VkCompareOp compare;
  VkStencilOp fail;
  VkStencilOp pass;
  VkStencilOp depth_fail;
  bool operator==(const StencilOps&) const = default;
};

// The dynamic state a draw sets on the library path. The full-pipeline path
// bakes the same values into its pipelines.
struct DynamicState {
  VkCullModeFlags cull_mode;
  VkFrontFace front_face;
  bool depth_test;
  bool depth_write;
  VkCompareOp depth_compare;
  bool stencil_test;
  StencilOps front;
  StencilOps back;
  uint32_t strides[kMaxStreams];
  bool operator==(const DynamicState&) const = default;
};

DynamicState DynamicStateOf(const PipelineKey& key);

// The stage of a converted shader from its SPIR-V's first OpEntryPoint:
// vertex or fragment, 0 for anything else or a damaged module. The pack does
// not record stages, and a library is compiled for one.
VkShaderStageFlagBits ShaderStageOf(std::span<const uint32_t> spirv);

// What the library path did since the counters were last taken.
struct LinkCounters {
  uint64_t fast_links = 0;         // pipelines fast-linked on a miss
  uint64_t fast_linked_draws = 0;  // draws served by a fast-linked pipeline
  uint64_t swapped_in = 0;         // optimized pipelines that replaced a fast-linked one
  uint64_t optimized = 0;          // optimized pipelines built
  // "fast_links=N fast_linked_draws=N swapped_in=N optimized=N", for the
  // every-300-frames statistics line.
  std::string Fields() const;
};

// The library path's pipelines by link key: the fast-linked one until the
// optimized one is swapped in. Not thread-safe; the caller locks.
class LinkedPipelines {
 public:
  struct Found {
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool fast_linked = false;
  };
  // A draw's pipeline; a draw served by a fast-linked one is counted.
  Found Find(const PipelineKey& link_key);
  void AddFastLinked(const PipelineKey& link_key, VkPipeline pipeline);
  // True if it replaced a fast-linked pipeline (a swap-in). The fast-linked
  // pipeline stays alive: frames in flight may still use it.
  bool AddOptimized(const PipelineKey& link_key, VkPipeline pipeline);
  bool HasOptimized(const PipelineKey& link_key) const;
  LinkCounters TakeCounters();
  // Every pipeline held, for destruction at shutdown.
  template <typename Visit>
  void ForEach(Visit&& visit) const {
    for (const auto& [key, entry] : entries_) {
      visit(entry.fast);
      visit(entry.optimized);
    }
  }
  size_t OptimizedCount() const { return optimized_total_; }

 private:
  struct Entry {
    VkPipeline fast = VK_NULL_HANDLE;
    VkPipeline optimized = VK_NULL_HANDLE;
  };
  std::unordered_map<PipelineKey, Entry, BytesHash, BytesEqual> entries_;
  LinkCounters counters_;
  size_t optimized_total_ = 0;
};

}  // namespace wwe::native::parts
