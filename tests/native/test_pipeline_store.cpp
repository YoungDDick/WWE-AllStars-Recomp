#include <catch_amalgamated.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "native/pipeline_store.h"

using wwe::native::PipelineKey;
namespace store = wwe::native::pipeline_store;

namespace {

constexpr uint64_t kConverter = 0xC0FFEE;
constexpr uint32_t kLibraries = store::kFeatureGraphicsPipelineLibrary;

store::DeviceTag OwnersGpu() {
  store::DeviceTag tag = {};
  tag.vendor_id = 0x1002;  // AMD
  tag.device_id = 0x744C;
  tag.driver_version = 0x8000F2;
  for (uint8_t i = 0; i < 16; ++i) {
    tag.cache_uuid[i] = uint8_t(0xA0 + i);
  }
  return tag;
}

PipelineKey Key(uint64_t vertex_shader, uint64_t pixel_shader) {
  PipelineKey key;
  std::memset(&key, 0, sizeof(key));
  key.vertex_shader = vertex_shader;
  key.pixel_shader = pixel_shader;
  return key;
}

}  // namespace

TEST_CASE("a pipeline store written on this device loads its keys") {
  const std::vector<PipelineKey> keys = {Key(1, 2), Key(3, 0)};
  const std::vector<uint8_t> file = store::WriteKeys(kConverter, OwnersGpu(), keys);
  const store::KeysLoad load = store::ReadKeys(file, kConverter, OwnersGpu());
  CHECK(load.refusal == store::Refusal::kNone);
  REQUIRE(load.keys.size() == 2);
  CHECK(std::memcmp(&load.keys[0], &keys[0], sizeof(PipelineKey)) == 0);
  CHECK(std::memcmp(&load.keys[1], &keys[1], sizeof(PipelineKey)) == 0);
}

TEST_CASE("a pipeline store from another GPU or driver names why and keeps its keys") {
  // Keys are device-independent; only the driver cache is bound to the driver.
  const std::vector<PipelineKey> keys = {Key(1, 2), Key(3, 0)};
  store::DeviceTag writer = OwnersGpu();
  store::Refusal expected = store::Refusal::kNone;
  std::string name;
  SECTION("another vendor") {
    writer.vendor_id = 0x10DE;
    expected = store::Refusal::kOtherVendor;
    name = "another GPU vendor";
  }
  SECTION("another device") {
    writer.device_id = 0x73BF;
    expected = store::Refusal::kOtherDevice;
    name = "another GPU";
  }
  SECTION("another driver version") {
    writer.driver_version = 0x8000E0;
    expected = store::Refusal::kOtherDriver;
    name = "another driver version";
  }
  SECTION("another pipeline cache UUID") {
    writer.cache_uuid[7] ^= 0xFF;
    expected = store::Refusal::kOtherCacheUuid;
    name = "another pipeline cache UUID";
  }
  const store::KeysLoad load =
      store::ReadKeys(store::WriteKeys(kConverter, writer, keys), kConverter, OwnersGpu());
  CHECK(load.refusal == expected);
  CHECK(store::RefusalName(load.refusal) == name);
  CHECK(store::MarksStale(load.refusal));
  REQUIRE(load.keys.size() == 2);
  CHECK(std::memcmp(&load.keys[0], &keys[0], sizeof(PipelineKey)) == 0);
  CHECK(std::memcmp(&load.keys[1], &keys[1], sizeof(PipelineKey)) == 0);
}

TEST_CASE("a cut-off pipeline store from another driver keeps no keys") {
  store::DeviceTag writer = OwnersGpu();
  writer.driver_version += 1;
  std::vector<uint8_t> file =
      store::WriteKeys(kConverter, writer, std::vector<PipelineKey>{Key(1, 2), Key(3, 4)});
  file.resize(file.size() - 1);
  const store::KeysLoad load = store::ReadKeys(file, kConverter, OwnersGpu());
  CHECK(load.refusal == store::Refusal::kTruncated);
  CHECK(load.keys.empty());
}

