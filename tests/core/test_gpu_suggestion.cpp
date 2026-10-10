// The suggested Graphics preset per GPU (low-end-preset ticket 07).

#include <catch_amalgamated.hpp>

#include <string>

#include "settings/gpu_suggestion.h"
#include "settings/registry.h"

using wwe::settings::GpuFacts;
using wwe::settings::GpuType;
using wwe::settings::SuggestedPreset;

namespace {

constexpr uint64_t kGiB = 1ull << 30;

GpuFacts Gpu(GpuType type, uint64_t heap, uint32_t vendor = 0x10DE, uint32_t device = 0x2786) {
  return {type, heap, vendor, device};
}

}  // namespace

TEST_CASE("a Steam Deck gets Low, LCD and OLED alike, whatever its type and heap say") {
  for (const uint32_t device : {0x163Fu, 0x1435u}) {
    INFO(device);
    CHECK(SuggestedPreset(Gpu(GpuType::kIntegrated, 1 * kGiB, 0x1002, device)) == "low");
    // RADV under Proton may report the shared memory as one big local heap.
    CHECK(SuggestedPreset(Gpu(GpuType::kDiscrete, 16 * kGiB, 0x1002, device)) == "low");
    CHECK(SuggestedPreset(Gpu(GpuType::kOther, 0, 0x1002, device)) == "low");
  }
  // The same device ID from another vendor is not a Deck.
  CHECK(SuggestedPreset(Gpu(GpuType::kDiscrete, 16 * kGiB, 0x10DE, 0x163F)) == "high");
}

TEST_CASE("any other integrated GPU gets Low") {
  CHECK(SuggestedPreset(Gpu(GpuType::kIntegrated, 2 * kGiB, 0x1002, 0x15BF)) == "low");  // 780M
  CHECK(SuggestedPreset(Gpu(GpuType::kIntegrated, 16 * kGiB, 0x8086, 0x7D55)) == "low");
}

TEST_CASE("a discrete GPU under 4 GiB gets Medium; at 4 GiB or more, High") {
  CHECK(SuggestedPreset(Gpu(GpuType::kDiscrete, 2 * kGiB)) == "medium");
  CHECK(SuggestedPreset(Gpu(GpuType::kDiscrete, 4 * kGiB - 1)) == "medium");
  CHECK(SuggestedPreset(Gpu(GpuType::kDiscrete, 4 * kGiB)) == "high");
  CHECK(SuggestedPreset(Gpu(GpuType::kDiscrete, 20 * kGiB, 0x1002, 0x744C)) == "high");  // 7900 XT
}

TEST_CASE("a GPU the rules cannot place gets High, today's defaults") {
  CHECK(SuggestedPreset(Gpu(GpuType::kOther, 0)) == "high");
  CHECK(SuggestedPreset(Gpu(GpuType::kVirtual, 1 * kGiB)) == "high");
  CHECK(SuggestedPreset(Gpu(GpuType::kCpu, 0)) == "high");
  CHECK(SuggestedPreset(GpuFacts{}) == "high");
}

TEST_CASE("every suggestion names a preset of the registry's Graphics preset row") {
  const auto group = wwe::settings::GraphicsPresets();
  for (const std::string suggestion : {"low", "medium", "high"}) {
    bool found = false;
    for (const auto& preset : group.presets) found |= preset.value == suggestion;
    CHECK(found);
  }
}

TEST_CASE("the preset row's help names the suggestion for the GPU in use") {
  const auto settings = wwe::settings::PlayerSettings();
  for (const auto& setting : wwe::settings::WithSuggestedPreset(settings, "medium")) {
    if (!setting.is_group()) continue;
    CHECK(setting.help.ends_with(" Suggested for this GPU: Medium."));
  }
  // No GPU known (no Vulkan device): the help stays as it is.
  for (size_t i = 0; i < settings.size(); ++i) {
    CHECK(wwe::settings::WithSuggestedPreset(settings, "")[i].help == settings[i].help);
  }
}
