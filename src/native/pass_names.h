// Stable names for Native's GPU work, for the GPU timer's CSV and the debug
// labels RenderDoc, RGP and Nsight show (low-end-preset tickets 01 and 04):
// a render-target pass by its host size and format, a resolve by its
// destination, the present copy by the output size. Tested in
// tests/native/test_pass_names.cpp.

#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan_core.h>

namespace wwe::native {

// The short name of a render-target format (guest_decode.h ColorTargetFormat).
inline std::string FormatName(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "RGB10A2";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "RGBA16F";
    case VK_FORMAT_R16G16_SFLOAT: return "RG16F";
    case VK_FORMAT_R32_SFLOAT: return "R32F";
    case VK_FORMAT_R32G32_SFLOAT: return "RG32F";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32S8";
    default: return "format " + std::to_string(uint32_t(format));
  }
}

struct PassDescription {
  uint32_t width = 0;   // host size
  uint32_t height = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  bool gamma = false;   // drawn through the sRGB view (k_8_8_8_8_GAMMA)
  bool depth = false;   // with a depth-stencil target
};

// "pass 1280x720 RGBA8 sRGB +depth"
inline std::string PassName(const PassDescription& pass) {
  return "pass " + std::to_string(pass.width) + "x" + std::to_string(pass.height) + " " +
         FormatName(pass.format) + (pass.gamma ? " sRGB" : "") + (pass.depth ? " +depth" : "");
}

// "resolve 640x360 RGBA8", "resolve depth 1280x720"
inline std::string ResolveName(uint32_t width, uint32_t height, VkFormat format, bool depth) {
  return std::string("resolve ") + (depth ? "depth " : "") + std::to_string(width) + "x" +
         std::to_string(height) + (depth ? "" : " " + FormatName(format));
}

// "present 2560x1440"
inline std::string PresentName(uint32_t width, uint32_t height) {
  return "present " + std::to_string(width) + "x" + std::to_string(height);
}

}  // namespace wwe::native
