// The sampler a texture fetch gets: its anisotropy and the key the native
// sampler cache stores it under. Texture filtering (wwe_native_anisotropy) is
// part of the key, so a change during the session makes the next lookup create
// a sampler with the new anisotropy; the old samplers stay valid for frames
// still in flight (live-settings 03).

#pragma once

#include <algorithm>
#include <cstdint>

#include <rex/graphics/xenos.h>

namespace wwe::native {

struct SamplerChoice {
  float anisotropy = 1.0f;  // 1 = off
  uint64_t key = 0;
};

// texture_filtering follows the emulated path's anisotropic_override: it
// applies to linear, mipmapped textures (-1 = the game's setting, 0 = off,
// 1..5 = 1x..16x); other textures keep the fetch constant's own setting.
// device_max: the device's maxSamplerAnisotropy, 0 without samplerAnisotropy.
inline SamplerChoice ChooseSampler(const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch,
                                   uint32_t min_level, uint32_t max_level,
                                   int32_t texture_filtering, float device_max) {
  using rex::graphics::xenos::TextureFilter;
  SamplerChoice choice;
  if (device_max >= 1.0f) {
    uint32_t aniso = uint32_t(fetch.aniso_filter);
    if (texture_filtering >= 0 && texture_filtering <= 5 && max_level > min_level &&
        fetch.mag_filter == TextureFilter::kLinear && fetch.min_filter == TextureFilter::kLinear &&
        uint32_t(fetch.mip_filter) <= 1) {
      aniso = uint32_t(texture_filtering);
    }
    if (aniso >= 1 && aniso <= 5) {
      choice.anisotropy = std::min(float(1u << (aniso - 1)), device_max);
    }
  }
  const uint32_t fields = uint32_t(fetch.clamp_x) | uint32_t(fetch.clamp_y) << 3 |
                          uint32_t(fetch.clamp_z) << 6 | uint32_t(fetch.mag_filter) << 9 |
                          uint32_t(fetch.min_filter) << 11 | uint32_t(fetch.mip_filter) << 13 |
                          uint32_t(fetch.border_color) << 15 | min_level << 17 | max_level << 21;
  choice.key = uint64_t(fields) | uint64_t(choice.anisotropy) << 32;
  return choice;
}

}  // namespace wwe::native
