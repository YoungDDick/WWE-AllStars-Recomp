// The GPU and language settings, whose choices depend on the player's PC and
// disc (launcher ticket 08).

#include <catch_amalgamated.hpp>

#include <set>
#include <string>
#include <vector>

#include "settings/gpu_list.h"
#include "settings/registry.h"
#include "settings/schema.h"

using wwe::settings::PlayerSettings;
using wwe::settings::Setting;
using wwe::settings::WithRuntimeChoices;

namespace {

const Setting& Find(const std::vector<Setting>& settings, const std::string& name) {
  for (const Setting& setting : settings) {
    if (setting.name == name) return setting;
  }
  FAIL("no setting " << name);
  return settings.front();
}

std::vector<std::string> Labels(const Setting& setting) {
  std::vector<std::string> labels;
  for (const auto& choice : setting.choices) labels.push_back(choice.label);
  return labels;
}

}  // namespace

TEST_CASE("the language offers the languages whose Coalesced files the disc holds") {
  const std::set<std::string> files = {"Game/Localization/Coalesced.int",
                                       "Game/Localization/Coalesced.fra",
                                       "Game/Localization/Coalesced.jpn"};
  const std::vector<Setting> settings = WithRuntimeChoices(
      PlayerSettings(), {}, [&](const std::string& file) { return files.contains(file); });

  CHECK(Labels(Find(settings, "user_language")) == std::vector<std::string>{"English", "French"});
}

TEST_CASE("the language keeps English, the game's fallback, without game data") {
  const std::vector<Setting> settings =
      WithRuntimeChoices(PlayerSettings(), {}, [](const std::string&) { return false; });

  CHECK(Labels(Find(settings, "user_language")) == std::vector<std::string>{"English"});
}

TEST_CASE("the GPU offers Automatic, then each GPU Vulkan reports, by index") {
  const std::vector<Setting> settings =
      WithRuntimeChoices(PlayerSettings(), {"Intel(R) UHD Graphics", "NVIDIA GeForce RTX 3060"},
                         [](const std::string&) { return true; });

  const Setting& gpu = Find(settings, "vulkan_device");
  REQUIRE(gpu.choices.size() == 3);
  CHECK(gpu.choices[0].value == "-1");
  CHECK(gpu.choices[0].label == "Automatic");
  CHECK(gpu.choices[1].value == "0");
  CHECK(gpu.choices[1].label == "Intel(R) UHD Graphics");
  CHECK(gpu.choices[2].value == "1");
  CHECK(gpu.choices[2].label == "NVIDIA GeForce RTX 3060");
}

TEST_CASE("the schema export marks both settings' choices as filled at runtime") {
  const std::string json = wwe::settings::SchemaJson(wwe::settings::WithLifecycles(PlayerSettings()));
  CHECK(json.find("\"key\": \"vulkan_device\"") != std::string::npos);
  CHECK(json.find("\"runtime_choices\": \"gpus\"") != std::string::npos);
  CHECK(json.find("\"runtime_choices\": \"languages\"") != std::string::npos);
  CHECK(json.find("{\"value\": \"3\", \"label\": \"German\", \"file\": "
                  "\"Game/Localization/Coalesced.deu\"}") != std::string::npos);
  // Fixed choices carry no file.
  CHECK(json.find("{\"value\": \"true\", \"label\": \"Native\"}") != std::string::npos);
}

TEST_CASE("--list-gpus prints one line per GPU, by index, with its suggested preset") {
  // A tab never appears in a Vulkan device name, so the field cannot run into the name.
  CHECK(wwe::settings::GpuListText({{"GPU A", "high"}, {"GPU B: integrated", "low"}}) ==
        "gpu 0: GPU A\tsuggested=high\ngpu 1: GPU B: integrated\tsuggested=low\n");
  CHECK(wwe::settings::GpuListText({}).empty());
  // The GPU Automatic opens is marked, so the launcher suggests for the same one.
  CHECK(wwe::settings::GpuListText({{"GPU A", "low"}, {"GPU B", "high", true}}) ==
        "gpu 0: GPU A\tsuggested=low\ngpu 1: GPU B\tsuggested=high\tautomatic\n");
  CHECK(wwe::settings::GpusRequested({"wweallstars.exe", "--list-gpus"}));
  CHECK_FALSE(wwe::settings::GpusRequested({"wweallstars.exe", "--list-gpus-now"}));
}

TEST_CASE("Automatic opens the first GPU with the best feature score, as the SDK orders them") {
  using wwe::settings::AutomaticIndex;
  CHECK(AutomaticIndex({}) == 0);
  CHECK(AutomaticIndex({2}) == 0);
  CHECK(AutomaticIndex({3, 3}) == 0);     // a tie keeps Vulkan's order
  CHECK(AutomaticIndex({2, 4, 4}) == 1);  // the best score, the first of equals
  CHECK(AutomaticIndex({4, 1}) == 0);
}
