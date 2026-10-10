// The files Native keeps its pipelines in, under the cache folder
// (<cache_root>/native-pipelines unless a setting names another folder):
// keys.bin, the pipeline keys met so far, and driver-cache.bin, the driver's
// pipeline cache data. Both carry the GPU and driver that wrote them; a file
// from another vendor, device, driver version or pipeline-cache UUID is
// refused with a named reason. The driver cache is then rebuilt, never loaded;
// the keys do not depend on the device and are kept.

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "native/pipelines.h"

namespace wwe::native::pipeline_store {

// The GPU and driver a file was written with
// (VkPhysicalDeviceProperties: vendorID, deviceID, driverVersion, pipelineCacheUUID).
struct DeviceTag {
  uint32_t vendor_id;
  uint32_t device_id;
  uint32_t driver_version;
  uint8_t cache_uuid[16];
};

enum class Refusal {
  kNone,
  kMissing,         // no file (a first run)
  kNotAStore,       // wrong magic
  kOtherFormat,     // another file version or key size
  kOtherConverter,  // keys for shaders converted by another XenosRecomp patch
  kOtherVendor,
  kOtherDevice,
  kOtherDriver,
  kOtherCacheUuid,
  kOtherFeatures,  // driver cache: written on a device with other optional features
  kTruncated,
};

std::string_view RefusalName(Refusal refusal);

std::vector<uint8_t> WriteKeys(uint64_t converter_id, const DeviceTag& device,
                               std::span<const PipelineKey> keys);

struct KeysLoad {
  Refusal refusal = Refusal::kMissing;
  // The stored keys when refusal is kNone or another device (MarksStale);
  // empty otherwise.
  std::vector<PipelineKey> keys;
};
KeysLoad ReadKeys(std::span<const uint8_t> file, uint64_t converter_id, const DeviceTag& device);

// Device features a driver cache depends on beyond the device tag. A cache
// from a device with VK_EXT_graphics_pipeline_library enabled is never given
// to one without it, or back: AMD lost the device at its first submit when
// that happened through its own driver cache (ticket 04).
constexpr uint32_t kFeatureGraphicsPipelineLibrary = 1;

// driver-cache.bin: the device tag and features, then vkGetPipelineCacheData's
// bytes. The driver checks its own header too, but not the driver version.
std::vector<uint8_t> WriteDriverCache(const DeviceTag& device, uint32_t features,
                                      std::span<const uint8_t> data);

struct DriverCacheLoad {
  Refusal refusal = Refusal::kMissing;
  std::vector<uint8_t> data;  // for VkPipelineCacheCreateInfo; empty unless refusal is kNone
};
DriverCacheLoad ReadDriverCache(std::span<const uint8_t> file, const DeviceTag& device,
                                uint32_t features);

// Whether a save writes the driver's current data (current_bytes) over the
// file: never with nothing, and never over a larger cache this session loaded
// for this device (loaded_bytes; 0 when none was loaded or it was refused). An
// early save, before the driver holds much, so never loses a warm cache.
bool ReplacesDriverCache(uint64_t loaded_bytes, uint64_t current_bytes);

// The "pipelines stale" marker: a session that refuses a store or driver
// cache from another GPU or driver writes it beside them, and the launcher
// then runs the prepare mode before the next Play; a finished prepare removes
// it. A missing or unreadable store does not mark (the launcher sees those).
bool MarksStale(Refusal refusal);
std::string StaleMarkerText(Refusal refusal);
std::filesystem::path StaleMarkerPath(const std::filesystem::path& directory);

// The folder of keys.bin and driver-cache.bin: the setting
// (--wwe_native_pipeline_dir) when given, else <cache_root>/native-pipelines.
std::filesystem::path Directory(std::string_view setting, const std::filesystem::path& cache_root);

}  // namespace wwe::native::pipeline_store