TEST_CASE("a pipeline store for another converter and another driver keeps no keys") {
  store::DeviceTag writer = OwnersGpu();
  writer.driver_version += 1;
  const store::KeysLoad load = store::ReadKeys(
      store::WriteKeys(kConverter + 1, writer, std::vector<PipelineKey>{Key(1, 2)}), kConverter,
      OwnersGpu());
  CHECK(load.refusal == store::Refusal::kOtherConverter);
  CHECK(load.keys.empty());
}

TEST_CASE("a pipeline store for another shader converter is refused") {
  const store::KeysLoad load = store::ReadKeys(
      store::WriteKeys(kConverter + 1, OwnersGpu(), std::vector<PipelineKey>{Key(1, 2)}),
      kConverter, OwnersGpu());
  CHECK(load.refusal == store::Refusal::kOtherConverter);
  CHECK(load.keys.empty());
}

TEST_CASE("no pipeline store is a first run, a cut-off one is refused") {
  CHECK(store::ReadKeys({}, kConverter, OwnersGpu()).refusal == store::Refusal::kMissing);
  std::vector<uint8_t> file =
      store::WriteKeys(kConverter, OwnersGpu(), std::vector<PipelineKey>{Key(1, 2), Key(3, 4)});
  file.resize(file.size() - 1);
  const store::KeysLoad load = store::ReadKeys(file, kConverter, OwnersGpu());
  CHECK(load.refusal == store::Refusal::kTruncated);
  CHECK(load.keys.empty());
}

TEST_CASE("the driver cache loads on the device that wrote it and nowhere else") {
  const std::vector<uint8_t> driver_data = {1, 2, 3, 4, 5};
  const std::vector<uint8_t> file = store::WriteDriverCache(OwnersGpu(), kLibraries, driver_data);
  const store::DriverCacheLoad same = store::ReadDriverCache(file, OwnersGpu(), kLibraries);
  CHECK(same.refusal == store::Refusal::kNone);
  CHECK(same.data == driver_data);

  store::DeviceTag updated = OwnersGpu();
  updated.driver_version += 1;
  const store::DriverCacheLoad other = store::ReadDriverCache(file, updated, kLibraries);
  CHECK(other.refusal == store::Refusal::kOtherDriver);
  CHECK(other.data.empty());

  CHECK(store::ReadDriverCache({}, OwnersGpu(), kLibraries).refusal == store::Refusal::kMissing);
}

TEST_CASE("a driver cache written before the device tag is not loaded") {
  // A bare VkPipelineCache blob from an older build: header version one, then
  // the driver's data.
  std::vector<uint8_t> bare = {32, 0, 0, 0, 1, 0, 0, 0, 0x02, 0x10, 0, 0, 0x4C, 0x74, 0, 0};
  bare.resize(4096, 0xCD);
  const store::DriverCacheLoad load = store::ReadDriverCache(bare, OwnersGpu(), kLibraries);
  CHECK(load.refusal == store::Refusal::kNotAStore);
  CHECK(load.data.empty());
}

TEST_CASE("a driver cache from a device with pipeline libraries is not loaded on one without") {
  // Ticket 04: AMD lost the device when a device without
  // VK_EXT_graphics_pipeline_library was given pipelines compiled with it.
  const std::vector<uint8_t> driver_data = {1, 2, 3};
  const std::vector<uint8_t> with = store::WriteDriverCache(OwnersGpu(), kLibraries, driver_data);
  const store::DriverCacheLoad load = store::ReadDriverCache(with, OwnersGpu(), 0);
  CHECK(load.refusal == store::Refusal::kOtherFeatures);
  CHECK(store::RefusalName(load.refusal) == "another device feature set");
  CHECK(load.data.empty());
  const std::vector<uint8_t> without = store::WriteDriverCache(OwnersGpu(), 0, driver_data);
  CHECK(store::ReadDriverCache(without, OwnersGpu(), kLibraries).refusal ==
        store::Refusal::kOtherFeatures);
  CHECK(store::ReadDriverCache(without, OwnersGpu(), 0).refusal == store::Refusal::kNone);
}

