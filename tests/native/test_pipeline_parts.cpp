#include <catch_amalgamated.hpp>

#include <cstring>
#include <vector>

#include "native/pipeline_parts.h"

using wwe::native::PipelineKey;
using wwe::native::prebuild::ShaderVariant;
using namespace wwe::native::parts;

namespace {

PipelineKey Key() {
  PipelineKey key;
  std::memset(&key, 0, sizeof(key));
  key.vertex_shader = 0x10;
  key.pixel_shader = 0x20;
  key.spec_constants = 0x7;
  key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  key.color_format = VK_FORMAT_B8G8R8A8_UNORM;
  key.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
  key.blend_control = 0x07060706;
  key.color_mask = 0xF;
  key.strides[0] = 32;
  key.strides[2] = 12;
  key.dummy_locations = 0x100;
  key.attribute_count = 1;
  key.attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  return key;
}

bool Same(const PipelineKey& a, const PipelineKey& b) {
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}

// RB_DEPTHCONTROL: stencil on, z on, z write on, LEQUAL (3), front stencil
// ALWAYS (7) / KEEP / REPLACE (2) / KEEP, back faces two-sided as NEVER/ZERO.
constexpr uint32_t kDepthControl = 1u | 1u << 1 | 1u << 2 | 3u << 4 | 7u << 8 | 2u << 14;

}  // namespace

TEST_CASE("keys that differ only in strides, culling or depth/stencil share a link key") {
  PipelineKey a = Key();
  PipelineKey b = Key();
  a.cull = 2;
  a.depth_control = kDepthControl;
  b.cull = 5;
  b.strides[0] = 48;
  b.strides[2] = 16;
  CHECK(Same(LinkKey(a), LinkKey(b)));
}

TEST_CASE("a link key keeps the shaders, formats, blend, write mask and which streams exist") {
  const PipelineKey base = Key();
  PipelineKey other = base;
  SECTION("pixel shader") { other.pixel_shader = 0x21; }
  SECTION("spec constants") { other.spec_constants = 0x1; }
  SECTION("colour format") { other.color_format = VK_FORMAT_R16G16B16A16_SFLOAT; }
  SECTION("depth format") { other.depth_format = VK_FORMAT_UNDEFINED; }
  SECTION("blend") { other.blend_control = 0x00010001; }
  SECTION("write mask") { other.color_mask = 0x7; }
  SECTION("topology") { other.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; }
  SECTION("a stream more") { other.strides[1] = 8; }
  SECTION("attribute") { other.attributes[0].offset = 4; }
  CHECK_FALSE(Same(LinkKey(base), LinkKey(other)));
}

