// The startup image check (tu2-dlc ticket 03): the recompiled code is TU2 code,
// so it must run on the TU2 image (base v5 + default.xexp, ADR-0022). A missing
// or different patch leaves a different image, and running on it would crash
// somewhere far from the cause. The check hashes .text as the loader leaves it
// and compares it with the hash of analysis/xex/tu2.pe (build.ps1 -Action Image;
// tests/test_image_check.py keeps the two in step).

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace wwe::image_check {

// TU2 .text (the section table of the TU2 image).
inline constexpr uint32_t kTextStart = 0x82410000;
inline constexpr uint32_t kTextSize = 0x10F4018;
// XXH3-64 of those bytes in analysis/xex/tu2.pe.
inline constexpr uint64_t kTextHash = 0x0289638F85BB6980;

// XXH3-64 of a loaded .text.
uint64_t TextHash(std::span<const uint8_t> text);

// What the player is told when the loaded .text does not hash to kTextHash.
inline constexpr std::string_view kMismatchMessage =
    "This build of WWE All Stars needs Title Update 2, but the game files it "
    "loaded are a different version (the update is missing or not the right "
    "one). Add Title Update 2 in WWEAllStarsLauncher, then start the game again.";

}  // namespace wwe::image_check
