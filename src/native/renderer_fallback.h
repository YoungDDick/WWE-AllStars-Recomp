// Renderer fallback: a session asked to run on Native that runs on Emulated
// instead. Any renderer fallback fails a scene (ticket 10). The shader pack
// loader (shader_pack.h) decides the shader-related reasons.

#pragma once

#include <cstdint>

namespace wwe::native {

enum class Fallback : uint8_t {
  kNone,
  kMissingShaderPack,        // no shader pack, or one without shaders
  kStaleShaderPack,          // converted with another XenosRecomp patch
  kVulkanSetup,              // Native's Vulkan resources could not be created
  kTruncatedShaderPack,      // the shader pack file ends early
  kBadShaderPackMagic,       // the shader pack file is not a shader pack
  kWrongShaderPackVersion,   // a shader pack of another format version
  kCorruptShaderPack,        // the shader pack's contents do not add up
};

inline const char* FallbackName(Fallback fallback) {
  switch (fallback) {
    case Fallback::kNone: return "none";
    case Fallback::kMissingShaderPack: return "missing_shader_pack";
    case Fallback::kStaleShaderPack: return "stale_shader_pack";
    case Fallback::kVulkanSetup: return "vulkan_setup";
    case Fallback::kTruncatedShaderPack: return "truncated_shader_pack";
    case Fallback::kBadShaderPackMagic: return "bad_shader_pack_magic";
    case Fallback::kWrongShaderPackVersion: return "wrong_shader_pack_version";
    case Fallback::kCorruptShaderPack: return "corrupt_shader_pack";
  }
  return "unknown";
}

}  // namespace wwe::native
