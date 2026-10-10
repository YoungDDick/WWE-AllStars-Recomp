#include <catch_amalgamated.hpp>

#include <set>
#include <string>

#include "native/renderer_fallback.h"

using wwe::native::Fallback;
using wwe::native::FallbackName;

TEST_CASE("fallback reasons have distinct names, none for no fallback") {
  CHECK(std::string(FallbackName(Fallback::kNone)) == "none");
  CHECK(std::string(FallbackName(Fallback::kMissingShaderPack)) == "missing_shader_pack");
  CHECK(std::string(FallbackName(Fallback::kStaleShaderPack)) == "stale_shader_pack");
  CHECK(std::string(FallbackName(Fallback::kVulkanSetup)) == "vulkan_setup");
  const std::set<std::string> names = {
      FallbackName(Fallback::kNone),
      FallbackName(Fallback::kMissingShaderPack),
      FallbackName(Fallback::kStaleShaderPack),
      FallbackName(Fallback::kVulkanSetup),
      FallbackName(Fallback::kTruncatedShaderPack),
      FallbackName(Fallback::kBadShaderPackMagic),
      FallbackName(Fallback::kWrongShaderPackVersion),
      FallbackName(Fallback::kCorruptShaderPack),
  };
  CHECK(names.size() == 8);
  CHECK_FALSE(names.contains("unknown"));
}
