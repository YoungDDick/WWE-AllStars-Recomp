#include "native/pipeline_parts.h"

#include <cstring>

#include <fmt/format.h>

namespace wwe::native::parts {

namespace {

StencilOps Stencil(uint32_t func, uint32_t fail, uint32_t pass, uint32_t depth_fail) {
  // Xenos CompareFunction and StencilOp values equal Vulkan's.
  return {VkCompareOp(func), VkStencilOp(fail), VkStencilOp(pass), VkStencilOp(depth_fail)};
}

}  // namespace

PipelineKey LinkKey(const PipelineKey& key) {
  PipelineKey link = key;
  for (uint32_t& stride : link.strides) {
    stride = stride ? 1 : 0;
  }
  link.cull = 0;
  link.depth_control = 0;
  return link;
}

Parts Split(const PipelineKey& key, uint32_t vertex_mask, uint32_t pixel_mask) {
  Parts parts;
  std::memset(&parts, 0, sizeof(parts));
  parts.vertex_input.topology = key.topology;
  for (uint32_t stream = 0; stream < kMaxStreams; ++stream) {
    if (key.strides[stream]) {
      parts.vertex_input.stream_mask |= 1u << stream;
    }
  }
  parts.vertex_input.dummy_locations = key.dummy_locations;
  parts.vertex_input.attribute_count = key.attribute_count;
  std::memcpy(parts.vertex_input.attributes, key.attributes, sizeof(key.attributes));
  parts.vertex = {key.vertex_shader, key.spec_constants & vertex_mask};
  if (!IsDepthOnly(key)) {
    parts.pixel = {key.pixel_shader, key.spec_constants & pixel_mask};
  }
  parts.output = {key.color_format, key.depth_format, key.blend_control, key.color_mask};
  return parts;
}

DynamicState DynamicStateOf(const PipelineKey& key) {
  DynamicState state;
  std::memset(&state, 0, sizeof(state));
  state.cull_mode = ((key.cull & 1) ? VK_CULL_MODE_FRONT_BIT : 0) |
                    ((key.cull & 2) ? VK_CULL_MODE_BACK_BIT : 0);
  state.front_face = (key.cull & 4) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
  std::memcpy(state.strides, key.strides, sizeof(state.strides));
  state.depth_compare = VK_COMPARE_OP_NEVER;
  state.front = state.back = Stencil(0, 0, 0, 0);
  if (key.depth_format == VK_FORMAT_UNDEFINED) {
    return state;
  }
  const uint32_t dc = key.depth_control;
  state.depth_test = (dc >> 1) & 1;
  state.depth_write = (dc >> 2) & 1;
  state.depth_compare = VkCompareOp((dc >> 4) & 7);
  state.stencil_test = dc & 1;
  state.front = Stencil((dc >> 8) & 7, (dc >> 11) & 7, (dc >> 14) & 7, (dc >> 17) & 7);
  state.back = ((dc >> 7) & 1)
                   ? Stencil((dc >> 20) & 7, (dc >> 23) & 7, (dc >> 26) & 7, (dc >> 29) & 7)
                   : state.front;
  return state;
}

VkShaderStageFlagBits ShaderStageOf(std::span<const uint32_t> spirv) {
  constexpr uint32_t kOpEntryPoint = 15;
  constexpr size_t kHeaderWords = 5;
  for (size_t at = kHeaderWords; at < spirv.size();) {
    const uint32_t word_count = spirv[at] >> 16;
    if (!word_count || at + word_count > spirv.size()) {
      break;
    }
    if ((spirv[at] & 0xFFFF) == kOpEntryPoint && word_count > 1) {
      switch (spirv[at + 1]) {
        case 0: return VK_SHADER_STAGE_VERTEX_BIT;
        case 4: return VK_SHADER_STAGE_FRAGMENT_BIT;
        default: return VkShaderStageFlagBits(0);
      }
    }
    at += word_count;
  }
  return VkShaderStageFlagBits(0);
}

std::string LinkCounters::Fields() const {
  return fmt::format("fast_links={} fast_linked_draws={} swapped_in={} optimized={}", fast_links,
                     fast_linked_draws, swapped_in, optimized);
}

LinkedPipelines::Found LinkedPipelines::Find(const PipelineKey& link_key) {
  const auto it = entries_.find(link_key);
  if (it == entries_.end()) {
    return {};
  }
  if (it->second.optimized) {
    return {it->second.optimized, false};
  }
  ++counters_.fast_linked_draws;
  return {it->second.fast, true};
}

void LinkedPipelines::AddFastLinked(const PipelineKey& link_key, VkPipeline pipeline) {
  Entry& entry = entries_[link_key];
  if (!entry.fast) {
    entry.fast = pipeline;
    ++counters_.fast_links;
  }
}

bool LinkedPipelines::AddOptimized(const PipelineKey& link_key, VkPipeline pipeline) {
  Entry& entry = entries_[link_key];
  if (entry.optimized) {
    return false;
  }
  entry.optimized = pipeline;
  ++counters_.optimized;
  ++optimized_total_;
  if (entry.fast) {
    ++counters_.swapped_in;
    return true;
  }
  return false;
}

bool LinkedPipelines::HasOptimized(const PipelineKey& link_key) const {
  const auto it = entries_.find(link_key);
  return it != entries_.end() && it->second.optimized;
}

LinkCounters LinkedPipelines::TakeCounters() {
  const LinkCounters counters = counters_;
  counters_ = {};
  return counters;
}

}  // namespace wwe::native::parts
