// Host Vulkan format for each guest texture format Native can sample, and its
// block shape. Unknown formats are VK_FORMAT_UNDEFINED: a draw sampling one is
// skipped as no_texture (ticket 19). The 16-bit and EXPAND formats follow the
// SDK's Vulkan texture cache (Emulated), so HDR data keeps its range.

#pragma once

#include <cstdint>

#include <rex/graphics/xenos.h>
#include <rex/ui/vulkan/api.h>

namespace wwe::native {

struct FormatInfo {
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t block_size = 1;  // texels per block side
  uint32_t bytes_per_block_log2 = 0;
};

// Host formats whose component order matches the guest one after the endian
// swap (component X in the lowest bits), so the fetch swizzle applies as is.
constexpr FormatInfo GetFormatInfo(rex::graphics::xenos::TextureFormat format) {
  using F = rex::graphics::xenos::TextureFormat;
  switch (format) {
    case F::k_8_8_8_8:
    case F::k_8_8_8_8_A:
    case F::k_8_8_8_8_AS_16_16_16_16:  // same data, sampled at 16-bit precision
      return {VK_FORMAT_R8G8B8A8_UNORM, 1, 2};
    case F::k_DXT1:
      return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 4, 3};
    case F::k_DXT2_3:
      return {VK_FORMAT_BC2_UNORM_BLOCK, 4, 4};
    case F::k_DXT4_5:
      return {VK_FORMAT_BC3_UNORM_BLOCK, 4, 4};
    case F::k_8:
    case F::k_8_A:
    case F::k_8_B:
      return {VK_FORMAT_R8_UNORM, 1, 0};
    case F::k_8_8:
      return {VK_FORMAT_R8G8_UNORM, 1, 1};
    case F::k_5_6_5:
      return {VK_FORMAT_B5G6R5_UNORM_PACK16, 1, 1};
    case F::k_2_10_10_10:
      return {VK_FORMAT_A2B10G10R10_UNORM_PACK32, 1, 2};
    case F::k_16_16:
      return {VK_FORMAT_R16G16_UNORM, 1, 2};
    case F::k_16_16_16_16:
      return {VK_FORMAT_R16G16B16A16_UNORM, 1, 3};
    case F::k_16_16_EXPAND:
    case F::k_16_16_FLOAT:
      return {VK_FORMAT_R16G16_SFLOAT, 1, 2};
    case F::k_16_16_16_16_EXPAND:  // the bloom chain's resolve destination
    case F::k_16_16_16_16_FLOAT:
      return {VK_FORMAT_R16G16B16A16_SFLOAT, 1, 3};
    case F::k_32_FLOAT:
      return {VK_FORMAT_R32_SFLOAT, 1, 2};
    default:
      return {};
  }
}

// Xenos gamma (ticket 39). The Xbox linearises gamma texture fetches when
// sampling and blends k_8_8_8_8_GAMMA render targets in linear, encoding on
// write. Native gets the same from Vulkan's sRGB formats: sRGB views decode on
// sample and encode on write, with blending in linear. sRGB approximates the
// Xenos piecewise-linear curve.

// The sRGB twin of a host format, or VK_FORMAT_UNDEFINED when it has none.
constexpr VkFormat GammaFormat(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
      return VK_FORMAT_R8G8B8A8_SRGB;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
      return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case VK_FORMAT_BC2_UNORM_BLOCK:
      return VK_FORMAT_BC2_SRGB_BLOCK;
    case VK_FORMAT_BC3_UNORM_BLOCK:
      return VK_FORMAT_BC3_SRGB_BLOCK;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

// Texture fetch dword 0: whether the colour components are signed gamma
// (TextureSign::kGamma = 3 in sign_x, sign_y and sign_z, bits 2-7).
constexpr bool FetchSamplesGamma(uint32_t dword0) {
  constexpr uint32_t kGamma = 3;
  return ((dword0 >> 2) & 3) == kGamma && ((dword0 >> 4) & 3) == kGamma &&
         ((dword0 >> 6) & 3) == kGamma;
}

// RB_COLOR_INFO format field: k_8_8_8_8_GAMMA.
constexpr bool ColorTargetIsGamma(uint32_t color_format) { return color_format == 1; }

// The Colour setting (wwe_native_color_mode). Natural uses sRGB for Xenos
// gamma (above). Original uses the Xenos piecewise-linear (PWL) curve, as the
// Xbox 360 and Xenia do: converted shaders PWL-decode gamma fetches after
// filtering and PWL-encode their output to gamma targets, which keep their
// sRGB view so that blending stays in (approximately) linear space
// (docs/research/color-palette-original-vs-enhanced.md, option A).
enum class ColourMode : int32_t { kOriginal = 0, kNatural = 1 };

// XenosRecomp's SPEC_CONSTANT_PWL_GAMMA_OUTPUT: the pixel shader writes
// sRGB-decode(PWL-encode(colour)), so the sRGB view stores the PWL byte.
inline constexpr uint32_t kSpecPwlGammaOutput = 1u << 3;
// XenosRecomp's XR_PWL_FETCH_FLAG, set in a texture descriptor index (shared
// constants): the shader samples the texture as is and PWL-decodes the colour
// components.
inline constexpr uint32_t kPwlFetchFlag = 1u << 31;

struct GammaFetchPath {
  bool srgb_view = false;      // sample through the format's sRGB twin
  bool shader_decode = false;  // kPwlFetchFlag: decode in the shader
};

// How a fetch is decoded; fetch_gamma is FetchSamplesGamma of its dword 0.
constexpr GammaFetchPath GammaFetch(ColourMode mode, bool fetch_gamma, VkFormat format) {
  if (!fetch_gamma) {
    return {};
  }
  if (mode == ColourMode::kOriginal) {
    return {false, true};
  }
  return {GammaFormat(format) != VK_FORMAT_UNDEFINED, false};
}

// The spec-constant bits a draw adds for its colour target.
constexpr uint32_t GammaOutputSpec(ColourMode mode, bool gamma_target) {
  return mode == ColourMode::kOriginal && gamma_target ? kSpecPwlGammaOutput : 0;
}

}  // namespace wwe::native