TEST_CASE("a driver cache written before the feature tag is not loaded") {
  // Version 1: magic, version, device tag, then the driver's data.
  std::vector<uint8_t> file = {'W', 'W', 'E', 'D', 1, 0, 0, 0};
  const store::DeviceTag tag = OwnersGpu();
  const auto* bytes = reinterpret_cast<const uint8_t*>(&tag);
  file.insert(file.end(), bytes, bytes + sizeof(tag));
  file.resize(file.size() + 64, 0xCD);
  CHECK(store::ReadDriverCache(file, OwnersGpu(), 0).refusal == store::Refusal::kOtherFormat);
}

TEST_CASE("a store from another GPU or driver marks the pipelines stale") {
  CHECK(store::MarksStale(store::Refusal::kOtherVendor));
  CHECK(store::MarksStale(store::Refusal::kOtherDevice));
  CHECK(store::MarksStale(store::Refusal::kOtherDriver));
  CHECK(store::MarksStale(store::Refusal::kOtherCacheUuid));
}

TEST_CASE("a loaded, missing or unreadable store does not mark the pipelines stale") {
  CHECK_FALSE(store::MarksStale(store::Refusal::kNone));
  CHECK_FALSE(store::MarksStale(store::Refusal::kMissing));
  CHECK_FALSE(store::MarksStale(store::Refusal::kNotAStore));
  CHECK_FALSE(store::MarksStale(store::Refusal::kOtherFormat));
  CHECK_FALSE(store::MarksStale(store::Refusal::kOtherConverter));
  CHECK_FALSE(store::MarksStale(store::Refusal::kTruncated));
  // Another feature set is this machine's own switch (a forced-legacy device),
  // not a new GPU or driver: the cache is rebuilt, nothing to prepare.
  CHECK_FALSE(store::MarksStale(store::Refusal::kOtherFeatures));
}

TEST_CASE("the stale marker names why, beside the store") {
  CHECK(store::StaleMarkerText(store::Refusal::kOtherDriver) ==
        "wwe-pipelines-stale 1\nreason=another driver version\n");
  CHECK(store::StaleMarkerPath("D:/WWEAllStars/cache/native-pipelines") ==
        std::filesystem::path("D:/WWEAllStars/cache/native-pipelines/pipelines-stale"));
}

TEST_CASE("the pipeline store lives in the cache folder unless a setting names another") {
  const std::filesystem::path cache_root = "D:/WWEAllStars/cache";
  CHECK(store::Directory("", cache_root) == cache_root / "native-pipelines");
  CHECK(store::Directory("D:/scene-out/native-pipelines", cache_root) ==
        std::filesystem::path("D:/scene-out/native-pipelines"));
}

TEST_CASE("a save never replaces a warm driver cache with a smaller one") {
  // Nothing on disk (or one that was refused and must go): any data is written.
  CHECK(store::ReplacesDriverCache(0, 40));
  // An early save before the driver has much: the warm 5 MB cache loaded at startup stays.
  CHECK_FALSE(store::ReplacesDriverCache(5 << 20, 32));
  CHECK(store::ReplacesDriverCache(5 << 20, 6 << 20));
  CHECK(store::ReplacesDriverCache(5 << 20, 5 << 20));
  // The driver gave nothing: nothing to write.
  CHECK_FALSE(store::ReplacesDriverCache(0, 0));
}

TEST_CASE("a pipeline store is written as this game's and one of the old format still loads") {
  const std::vector<PipelineKey> keys = {Key(1, 2)};
  std::vector<uint8_t> file = store::WriteKeys(kConverter, OwnersGpu(), keys);
  CHECK(std::string(file.begin(), file.begin() + 4) == "WWEP");
  // Stores written before the magic changed.
  std::memcpy(file.data(), "SVRP", 4);
  const store::KeysLoad load = store::ReadKeys(file, kConverter, OwnersGpu());
  CHECK(load.refusal == store::Refusal::kNone);
  CHECK(load.keys.size() == 1);
  std::memcpy(file.data(), "XXXX", 4);
  CHECK(store::ReadKeys(file, kConverter, OwnersGpu()).refusal == store::Refusal::kNotAStore);
}
