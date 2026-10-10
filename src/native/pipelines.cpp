#include "native/pipelines.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "native/bytes_key.h"
#include "native/guest_decode.h"
#include "native/module_table.h"
#include "native/pipeline_parts.h"
#include "native/pipeline_store.h"
#include "native/prebuild_plan.h"
#include "native/shader_library.h"
#include "native/thread_budget.h"
#include "native/vk_context.h"

REXCVAR_DEFINE_BOOL(wwe_native_pipeline_cache, true, "WWEAllStars",
                    "Native renderer: keep pipelines in the cache folder and rebuild them "
                    "in the background at startup");
REXCVAR_DEFINE_STRING(wwe_native_pipeline_dir, "", "WWEAllStars",
                      "Native renderer: folder of the pipeline store and driver cache; empty "
                      "is native-pipelines in the cache_root folder (scene runs share one)");
REXCVAR_DEFINE_BOOL(wwe_native_async_pipelines, true, "WWEAllStars",
                    "Native renderer: compile new pipelines on worker threads; main-scene "
                    "draws wait (are skipped) a frame or two instead of stalling the game");
REXCVAR_DEFINE_BOOL(wwe_native_pipeline_libraries, true, "WWEAllStars",
                    "Native renderer: on GPUs with graphics pipeline libraries and fast "
                    "linking, fast-link a pipeline not built yet instead of skipping or "
                    "compiling the draw, and swap in the optimized one when a worker has it. "
                    "false: full pipelines only (the device keeps the extension)");
REXCVAR_DEFINE_STRING(wwe_native_pipeline_optimize, "monolithic", "WWEAllStars",
                      "Native renderer, library path: how the optimized pipeline that replaces "
                      "a fast-linked one is built: monolithic (a full compile, as DXVK does) or "
                      "lto (a link-time-optimized link of the libraries)");
REXCVAR_DEFINE_BOOL(wwe_prepare_pipelines, false, "WWEAllStars",
                    "Prepare pipelines and exit: build the shader modules and every planned "
                    "pipeline on all cores, save the store and driver cache, print progress "
                    "(the launcher's setup step; src/native/prepare_output.h)");
REXCVAR_DECLARE(bool, wwe_native_debug_no_blend);
REXCVAR_DEFINE_BOOL(wwe_native_debug_solid_ps, false, "WWEAllStars",
                    "Native renderer debugging: replace every pixel shader with solid magenta "
                    "(isolates geometry and output from textures and constants)");

namespace wwe::native::pipelines {

namespace {

using guest::InputLocation;
using guest::kInputLocations;

constexpr char kKeysFile[] = "keys.bin";
constexpr char kCacheFile[] = "driver-cache.bin";
constexpr char kShippedKeysFile[] = "pipeline-keys.txt";

// The library path (pipeline_parts.h): shader libraries per (shader, spec
// value), built by the workers at startup; vertex-input and output libraries
// per part, built on first use; linked pipelines by link key.
struct Libraries {
  bool enabled = false;
  bool lto = false;
  std::vector<prebuild::ShaderVariant> variants;  // build order: keyed first
  std::map<std::pair<uint64_t, uint32_t>, size_t> index;
  std::unique_ptr<ModuleTable<VkPipeline>> shaders;
  std::mutex parts_mutex;
  // Shader libraries of variants outside the plan: shaders converted live
  // (live_topup.h), built by the converter thread before the shader is used.
  std::map<std::pair<uint64_t, uint32_t>, VkPipeline> unplanned;  // under parts_mutex
  std::unordered_map<parts::VertexInputPart, VkPipeline, BytesHash, BytesEqual> vertex_input;
  std::unordered_map<parts::OutputPart, VkPipeline, BytesHash, BytesEqual> output;
  std::once_flag empty_fragment_once;
  VkPipeline empty_fragment = VK_NULL_HANDLE;
  parts::LinkedPipelines linked;  // under g.mutex
  // Workers still building shader libraries, and since when.
  std::atomic<uint32_t> builders_left{0};
  std::chrono::steady_clock::time_point start;
  // Render thread: fast links and the time they took, since the last stats.
  uint64_t link_ns = 0;
  std::atomic<uint64_t> parts_created{0};  // vertex-input/output libraries off the workers
} lib;

struct State {
  std::mutex mutex;
  std::mutex save_mutex;  // the render thread and shutdown both save
  std::unordered_map<PipelineKey, VkPipeline, BytesHash, BytesEqual> pipelines;
  std::unordered_map<PipelineKey, bool, BytesHash, BytesEqual> met;  // library path: full keys
  std::vector<PipelineKey> created;  // in creation order, for Save
  size_t saved = 0;
  VkPipelineCache cache = VK_NULL_HANDLE;
  // The largest driver cache on disk for this device: loaded at startup or
  // written since (pipeline_store::ReplacesDriverCache). Under save_mutex.
  uint64_t driver_cache_bytes = 0;
  uint64_t converter_id = 0;
  ColourMode colour = ColourMode::kNatural;  // the plan builds this mode's variants
  std::filesystem::path directory;
  pipeline_store::DeviceTag device = {};
  uint32_t device_features = 0;  // pipeline_store::kFeature*
  std::atomic<uint32_t> errors_logged{0};
  // Workers compile queued keys: stored ones at startup, then new ones asked
  // for with Get(key, false). queued holds keys queued or being compiled. On
  // the library path the keys are link keys and the work is the optimized
  // pipeline.
  std::condition_variable_any work_ready;
  std::deque<PipelineKey> queue;
  std::unordered_map<PipelineKey, bool, BytesHash, BytesEqual> queued;
  std::vector<std::jthread> workers;
  prebuild::Progress prebuild;  // the startup plan's pipelines
  bool save_requested = false;
  // Full keys the render thread waits for (off-screen draws, keys whose
  // libraries failed): built before anything else, at high priority.
  // pipeline_built wakes it when a full pipeline is stored.
  std::deque<PipelineKey> urgent;
  std::condition_variable pipeline_built;
  bool stopping = false;
  // Workers still building the shader pack's modules, and since when.
  std::atomic<uint32_t> module_builders_left{0};
  std::chrono::steady_clock::time_point modules_start;
  // Render thread: pipelines it created itself, and pipelines it waited for
  // a worker to build, with the time each took.
  uint64_t sync_count = 0;
  uint64_t sync_ns = 0;
  uint64_t wait_count = 0;
  uint64_t wait_ns = 0;
} g;

thread_local bool t_worker = false;

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

// Written beside the target and renamed over it, so a crash mid-write keeps
// the previous file.
bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!file) {
      return false;
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  return !error;
}

uint64_t NsSince(std::chrono::steady_clock::time_point start) {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count());
}

