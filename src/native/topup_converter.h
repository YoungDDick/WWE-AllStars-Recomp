// Converts shader top-ups live (zero-stutter ticket 22, live_topup.h): one
// background thread runs XenosRecomp on each container the loaded pack lacks,
// saves the one-shader pack beside the container for the next session, and
// hands the shader to the shader library once its pipeline libraries are built.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "native/live_topup.h"

namespace wwe::native::topup_converter {

// After the shader library is set up (Native on). Without XenosRecomp, or
// with --wwe_live_shader_topups=false, nothing is converted live and the
// containers wait for the launcher's shader step as before.
void Configure(const std::filesystem::path& cache_root, uint64_t converter_id);

// The game created a shader the loaded pack lacks. Any thread; returns at once.
void Offer(const uint8_t* container, size_t size, bool is_pixel_shader);

live_topup::Counts Counts();

}  // namespace wwe::native::topup_converter
