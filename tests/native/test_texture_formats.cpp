#include <catch_amalgamated.hpp>

#include "native/texture_formats.h"

using wwe::native::GetFormatInfo;
using F = rex::graphics::xenos::TextureFormat;

TEST_CASE("8888 sampled at 16-bit precision is still 8888 data (ticket 31)") {
  const auto info = GetFormatInfo(F::k_8_8_8_8_AS_16_16_16_16);
  CHECK(info.format == VK_FORMAT_R8G8B8A8_UNORM);
  CHECK(info.bytes_per_block_log2 == 2);
}

TEST_CASE("HDR formats keep their range instead of falling back to 8 bits (ticket 36)") {
  CHECK(GetFormatInfo(F::k_16_16_16_16_EXPAND).format == VK_FORMAT_R16G16B16A16_SFLOAT);
  CHECK(GetFormatInfo(F::k_16_16_16_16_EXPAND).bytes_per_block_log2 == 3);
  CHECK(GetFormatInfo(F::k_16_16_EXPAND).format == VK_FORMAT_R16G16_SFLOAT);
  CHECK(GetFormatInfo(F::k_16_16_EXPAND).bytes_per_block_log2 == 2);
  CHECK(GetFormatInfo(F::k_16_16_16_16).format == VK_FORMAT_R16G16B16A16_UNORM);
  CHECK(GetFormatInfo(F::k_16_16_16_16).bytes_per_block_log2 == 3);
  CHECK(GetFormatInfo(F::k_16_16).format == VK_FORMAT_R16G16_UNORM);
  CHECK(GetFormatInfo(F::k_16_16).bytes_per_block_log2 == 2);
}

TEST_CASE("formats supported before keep their mapping") {
  CHECK(GetFormatInfo(F::k_8_8_8_8).format == VK_FORMAT_R8G8B8A8_UNORM);
  CHECK(GetFormatInfo(F::k_DXT1).block_size == 4);
  CHECK(GetFormatInfo(F::k_16_16_16_16_FLOAT).format == VK_FORMAT_R16G16B16A16_SFLOAT);
}

TEST_CASE("formats Native cannot sample stay undefined") {
  CHECK(GetFormatInfo(F::k_24_8).format == VK_FORMAT_UNDEFINED);
}

TEST_CASE("gamma data reads and writes through an sRGB twin of its format (ticket 39)") {
  using wwe::native::GammaFormat;
  CHECK(GammaFormat(VK_FORMAT_R8G8B8A8_UNORM) == VK_FORMAT_R8G8B8A8_SRGB);
  CHECK(GammaFormat(VK_FORMAT_BC1_RGBA_UNORM_BLOCK) == VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
  CHECK(GammaFormat(VK_FORMAT_BC2_UNORM_BLOCK) == VK_FORMAT_BC2_SRGB_BLOCK);
  CHECK(GammaFormat(VK_FORMAT_BC3_UNORM_BLOCK) == VK_FORMAT_BC3_SRGB_BLOCK);
  CHECK(GammaFormat(VK_FORMAT_R16G16B16A16_SFLOAT) == VK_FORMAT_UNDEFINED);  // no gamma twin
}

TEST_CASE("a fetch samples gamma when its colour components are signed gamma") {
  using wwe::native::FetchSamplesGamma;
  // dword 0: type in bits 0-1, sign_x 2-3, sign_y 4-5, sign_z 6-7, sign_w 8-9.
  const uint32_t gamma_rgb = 2 | 3u << 2 | 3u << 4 | 3u << 6;
  CHECK(FetchSamplesGamma(gamma_rgb));
  CHECK_FALSE(FetchSamplesGamma(2));                  // unsigned
  CHECK_FALSE(FetchSamplesGamma(2 | 1u << 2 | 1u << 4 | 1u << 6));  // signed
}

TEST_CASE("the k_8_8_8_8_GAMMA render-target view is the gamma one") {
  using wwe::native::ColorTargetIsGamma;
  CHECK(ColorTargetIsGamma(1));
  CHECK_FALSE(ColorTargetIsGamma(0));  // k_8_8_8_8
  CHECK_FALSE(ColorTargetIsGamma(7));  // k_16_16_16_16_FLOAT
}

TEST_CASE("Natural decodes gamma fetches through the sRGB view, where the format has one") {
  using wwe::native::ColourMode;
  using wwe::native::GammaFetch;
  const auto rgba8 = GammaFetch(ColourMode::kNatural, true, VK_FORMAT_R8G8B8A8_UNORM);
  CHECK(rgba8.srgb_view);
  CHECK_FALSE(rgba8.shader_decode);
  const auto half = GammaFetch(ColourMode::kNatural, true, VK_FORMAT_R16G16B16A16_SFLOAT);
  CHECK_FALSE(half.srgb_view);
  CHECK_FALSE(half.shader_decode);
}

TEST_CASE("Original decodes every gamma fetch in the shader with the Xenos curve, as Xenia") {
  using wwe::native::ColourMode;
  using wwe::native::GammaFetch;
  for (const VkFormat format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
                                VK_FORMAT_R16G16B16A16_SFLOAT}) {
    const auto path = GammaFetch(ColourMode::kOriginal, true, format);
    CHECK_FALSE(path.srgb_view);  // filtered in encoded space, then decoded
    CHECK(path.shader_decode);
  }
}

TEST_CASE("fetches without the gamma sign are not decoded in either mode") {
  using wwe::native::ColourMode;
  using wwe::native::GammaFetch;
  for (const ColourMode mode : {ColourMode::kOriginal, ColourMode::kNatural}) {
    const auto path = GammaFetch(mode, false, VK_FORMAT_R8G8B8A8_UNORM);
    CHECK_FALSE(path.srgb_view);
    CHECK_FALSE(path.shader_decode);
  }
}

TEST_CASE("only Original's draws into a gamma target encode with the Xenos curve") {
  using wwe::native::ColourMode;
  using wwe::native::GammaOutputSpec;
  CHECK(GammaOutputSpec(ColourMode::kOriginal, true) == 8u);  // SPEC_CONSTANT_PWL_GAMMA_OUTPUT
  CHECK(GammaOutputSpec(ColourMode::kOriginal, false) == 0u);
  CHECK(GammaOutputSpec(ColourMode::kNatural, true) == 0u);
}