VkBlendFactor BlendFactor(uint32_t factor) {
  switch (factor) {
    case 0: return VK_BLEND_FACTOR_ZERO;
    case 1: return VK_BLEND_FACTOR_ONE;
    case 4: return VK_BLEND_FACTOR_SRC_COLOR;
    case 5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 6: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 7: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 8: return VK_BLEND_FACTOR_DST_COLOR;
    case 9: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 10: return VK_BLEND_FACTOR_DST_ALPHA;
    case 11: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 12: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 13: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 14: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 15: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    case 16: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default: return VK_BLEND_FACTOR_ONE;
  }
}

VkBlendOp BlendOp(uint32_t op) {
  switch (op) {
    case 1: return VK_BLEND_OP_SUBTRACT;
    case 2: return VK_BLEND_OP_MIN;
    case 3: return VK_BLEND_OP_MAX;
    case 4: return VK_BLEND_OP_REVERSE_SUBTRACT;
    default: return VK_BLEND_OP_ADD;
  }
}

VkStencilOpState StencilState(const parts::StencilOps& ops) {
  VkStencilOpState state = {};
  state.compareOp = ops.compare;
  state.failOp = ops.fail;
  state.passOp = ops.pass;
  state.depthFailOp = ops.depth_fail;
  state.compareMask = 0xFF;
  state.writeMask = 0xFF;
  return state;
}

constexpr uint32_t kDebugSolidPixelShader[] = {
#include "native/debug_solid_ps.inc"
};

VkShaderModule DebugSolidPixelShader() {
  static std::once_flag once;
  static VkShaderModule module = VK_NULL_HANDLE;
  std::call_once(once, [] {
    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = sizeof(kDebugSolidPixelShader);
    info.pCode = kDebugSolidPixelShader;
    g_vk.dfn->vkCreateShaderModule(g_vk.vk_device, &info, nullptr, &module);
  });
  return module;
}

VkShaderModule PixelModule(uint64_t hash) {
  return REXCVAR_GET(wwe_native_debug_solid_ps) ? DebugSolidPixelShader()
                                                : shader_library::Module(hash);
}

// Dynamic state on both paths, and the states the library path adds
// (Vulkan 1.3 extended dynamic state), each listed with the library holding it.
constexpr VkDynamicState kPreRasterDynamic[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR,
                                                VK_DYNAMIC_STATE_CULL_MODE,
                                                VK_DYNAMIC_STATE_FRONT_FACE};
constexpr VkDynamicState kFragmentDynamic[] = {
    VK_DYNAMIC_STATE_STENCIL_REFERENCE,  VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP};
constexpr VkDynamicState kVertexInputDynamic[] = {VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE};
constexpr VkDynamicState kOutputDynamic[] = {VK_DYNAMIC_STATE_BLEND_CONSTANTS};
constexpr VkDynamicState kStaticPathDynamic[] = {
    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK};

template <size_t N>
VkPipelineDynamicStateCreateInfo DynamicInfo(const VkDynamicState (&states)[N]) {
  VkPipelineDynamicStateCreateInfo info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  info.dynamicStateCount = uint32_t(N);
  info.pDynamicStates = states;
  return info;
}

// The create-info pieces of one pipeline, filled from a key. Not copyable:
// the pieces point into each other.
struct Desc {
  Desc() = default;
  Desc(const Desc&) = delete;
  Desc& operator=(const Desc&) = delete;

