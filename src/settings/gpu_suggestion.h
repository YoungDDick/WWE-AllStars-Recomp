// The suggested Graphics preset for a GPU (low-end-preset spec, "Suggested
// preset"): a pure function of the facts Vulkan reports, so it is tested
// without a GPU (tests/core/test_gpu_suggestion.cpp). The rules are revisited
// once tester data exists.

#pragma once

#include <cstdint>
#include <string>

namespace wwe::settings {

// VkPhysicalDeviceType, without the Vulkan headers.
enum class GpuType : uint32_t { kOther = 0, kIntegrated = 1, kDiscrete = 2, kVirtual = 3, kCpu = 4 };

struct GpuFacts {
  GpuType type = GpuType::kOther;
  uint64_t largest_local_heap = 0;  // bytes, the biggest DEVICE_LOCAL heap
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
};

inline constexpr uint32_t kAmdVendorId = 0x1002;
// Steam Deck LCD ("VanGogh") and OLED ("Sephiroth"), from the PCI ID database.
// The IDs decide on their own, so the Deck never depends on how RADV or
// Proton report its device type and heaps.
inline constexpr uint32_t kSteamDeckLcd = 0x163F, kSteamDeckOled = 0x1435;
inline constexpr uint64_t kMediumBelowBytes = 4ull << 30;  // a discrete GPU under 4 GiB

// The preset's registry value (settings/registry.h GraphicsPresets): "low",
// "medium" or "high". A GPU the rules cannot place gets "high", today's defaults.
inline std::string SuggestedPreset(const GpuFacts& gpu) {
  if (gpu.vendor_id == kAmdVendorId &&
      (gpu.device_id == kSteamDeckLcd || gpu.device_id == kSteamDeckOled)) {
    return "low";
  }
  if (gpu.type == GpuType::kIntegrated) return "low";
  if (gpu.type == GpuType::kDiscrete && gpu.largest_local_heap < kMediumBelowBytes) return "medium";
  return "high";
}

}  // namespace wwe::settings