TEST_CASE("a key splits into its four libraries, each shader with only its own spec bits") {
  const Parts parts = Split(Key(), /*vertex_mask=*/0x5, /*pixel_mask=*/0x2);
  CHECK(parts.vertex == ShaderVariant{0x10, 0x5});
  CHECK(parts.pixel == ShaderVariant{0x20, 0x2});
  CHECK(parts.vertex_input.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  CHECK(parts.vertex_input.stream_mask == 0b101);
  CHECK(parts.vertex_input.dummy_locations == 0x100);
  CHECK(parts.vertex_input.attribute_count == 1);
  CHECK(parts.output.color_format == VK_FORMAT_B8G8R8A8_UNORM);
  CHECK(parts.output.depth_format == VK_FORMAT_D24_UNORM_S8_UINT);
  CHECK(parts.output.blend_control == 0x07060706);
  CHECK(parts.output.color_mask == 0xF);
}

TEST_CASE("a depth-only key splits into a fragment library without a shader") {
  PipelineKey key = Key();
  wwe::native::MakeDepthOnly(key);
  const Parts parts = Split(key, 0x5, 0x2);
  CHECK(parts.pixel == ShaderVariant{0, 0});
  CHECK(parts.output.color_mask == 0);
}

TEST_CASE("dynamic state comes from the key's culling and depth/stencil registers") {
  PipelineKey key = Key();
  key.cull = 2 | 4;  // cull back faces, front face clockwise
  key.depth_control = kDepthControl;
  const DynamicState state = DynamicStateOf(key);
  CHECK(state.cull_mode == VK_CULL_MODE_BACK_BIT);
  CHECK(state.front_face == VK_FRONT_FACE_CLOCKWISE);
  CHECK(state.depth_test);
  CHECK(state.depth_write);
  CHECK(state.depth_compare == VK_COMPARE_OP_LESS_OR_EQUAL);
  CHECK(state.stencil_test);
  CHECK(state.front.compare == VK_COMPARE_OP_ALWAYS);
  CHECK(state.front.pass == VK_STENCIL_OP_REPLACE);
  CHECK(state.front.fail == VK_STENCIL_OP_KEEP);
  // Two-sided stencil is off (bit 7), so back faces use the front ops.
  CHECK(state.back == state.front);
  CHECK(state.strides[0] == 32);
  CHECK(state.strides[2] == 12);
}

TEST_CASE("with two-sided stencil the back faces get their own ops") {
  PipelineKey key = Key();
  key.depth_control = kDepthControl | 1u << 7 | 4u << 20 | 1u << 26;
  const DynamicState state = DynamicStateOf(key);
  CHECK(state.back.compare == VK_COMPARE_OP_GREATER);
  CHECK(state.back.pass == VK_STENCIL_OP_ZERO);
  CHECK(state.front.compare == VK_COMPARE_OP_ALWAYS);
}

TEST_CASE("without a depth target depth and stencil are off") {
  PipelineKey key = Key();
  key.depth_format = VK_FORMAT_UNDEFINED;
  key.depth_control = kDepthControl;
  const DynamicState state = DynamicStateOf(key);
  CHECK_FALSE(state.depth_test);
  CHECK_FALSE(state.depth_write);
  CHECK_FALSE(state.stencil_test);
}

namespace {

VkPipeline Handle(uintptr_t value) { return reinterpret_cast<VkPipeline>(value); }

}  // namespace

TEST_CASE("a fast-linked pipeline serves its draws until the optimized one is swapped in") {
  LinkedPipelines linked;
  const PipelineKey key = LinkKey(Key());
  CHECK(linked.Find(key).pipeline == VK_NULL_HANDLE);
  linked.AddFastLinked(key, Handle(1));
  CHECK(linked.Find(key).pipeline == Handle(1));
  CHECK(linked.Find(key).pipeline == Handle(1));
  CHECK(linked.AddOptimized(key, Handle(2)));
  CHECK(linked.Find(key).pipeline == Handle(2));
  const LinkCounters counters = linked.TakeCounters();
  CHECK(counters.fast_links == 1);
  CHECK(counters.fast_linked_draws == 2);
  CHECK(counters.swapped_in == 1);
  CHECK(counters.optimized == 1);
  CHECK(linked.TakeCounters().fast_linked_draws == 0);
}

TEST_CASE("an optimized pipeline built before any draw needed it is not a swap") {
  LinkedPipelines linked;
  const PipelineKey key = LinkKey(Key());
  CHECK_FALSE(linked.AddOptimized(key, Handle(2)));
  CHECK(linked.Find(key).pipeline == Handle(2));
  CHECK_FALSE(linked.Find(key).fast_linked);
  const LinkCounters counters = linked.TakeCounters();
  CHECK(counters.swapped_in == 0);
  CHECK(counters.fast_linked_draws == 0);
  CHECK(counters.optimized == 1);
}

TEST_CASE("the counters read as one statistics field list") {
  const LinkCounters counters = {3, 40, 2, 5};
  CHECK(counters.Fields() == "fast_links=3 fast_linked_draws=40 swapped_in=2 optimized=5");
}

TEST_CASE("a shader's stage is read from its SPIR-V entry point") {
  // Header (5 words), OpCapability Shader, OpMemoryModel, OpEntryPoint <model> %1 "main".
  const auto module = [](uint32_t execution_model) {
    return std::vector<uint32_t>{0x07230203, 0x00010300, 0, 10, 0,
                                 2u << 16 | 17, 1,
                                 3u << 16 | 14, 0, 1,
                                 5u << 16 | 15, execution_model, 1, 0x6E69616D, 0};
  };
  CHECK(ShaderStageOf(module(0)) == VK_SHADER_STAGE_VERTEX_BIT);
  CHECK(ShaderStageOf(module(4)) == VK_SHADER_STAGE_FRAGMENT_BIT);
  CHECK(ShaderStageOf(module(5)) == VkShaderStageFlagBits(0));  // GLCompute: not ours
  CHECK(ShaderStageOf({}) == VkShaderStageFlagBits(0));
  std::vector<uint32_t> cut = module(0);
  cut.resize(9);  // ends inside OpMemoryModel
  CHECK(ShaderStageOf(cut) == VkShaderStageFlagBits(0));
}