  uint32_t spec_values[2] = {};
  VkSpecializationMapEntry spec_entry = {0, 0, sizeof(uint32_t)};
  VkSpecializationInfo spec_info[2] = {};
  VkPipelineShaderStageCreateInfo stages[2] = {};
  VkVertexInputBindingDescription bindings[kMaxStreams + 1] = {};
  VkVertexInputAttributeDescription attributes[kMaxAttributes * 2] = {};
  VkPipelineVertexInputStateCreateInfo vertex_input = {
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  VkPipelineViewportStateCreateInfo viewport = {
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  VkPipelineRasterizationStateCreateInfo raster = {
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  VkPipelineMultisampleStateCreateInfo multisample = {
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  VkPipelineColorBlendAttachmentState blend_attachment = {};
  VkPipelineColorBlendStateCreateInfo blend = {
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  VkFormat color_format = VK_FORMAT_UNDEFINED;
};

// One shader stage; spec is the spec-constant value it is compiled with.
void FillStage(Desc& d, uint32_t slot, VkShaderStageFlagBits stage, VkShaderModule module,
               uint32_t spec) {
  d.spec_values[slot] = spec;
  d.spec_info[slot] = {1, &d.spec_entry, sizeof(uint32_t), &d.spec_values[slot]};
  VkPipelineShaderStageCreateInfo& s = d.stages[slot];
  s = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  s.stage = stage;
  s.module = module;
  s.pName = "main";
  s.pSpecializationInfo = &d.spec_info[slot];
}

void FillVertexInput(Desc& d, const PipelineKey& key) {
  uint32_t binding_count = 0;
  for (uint32_t stream = 0; stream < kMaxStreams; ++stream) {
    if (key.strides[stream]) {
      d.bindings[binding_count++] = {stream, key.strides[stream], VK_VERTEX_INPUT_RATE_VERTEX};
    }
  }
  if (key.dummy_locations) {
    d.bindings[binding_count++] = {kDummyBinding, 0, VK_VERTEX_INPUT_RATE_VERTEX};
  }
  uint32_t attribute_count = 0;
  for (uint32_t i = 0; i < key.attribute_count; ++i) {
    d.attributes[attribute_count++] = {key.attributes[i].location, key.attributes[i].stream,
                                       key.attributes[i].format, key.attributes[i].offset};
  }
  for (const InputLocation& l : kInputLocations) {
    if (key.dummy_locations & (1u << l.location)) {
      d.attributes[attribute_count++] = {
          l.location, kDummyBinding,
          l.is_uint ? VK_FORMAT_R32G32B32A32_UINT : VK_FORMAT_R32G32B32A32_SFLOAT, 0};
    }
  }
  d.vertex_input.vertexBindingDescriptionCount = binding_count;
  d.vertex_input.pVertexBindingDescriptions = d.bindings;
  d.vertex_input.vertexAttributeDescriptionCount = attribute_count;
  d.vertex_input.pVertexAttributeDescriptions = d.attributes;
  d.input_assembly.topology = VkPrimitiveTopology(key.topology);
  // Strips from index buffers use the all-ones index as a restart marker.
  d.input_assembly.primitiveRestartEnable =
      key.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
      key.topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
      key.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
}

// Rasterization and depth/stencil; on the library path both come from
// dynamic state, so the baked values are placeholders.
void FillRasterAndDepth(Desc& d, const PipelineKey& key, bool dynamic) {
  d.viewport.viewportCount = 1;
  d.viewport.scissorCount = 1;
  d.raster.polygonMode = VK_POLYGON_MODE_FILL;
  d.raster.lineWidth = 1.0f;
  d.multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  if (dynamic) {
    return;
  }
  const parts::DynamicState state = parts::DynamicStateOf(key);
  d.raster.cullMode = state.cull_mode;
  d.raster.frontFace = state.front_face;
  d.depth_stencil.depthTestEnable = state.depth_test;
  d.depth_stencil.depthWriteEnable = state.depth_write;
  d.depth_stencil.depthCompareOp = state.depth_compare;
  d.depth_stencil.stencilTestEnable = state.stencil_test;
  d.depth_stencil.front = StencilState(state.front);
  d.depth_stencil.back = StencilState(state.back);
}

void FillOutput(Desc& d, const PipelineKey& key) {
  const uint32_t bc = key.blend_control;
  VkPipelineColorBlendAttachmentState& a = d.blend_attachment;
  a.srcColorBlendFactor = BlendFactor(bc & 0x1F);
  a.colorBlendOp = BlendOp((bc >> 5) & 7);
  a.dstColorBlendFactor = BlendFactor((bc >> 8) & 0x1F);
  a.srcAlphaBlendFactor = BlendFactor((bc >> 16) & 0x1F);
  a.alphaBlendOp = BlendOp((bc >> 21) & 7);
  a.dstAlphaBlendFactor = BlendFactor((bc >> 24) & 0x1F);
  // ONE, ZERO, ADD on both is the Xenos way of saying "no blending".
  a.blendEnable = (bc & 0x1FFF1FFF) != 0x00010001 && !REXCVAR_GET(wwe_native_debug_no_blend);
  a.colorWriteMask = key.color_mask & 0xF;
  d.blend.attachmentCount = 1;
  d.blend.pAttachments = &d.blend_attachment;
  d.color_format = key.color_format;
  d.rendering.colorAttachmentCount = 1;
  d.rendering.pColorAttachmentFormats = &d.color_format;
  d.rendering.depthAttachmentFormat = key.depth_format;
  d.rendering.stencilAttachmentFormat = key.depth_format;
}

VkPipeline CreatePipeline(VkGraphicsPipelineCreateInfo& info) {
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (g_vk.dfn->vkCreateGraphicsPipelines(g_vk.vk_device, g.cache, 1, &info, nullptr,
                                          &pipeline) != VK_SUCCESS) {
    if (g.errors_logged++ < 16) {
      REXLOG_ERROR("native renderer: vkCreateGraphicsPipelines failed (flags {:X})",
                   uint32_t(info.flags));
    }
    pipeline = VK_NULL_HANDLE;
  }
  return pipeline;
}

// A whole pipeline: the full-pipeline path (state baked in, both stages
// given the key's whole spec value) or, with dynamic set, the library path's
// monolithic optimized pipeline (each stage with its own spec bits). Any
// thread: pipeline creation with a pipeline cache is thread-safe.
VkPipeline Create(const PipelineKey& key, bool dynamic = false) {
  const bool depth_only = IsDepthOnly(key);
  const VkShaderModule vertex_module = shader_library::Module(key.vertex_shader);
  const VkShaderModule pixel_module = depth_only ? VK_NULL_HANDLE : PixelModule(key.pixel_shader);
  if (!vertex_module || (!pixel_module && !depth_only)) {
    return VK_NULL_HANDLE;
  }
  Desc d;
  const parts::Parts split =
      parts::Split(key, shader_library::SpecConstantsMask(key.vertex_shader),
                   shader_library::SpecConstantsMask(key.pixel_shader));
  FillStage(d, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex_module,
            dynamic ? split.vertex.spec_constants : key.spec_constants);
  FillStage(d, 1, VK_SHADER_STAGE_FRAGMENT_BIT, pixel_module,
            dynamic ? split.pixel.spec_constants : key.spec_constants);
  FillVertexInput(d, key);
  FillRasterAndDepth(d, key, dynamic);
  FillOutput(d, key);

  constexpr size_t kAllDynamic = std::size(kPreRasterDynamic) + std::size(kFragmentDynamic) +
                                 std::size(kVertexInputDynamic) + std::size(kOutputDynamic);
  VkDynamicState all_dynamic[kAllDynamic];
  VkDynamicState* out = all_dynamic;
  out = std::copy(std::begin(kPreRasterDynamic), std::end(kPreRasterDynamic), out);
  out = std::copy(std::begin(kFragmentDynamic), std::end(kFragmentDynamic), out);
  out = std::copy(std::begin(kVertexInputDynamic), std::end(kVertexInputDynamic), out);
  out = std::copy(std::begin(kOutputDynamic), std::end(kOutputDynamic), out);
  VkPipelineDynamicStateCreateInfo dynamic_info =
      dynamic ? VkPipelineDynamicStateCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                 nullptr, 0, uint32_t(kAllDynamic), all_dynamic}
              : DynamicInfo(kStaticPathDynamic);

  VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &d.rendering;
  info.stageCount = depth_only ? 1 : 2;
  info.pStages = d.stages;
  info.pVertexInputState = &d.vertex_input;
  info.pInputAssemblyState = &d.input_assembly;
  info.pViewportState = &d.viewport;
  info.pRasterizationState = &d.raster;
  info.pMultisampleState = &d.multisample;
  info.pDepthStencilState = &d.depth_stencil;
  info.pColorBlendState = &d.blend;
  info.pDynamicState = &dynamic_info;
  info.layout = g_vk.pipeline_layout;
  return CreatePipeline(info);
}

// ---- Library path ----

VkPipeline CreateLibrary(VkGraphicsPipelineLibraryFlagsEXT part,
                         VkGraphicsPipelineCreateInfo& info) {
  VkGraphicsPipelineLibraryCreateInfoEXT library = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT};
  library.pNext = info.pNext;
  library.flags = part;
  info.pNext = &library;
  info.flags |= VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
  if (lib.lto) {
    info.flags |= VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
  }
  return CreatePipeline(info);
}

// Pre-rasterization (vertex shader) or fragment-shader library of one pack
// shader with one spec value; module VK_NULL_HANDLE: a fragment library with
// no shader, for depth-only draws.
VkPipeline CreateShaderLibrary(VkShaderStageFlagBits stage, VkShaderModule module,
                               uint32_t spec) {
  Desc d;
  PipelineKey none;
  std::memset(&none, 0, sizeof(none));
  FillRasterAndDepth(d, none, true);
  VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.layout = g_vk.pipeline_layout;
  if (module) {
    FillStage(d, 0, stage, module, spec);
    info.stageCount = 1;
    info.pStages = d.stages;
  }
  if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
    const VkPipelineDynamicStateCreateInfo dynamic = DynamicInfo(kPreRasterDynamic);
    info.pViewportState = &d.viewport;
    info.pRasterizationState = &d.raster;
    info.pDynamicState = &dynamic;
    return CreateLibrary(VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT, info);
  }
  const VkPipelineDynamicStateCreateInfo dynamic = DynamicInfo(kFragmentDynamic);
  info.pDepthStencilState = &d.depth_stencil;
  info.pMultisampleState = &d.multisample;
  info.pDynamicState = &dynamic;
  return CreateLibrary(VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT, info);
}

VkPipeline BuildVariantLibrary(const prebuild::ShaderVariant& variant) {
  const VkShaderStageFlagBits stage = shader_library::Stage(variant.shader);
  if (stage != VK_SHADER_STAGE_VERTEX_BIT && stage != VK_SHADER_STAGE_FRAGMENT_BIT) {
    return VK_NULL_HANDLE;
  }
  const VkShaderModule module = stage == VK_SHADER_STAGE_FRAGMENT_BIT
                                    ? PixelModule(variant.shader)
                                    : shader_library::Module(variant.shader);
  return module ? CreateShaderLibrary(stage, module, variant.spec_constants) : VK_NULL_HANDLE;
}

VkPipeline BuildShaderLibrary(size_t index) { return BuildVariantLibrary(lib.variants[index]); }

// A variant outside the plan: only shaders converted live have one (the plan
// covers every value of every pack shader), and the converter thread builds
// them before the shader is used; one built on any other thread is counted.
VkPipeline UnplannedLibrary(const prebuild::ShaderVariant& variant) {
  const std::pair<uint64_t, uint32_t> id(variant.shader, variant.spec_constants);
  {
    std::lock_guard lock(lib.parts_mutex);
    if (const auto it = lib.unplanned.find(id); it != lib.unplanned.end()) {
      return it->second;
    }
  }
  const VkPipeline pipeline = BuildVariantLibrary(variant);
  std::lock_guard lock(lib.parts_mutex);
  const auto [it, inserted] = lib.unplanned.emplace(id, pipeline);
  if (!inserted && pipeline) {
    g_vk.dfn->vkDestroyPipeline(g_vk.vk_device, pipeline, nullptr);
  }
  lib.parts_created += inserted && !t_worker;
  return it->second;
}

VkPipeline ShaderLibrary(const prebuild::ShaderVariant& variant) {
  const auto it = lib.index.find({variant.shader, variant.spec_constants});
  if (it == lib.index.end()) {
    return UnplannedLibrary(variant);
  }
  // Built here only if no worker has yet: counted as a render-thread compile.
  return lib.shaders->Get(it->second, BuildShaderLibrary, !t_worker);
}

VkPipeline EmptyFragmentLibrary() {
  std::call_once(lib.empty_fragment_once, [] {
    lib.empty_fragment =
        CreateShaderLibrary(VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE, 0);
  });
  return lib.empty_fragment;
}

// Vertex-input and output libraries hold no shader; they are made on first
// use, by whichever thread needs one.
VkPipeline VertexInputLibrary(const parts::VertexInputPart& part, const PipelineKey& key) {
  {
    std::lock_guard lock(lib.parts_mutex);
    if (const auto it = lib.vertex_input.find(part); it != lib.vertex_input.end()) {
      return it->second;
    }
  }
  Desc d;
  FillVertexInput(d, key);
  const VkPipelineDynamicStateCreateInfo dynamic = DynamicInfo(kVertexInputDynamic);
  VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pVertexInputState = &d.vertex_input;
  info.pInputAssemblyState = &d.input_assembly;
  info.pDynamicState = &dynamic;
  VkPipeline pipeline =
      CreateLibrary(VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT, info);
  std::lock_guard lock(lib.parts_mutex);
  const auto [it, inserted] = lib.vertex_input.emplace(part, pipeline);
  if (!inserted && pipeline) {
    g_vk.dfn->vkDestroyPipeline(g_vk.vk_device, pipeline, nullptr);
  }
  lib.parts_created += inserted && !t_worker;
  return it->second;
}

VkPipeline OutputLibrary(const parts::OutputPart& part, const PipelineKey& key) {
  {
    std::lock_guard lock(lib.parts_mutex);
    if (const auto it = lib.output.find(part); it != lib.output.end()) {
      return it->second;
    }
  }
  Desc d;
  FillRasterAndDepth(d, key, true);
  FillOutput(d, key);
  const VkPipelineDynamicStateCreateInfo dynamic = DynamicInfo(kOutputDynamic);
  VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &d.rendering;
  info.pColorBlendState = &d.blend;
  info.pMultisampleState = &d.multisample;
  info.pDynamicState = &dynamic;
  VkPipeline pipeline =
      CreateLibrary(VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT, info);
  std::lock_guard lock(lib.parts_mutex);
  const auto [it, inserted] = lib.output.emplace(part, pipeline);
  if (!inserted && pipeline) {
    g_vk.dfn->vkDestroyPipeline(g_vk.vk_device, pipeline, nullptr);
  }
  lib.parts_created += inserted && !t_worker;
  return it->second;
}

// Links a link key's four libraries: fast, or link-time optimized (the lto
// route's optimized pipeline).
VkPipeline Link(const PipelineKey& link_key, bool optimize) {
  const parts::Parts split =
      parts::Split(link_key, shader_library::SpecConstantsMask(link_key.vertex_shader),
                   shader_library::SpecConstantsMask(link_key.pixel_shader));
  const VkPipeline libraries[4] = {
      VertexInputLibrary(split.vertex_input, link_key), ShaderLibrary(split.vertex),
      split.pixel.shader ? ShaderLibrary(split.pixel) : EmptyFragmentLibrary(),
      OutputLibrary(split.output, link_key)};
  for (const VkPipeline library : libraries) {
    if (!library) {
      return VK_NULL_HANDLE;
    }
  }
  VkPipelineLibraryCreateInfoKHR library_info = {VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
  library_info.libraryCount = uint32_t(std::size(libraries));
  library_info.pLibraries = libraries;
  VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &library_info;
  info.layout = g_vk.pipeline_layout;
  if (optimize) {
    info.flags = VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT;
  }
  return CreatePipeline(info);
}

VkPipeline BuildOptimized(const PipelineKey& link_key) {
  return lib.lto ? Link(link_key, true) : Create(link_key, true);
}

// ---- Workers ----

// A worker's priority for one build: lowest for pipelines nothing waits for,
// highest for one the render thread waits for.
class ScopedPriority {
 public:
  enum Level { kLow, kHigh };
  explicit ScopedPriority(Level level) {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(),
                      level == kLow ? THREAD_PRIORITY_LOWEST : THREAD_PRIORITY_HIGHEST);
#endif
  }
  ~ScopedPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif
  }
};

// Stores a created full pipeline unless another thread stored one first.
VkPipeline Insert(const PipelineKey& key, VkPipeline pipeline) {
  std::lock_guard lock(g.mutex);
  const auto [it, inserted] = g.pipelines.emplace(key, pipeline);
  g.pipeline_built.notify_all();
  if (!inserted) {
    if (pipeline) {
      g_vk.dfn->vkDestroyPipeline(g_vk.vk_device, pipeline, nullptr);
    }
    return it->second;
  }
  if (pipeline) {
    g.created.push_back(key);
  }
  return pipeline;
}

// A full pipeline the render thread waits for, if one is asked for: built
// before anything else, between the startup builds too. True if it built one.
bool ServeUrgent() {
  PipelineKey key;
  {
    std::lock_guard lock(g.mutex);
    if (g.urgent.empty()) {
      return false;
    }
    key = g.urgent.front();
    g.urgent.pop_front();
  }
  const ScopedPriority high(ScopedPriority::kHigh);
  Insert(key, Create(key));
  return true;
}

// Every module first, so no pipeline (and no draw) waits on one.
void BuildModules(std::stop_token stop) {
  shader_library::MarkWorkerThread();
  while (!stop.stop_requested() && (ServeUrgent() || shader_library::BuildNextModule())) {
  }
  if (g.module_builders_left.fetch_sub(1) == 1) {
    REXLOG_INFO("native renderer: {} shader modules built on the workers in {:.0f} ms",
                shader_library::ModuleCount(), NsSince(g.modules_start) / 1e6);
    lib.start = std::chrono::steady_clock::now();
  }
}

// Then, on the library path, every shader library, keyed ones first.
void BuildShaderLibraries(std::stop_token stop) {
  if (!lib.enabled) {
    return;
  }
  while (!stop.stop_requested() &&
         (ServeUrgent() || lib.shaders->BuildNext(BuildShaderLibrary))) {
  }
  if (lib.builders_left.fetch_sub(1) == 1) {
    size_t failed = 0;
    for (size_t i = 0; i < lib.variants.size(); ++i) {
      failed += !lib.shaders->Get(i, BuildShaderLibrary);
    }
    REXLOG_INFO("native renderer: {} shader libraries built on the workers in {:.0f} ms ({} "
                "failed)",
                lib.variants.size(), NsSince(lib.start) / 1e6, failed);
  }
}

// Caller holds g.mutex: a queued key is done. Only keys of the startup plan
// count towards it; keys the render thread queued meanwhile do not.
void FinishPrebuildLocked(const PipelineKey& key, bool failed) {
  if (g.prebuild.Finish(key, failed)) {
    REXLOG_INFO("native renderer: planned pipelines prebuilt ({} planned, {} failed)",
                g.prebuild.total(), g.prebuild.failed());
  }
}

void Work(std::stop_token stop) {
  t_worker = true;
  BuildModules(stop);
  BuildShaderLibraries(stop);
  for (;;) {
    PipelineKey key;
    {
      std::unique_lock lock(g.mutex);
      g.work_ready.wait(lock, stop, [] {
        return !g.urgent.empty() || !g.queue.empty() || g.save_requested;
      });
      if (stop.stop_requested()) {
        return;
      }
      if (!g.urgent.empty()) {
        lock.unlock();
        ServeUrgent();
        continue;
      }
      if (g.save_requested) {
        g.save_requested = false;
        lock.unlock();
        Save();
        continue;
      }
      key = g.queue.front();
      g.queue.pop_front();
      if (lib.enabled ? lib.linked.HasOptimized(key) : g.pipelines.count(key) != 0) {
        g.queued.erase(key);
        FinishPrebuildLocked(key, false);
        continue;
      }
    }
    VkPipeline pipeline;
    VkPipeline duplicate = VK_NULL_HANDLE;
    if (lib.enabled) {
      {
        const ScopedPriority low(ScopedPriority::kLow);
        pipeline = BuildOptimized(key);
      }
      std::lock_guard lock(g.mutex);
      // Another thread may have stored one meanwhile: keep that, as Insert does.
      if (pipeline && !lib.linked.HasOptimized(key)) {
        lib.linked.AddOptimized(key, pipeline);
      } else {
        duplicate = pipeline;
      }
    } else {
      pipeline = Insert(key, Create(key));
    }
    if (duplicate) {
      g_vk.dfn->vkDestroyPipeline(g_vk.vk_device, duplicate, nullptr);
    }
    std::lock_guard lock(g.mutex);
    g.queued.erase(key);
    FinishPrebuildLocked(key, !pipeline);
  }
}

// Caller holds g.mutex.
void EnqueueLocked(const PipelineKey& key) {
  if (g.queued.emplace(key, true).second) {
    g.queue.push_back(key);
    g.work_ready.notify_one();
  }
}

// A store or driver cache from another GPU or driver: the launcher runs the
// prepare mode before the next Play.
void MarkStaleIfAsked(pipeline_store::Refusal refusal) {
  if (!pipeline_store::MarksStale(refusal)) {
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(g.directory, error);
  const std::string text = pipeline_store::StaleMarkerText(refusal);
  if (!WriteFile(pipeline_store::StaleMarkerPath(g.directory),
                 std::vector<uint8_t>(text.begin(), text.end()))) {
    REXLOG_WARN("native renderer: could not write the pipelines-stale marker");
  }
}

std::vector<PipelineKey> LoadKeys() {
  const pipeline_store::KeysLoad load = pipeline_store::ReadKeys(
      ReadFile(g.directory / kKeysFile), g.converter_id, g.device);
  if (load.refusal == pipeline_store::Refusal::kMissing) {
    return {};
  }
  if (pipeline_store::MarksStale(load.refusal)) {
    REXLOG_INFO("native renderer: stored pipelines from {}; keeping their {} keys",
                pipeline_store::RefusalName(load.refusal), load.keys.size());
    MarkStaleIfAsked(load.refusal);
  } else if (load.refusal != pipeline_store::Refusal::kNone) {
    REXLOG_INFO("native renderer: stored pipelines not loaded ({}); rebuilding them",
                pipeline_store::RefusalName(load.refusal));
    return {};
  }
  return load.keys;
}

// The key list shipped beside the exe (data/pipeline-keys.txt).
prebuild::KeyListLoad LoadShippedKeys() {
  const std::filesystem::path path = rex::filesystem::GetExecutableFolder() / kShippedKeysFile;
  const std::vector<uint8_t> file = ReadFile(path);
  if (file.empty()) {
    REXLOG_INFO("native renderer: no shipped pipeline key list at {}", path.string());
    return {};
  }
  prebuild::KeyListLoad load =
      prebuild::ReadKeyList(std::string_view(reinterpret_cast<const char*>(file.data()), file.size()));
  if (!load.ok) {
    REXLOG_INFO("native renderer: shipped pipeline key list {} not read (another format)",
                path.string());
  }
  return load;
}

std::vector<uint8_t> LoadDriverCache() {
  pipeline_store::DriverCacheLoad load = pipeline_store::ReadDriverCache(
      ReadFile(g.directory / kCacheFile), g.device, g.device_features);
  if (load.refusal != pipeline_store::Refusal::kNone &&
      load.refusal != pipeline_store::Refusal::kMissing) {
    REXLOG_INFO("native renderer: driver pipeline cache not loaded ({}); starting a new one",
                pipeline_store::RefusalName(load.refusal));
    MarkStaleIfAsked(load.refusal);
  }
  g.driver_cache_bytes = load.data.size();
  return std::move(load.data);
}

pipeline_store::DeviceTag ThisDevice() {
  VkPhysicalDeviceProperties properties = {};
  g_vk.device->vulkan_instance()->functions().vkGetPhysicalDeviceProperties(
      g_vk.device->physical_device(), &properties);
  pipeline_store::DeviceTag tag = {};
  tag.vendor_id = properties.vendorID;
  tag.device_id = properties.deviceID;
  tag.driver_version = properties.driverVersion;
  std::memcpy(tag.cache_uuid, properties.pipelineCacheUUID, sizeof(tag.cache_uuid));
  return tag;
}

// Whether to take the library path, logged with the reason when not.
bool ChooseLibraryPath() {
  const auto& p = g_vk.device->properties();
  const char* off = nullptr;
  if (!REXCVAR_GET(wwe_native_pipeline_libraries)) {
    off = "turned off by wwe_native_pipeline_libraries";
  } else if (!p.graphicsPipelineLibrary) {
    off = "the device has no graphics pipeline libraries";
  } else if (!p.graphicsPipelineLibraryFastLinking) {
    off = "the device cannot fast-link";
  } else if (!p.extendedDynamicState) {
    off = "the device has no extended dynamic state";
  }
  if (off) {
    REXLOG_INFO("native renderer: pipeline libraries off ({}); full pipelines", off);
    return false;
  }
  lib.lto = REXCVAR_GET(wwe_native_pipeline_optimize) == "lto";
  REXLOG_INFO("native renderer: pipeline libraries on (fast link on a miss, optimized "
              "pipelines by {})",
              lib.lto ? "link-time-optimized link" : "monolithic compile");
  return true;
}

}  // namespace

void Initialize(uint64_t converter_id, const std::filesystem::path& cache_root,
                ColourMode colour) {
  g.converter_id = converter_id;
  g.colour = colour;
  g.directory = pipeline_store::Directory(REXCVAR_GET(wwe_native_pipeline_dir), cache_root);
  g.device = ThisDevice();
  g.device_features = g_vk.device->properties().graphicsPipelineLibrary
                          ? pipeline_store::kFeatureGraphicsPipelineLibrary
                          : 0;
  lib.enabled = ChooseLibraryPath();
  // A few workers during play, every core in a prepare (thread_budget.h).
  const uint32_t cores = std::thread::hardware_concurrency();
  const uint32_t workers = (PrepareMode() ? thread_budget::ForPrepare(cores)
                                          : thread_budget::ForPlay(cores)).pipelines;
  REXLOG_INFO("native renderer: {} pipeline workers ({} logical CPUs)", workers, cores);

  // The shipped list first, then the player's own keys; the plan drops keys
  // whose shaders left the pack (UsableKeys' rule) and duplicates.
  prebuild::KeyListLoad shipped;
  std::vector<PipelineKey> stored;
  bool cache_ok = false;
  if (REXCVAR_GET(wwe_native_pipeline_cache)) {
    REXLOG_INFO("native renderer: pipeline store in {}", g.directory.string());
    const std::vector<uint8_t> initial = LoadDriverCache();
    VkPipelineCacheCreateInfo cache_info = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cache_info.initialDataSize = initial.size();
    cache_info.pInitialData = initial.empty() ? nullptr : initial.data();
    if (g_vk.vkCreatePipelineCache(g_vk.vk_device, &cache_info, nullptr, &g.cache) !=
        VK_SUCCESS) {
      cache_info.initialDataSize = 0;
      cache_info.pInitialData = nullptr;
      if (g_vk.vkCreatePipelineCache(g_vk.vk_device, &cache_info, nullptr, &g.cache) !=
          VK_SUCCESS) {
        g.cache = VK_NULL_HANDLE;
      }
    }
    cache_ok = g.cache != VK_NULL_HANDLE;
    if (cache_ok) {
      shipped = LoadShippedKeys();
      stored = LoadKeys();
    }
  }
  const prebuild::KeySource sources[] = {{shipped.converter_id, shipped.keys},
                                         {converter_id, stored}};
  const prebuild::Plan plan = prebuild::MakePlan(
      shader_library::Entries(), converter_id, sources, {lib.enabled, lib.enabled}, g.colour);
  if (plan.missing_shader || plan.other_converter) {
    REXLOG_INFO("native renderer: dropped {} pipeline keys whose shaders left the pack and {} "
                "of another shader converter",
                plan.missing_shader, plan.other_converter);
  }
  if (lib.enabled) {
    lib.variants = plan.libraries;
    lib.variants.insert(lib.variants.end(), plan.pack_libraries.begin(),
                        plan.pack_libraries.end());
    for (size_t i = 0; i < lib.variants.size(); ++i) {
      lib.index.emplace(std::make_pair(lib.variants[i].shader, lib.variants[i].spec_constants), i);
    }
    lib.shaders = std::make_unique<ModuleTable<VkPipeline>>(lib.variants.size());
    lib.builders_left = workers;
    REXLOG_INFO("native renderer: building {} shader libraries ({} for known keys)",
                lib.variants.size(), plan.libraries.size());
    // The full keys met so far stay in the store, as on the full path.
    const prebuild::Plan full =
        prebuild::MakePlan(shader_library::Entries(), converter_id, sources, {}, g.colour);
    std::lock_guard lock(g.mutex);
    for (const PipelineKey& key : full.pipelines) {
      if (g.met.emplace(key, true).second) {
        g.created.push_back(key);
      }
    }
  }

  g.modules_start = std::chrono::steady_clock::now();
  g.module_builders_left = workers;
  for (uint32_t i = 0; i < workers; ++i) {
    g.workers.emplace_back(Work);
  }
  if (plan.pipelines.empty()) {
    return;
  }
  REXLOG_INFO("native renderer: prebuilding {} {}pipelines ({} shipped, {} stored keys) on {} "
              "threads",
              plan.pipelines.size(), lib.enabled ? "optimized " : "", shipped.keys.size(),
              stored.size(), workers);
  std::lock_guard lock(g.mutex);
  g.prebuild.Start(plan.pipelines);
  for (const PipelineKey& key : plan.pipelines) {
    EnqueueLocked(key);
  }
}

VkPipeline Get(const PipelineKey& key, bool wait, bool& pending, bool& dynamic) {
  pending = false;
  dynamic = false;
  if (lib.enabled) {
    const PipelineKey link_key = parts::LinkKey(key);
    {
      std::lock_guard lock(g.mutex);
      if (g.met.emplace(key, true).second) {
        g.created.push_back(key);
      }
      if (const parts::LinkedPipelines::Found found = lib.linked.Find(link_key);
          found.pipeline) {
        dynamic = true;
        return found.pipeline;
      }
      // A key whose libraries failed before has a full pipeline.
      if (const auto it = g.pipelines.find(key); it != g.pipelines.end()) {
        return it->second;
      }
    }
    const auto start = std::chrono::steady_clock::now();
    const VkPipeline fast = Link(link_key, false);
    lib.link_ns += NsSince(start);
    if (fast) {
      std::lock_guard lock(g.mutex);
      lib.linked.AddFastLinked(link_key, fast);
      EnqueueLocked(link_key);
      dynamic = true;
      return lib.linked.Find(link_key).pipeline;
    }
    // A library failed: this key takes the full-pipeline path.
  }
  {
    std::unique_lock lock(g.mutex);
    if (const auto it = g.pipelines.find(key); it != g.pipelines.end()) {
      return it->second;
    }
    if (REXCVAR_GET(wwe_native_async_pipelines) && !g.workers.empty() && !g.stopping) {
      if (!wait && !lib.enabled) {
        EnqueueLocked(key);
        pending = true;
        return VK_NULL_HANDLE;
      }
      // Needed now (an off-screen draw, or a key whose libraries failed): a
      // worker builds it before anything else and this thread waits. The
      // wait is a hitch cause, counted (TakeWaitStats).
      if (std::none_of(g.urgent.begin(), g.urgent.end(),
                       [&](const PipelineKey& k) { return BytesEqual()(k, key); })) {
        g.urgent.push_back(key);
        g.work_ready.notify_all();
      }
      const auto start = std::chrono::steady_clock::now();
      g.pipeline_built.wait(lock, [&] { return g.pipelines.count(key) || g.stopping; });
      g.wait_ns += NsSince(start);
      ++g.wait_count;
      if (const auto it = g.pipelines.find(key); it != g.pipelines.end()) {
        return it->second;
      }
    }
  }
  // Compiled here (async compiling off, or shutting down) even if a worker
  // has it queued; Insert keeps one.
  const auto start = std::chrono::steady_clock::now();
  const VkPipeline pipeline = Insert(key, Create(key));
  g.sync_ns += NsSince(start);
  ++g.sync_count;
  return pipeline;
}

void TakeSyncStats(uint64_t& count, uint64_t& ns) {
  count = g.sync_count;
  ns = g.sync_ns;
  g.sync_count = g.sync_ns = 0;
  if (lib.shaders) {
    // Shader libraries the workers had not built yet are compiles too.
    uint64_t library_count, library_ns;
    lib.shaders->TakeCounted(library_count, library_ns);
    count += library_count;
    ns += library_ns;
  }
}

void TakeWaitStats(uint64_t& count, uint64_t& ns) {
  count = g.wait_count;
  ns = g.wait_ns;
  g.wait_count = g.wait_ns = 0;
}

std::string TakeLinkStats() {
  if (!lib.enabled) {
    return "libraries=off";
  }
  parts::LinkCounters counters;
  {
    std::lock_guard lock(g.mutex);
    counters = lib.linked.TakeCounters();
  }
  const std::string fields =
      fmt::format("{} fast_link_ms={:.1f} state_libraries={}", counters.Fields(),
                  lib.link_ns / 1e6, lib.parts_created.exchange(0));
  lib.link_ns = 0;
  return fields;
}

size_t Count() {
  std::lock_guard lock(g.mutex);
  return lib.enabled ? lib.linked.OptimizedCount() : g.pipelines.size();
}

bool PrepareMode() { return REXCVAR_GET(wwe_prepare_pipelines); }

PrebuildStatus Status() {
  PrebuildStatus status;
  status.modules_built = shader_library::ModulesBuilt();
  status.modules_total = shader_library::ModuleCount();
  if (lib.enabled && lib.shaders) {
    status.libraries_built = lib.shaders->Built();
    status.libraries_total = lib.shaders->size();
    if (status.libraries_built >= status.libraries_total) {
      // Every entry is done: Get only reads them.
      for (size_t i = 0; i < lib.variants.size(); ++i) {
        status.libraries_failed += !lib.shaders->Get(i, BuildShaderLibrary);
      }
    }
  }
  std::lock_guard lock(g.mutex);
  status.pipelines_total = g.prebuild.total();
  status.pipelines_done = g.prebuild.done();
  status.pipelines_failed = g.prebuild.failed();
  return status;
}

void ClearStaleMarker() {
  std::error_code error;
  std::filesystem::remove(pipeline_store::StaleMarkerPath(g.directory), error);
}

void Save(bool force) {
  if (!g.cache) {
    return;
  }
  std::lock_guard save_lock(g.save_mutex);
  std::vector<PipelineKey> keys;
  {
    std::lock_guard lock(g.mutex);
    if (g.created.size() == g.saved && !force) {
      return;
    }
    keys = g.created;
    g.saved = keys.size();
  }
  std::error_code error;
  std::filesystem::create_directories(g.directory, error);
  size_t size = 0;
  std::vector<uint8_t> data;
  if (g_vk.vkGetPipelineCacheData(g_vk.vk_device, g.cache, &size, nullptr) == VK_SUCCESS &&
      size) {
    data.resize(size);
    if (g_vk.vkGetPipelineCacheData(g_vk.vk_device, g.cache, &size, data.data()) !=
        VK_SUCCESS) {
      size = 0;
    }
    data.resize(size);
  }
  // An early save, before the driver holds much, keeps the warm cache on disk.
  const bool keep_cache = !pipeline_store::ReplacesDriverCache(g.driver_cache_bytes, data.size());
  if (keep_cache) {
    data.clear();
  }
  const bool ok =
      WriteFile(g.directory / kKeysFile,
                pipeline_store::WriteKeys(g.converter_id, g.device, keys)) &&
      (data.empty() ||
       WriteFile(g.directory / kCacheFile,
                 pipeline_store::WriteDriverCache(g.device, g.device_features, data)));
  if (ok && !data.empty()) {
    g.driver_cache_bytes = data.size();
  }
  const std::string cache_note =
      keep_cache ? fmt::format("driver cache on disk kept, {} KB", g.driver_cache_bytes / 1024)
                 : fmt::format("{} KB driver cache", data.size() / 1024);
  REXLOG_INFO("native renderer: {} {} pipelines ({})", ok ? "saved" : "could not save",
              keys.size(), cache_note);
}

void RequestSave() {
  if (g.workers.empty()) {
    Save();
    return;
  }
  std::lock_guard lock(g.mutex);
  g.save_requested = true;
  g.work_ready.notify_one();
}

size_t PrepareShader(uint64_t hash) {
  if (!lib.enabled) {
    return 0;
  }
  // Counted as a worker: nothing waits for these.
  const bool was_worker = t_worker;
  t_worker = true;
  size_t built = 0;
  const uint32_t mask = shader_library::SpecConstantsMask(hash);
  // Every subset of the mask, as the plan's pack libraries.
  for (uint32_t value = 0;; value = (value - mask) & mask) {
    built += ShaderLibrary({hash, value}) != VK_NULL_HANDLE;
    if (value == mask) {
      break;
    }
  }
  t_worker = was_worker;
  return built;
}

void Shutdown() {
  {
    std::lock_guard lock(g.mutex);
    g.stopping = true;
  }
  g.pipeline_built.notify_all();
  for (std::jthread& worker : g.workers) {
    worker.request_stop();
  }
  g.work_ready.notify_all();
  g.workers.clear();  // joins
  Save();
}

}  // namespace wwe::native::pipelines
