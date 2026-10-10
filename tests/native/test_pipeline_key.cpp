#include <catch_amalgamated.hpp>

#include <cstring>

#include "native/pipelines.h"

using wwe::native::IsDepthOnly;
using wwe::native::MakeDepthOnly;
using wwe::native::PipelineKey;

TEST_CASE("a draw with no pixel shader gets a depth-only key with colour writes off") {
  PipelineKey key;
  std::memset(&key, 0, sizeof(key));
  key.vertex_shader = 0x416E3200;
  key.pixel_shader = 0;
  key.color_mask = 0xF;
  key.blend_control = 0x07060706;
  MakeDepthOnly(key);
  CHECK(IsDepthOnly(key));
  CHECK(key.color_mask == 0);
  CHECK(key.blend_control == 0);
  CHECK(key.vertex_shader == 0x416E3200);
}

TEST_CASE("a key with a pixel shader is not depth-only") {
  PipelineKey key;
  std::memset(&key, 0, sizeof(key));
  key.pixel_shader = 0x1234;
  CHECK_FALSE(IsDepthOnly(key));
}

TEST_CASE("stored keys survive added shaders and drop only keys whose shaders are gone") {
  using wwe::native::UsableKeys;
  PipelineKey kept, depth_only, gone_ps, gone_vs;
  for (PipelineKey* key : {&kept, &depth_only, &gone_ps, &gone_vs}) {
    std::memset(key, 0, sizeof(*key));
  }
  kept.vertex_shader = 1;
  kept.pixel_shader = 2;
  depth_only.vertex_shader = 1;  // no pixel shader
  gone_ps.vertex_shader = 1;
  gone_ps.pixel_shader = 9;
  gone_vs.vertex_shader = 8;
  gone_vs.pixel_shader = 2;
  // The library now also holds shader 3, added since the keys were stored.
  const auto has_shader = [](uint64_t hash) { return hash == 1 || hash == 2 || hash == 3; };
  const std::vector<PipelineKey> usable =
      UsableKeys({kept, depth_only, gone_ps, gone_vs}, has_shader);
  REQUIRE(usable.size() == 2);
  CHECK(std::memcmp(&usable[0], &kept, sizeof(kept)) == 0);
  CHECK(std::memcmp(&usable[1], &depth_only, sizeof(depth_only)) == 0);
}
