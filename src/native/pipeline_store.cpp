#include "native/pipeline_store.h"

#include <cstring>

namespace wwe::native::pipeline_store {

namespace {

struct KeysHeader {
  char magic[4];
  uint32_t version;
  uint32_t key_size;
  uint32_t count;
  uint64_t converter_id;
  DeviceTag device;
  uint32_t padding;
};
constexpr char kKeysMagic[4] = {'W', 'W', 'E', 'P'};
// Stores written before the magic was this game's (it came from SVR2011-Recomp).
constexpr char kOldKeysMagic[4] = {'S', 'V', 'R', 'P'};
constexpr uint32_t kKeysVersion = 3;  // 3: device tag

struct DriverCacheHeader {
  char magic[4];
  uint32_t version;
  DeviceTag device;
  uint32_t features;
};
constexpr char kDriverCacheMagic[4] = {'W', 'W', 'E', 'D'};
constexpr uint32_t kDriverCacheVersion = 2;  // 2: features

Refusal CompareDevice(const DeviceTag& stored, const DeviceTag& device) {
  if (stored.vendor_id != device.vendor_id) {
    return Refusal::kOtherVendor;
  }
  if (stored.device_id != device.device_id) {
    return Refusal::kOtherDevice;
  }
  if (stored.driver_version != device.driver_version) {
    return Refusal::kOtherDriver;
  }
  if (std::memcmp(stored.cache_uuid, device.cache_uuid, sizeof(device.cache_uuid)) != 0) {
    return Refusal::kOtherCacheUuid;
  }
  return Refusal::kNone;
}

}  // namespace

std::string_view RefusalName(Refusal refusal) {
  switch (refusal) {
    case Refusal::kNone: return "none";
    case Refusal::kMissing: return "missing";
    case Refusal::kNotAStore: return "not a store";
    case Refusal::kOtherFormat: return "another format";
    case Refusal::kOtherConverter: return "another shader converter";
    case Refusal::kOtherVendor: return "another GPU vendor";
    case Refusal::kOtherDevice: return "another GPU";
    case Refusal::kOtherDriver: return "another driver version";
    case Refusal::kOtherCacheUuid: return "another pipeline cache UUID";
    case Refusal::kOtherFeatures: return "another device feature set";
    case Refusal::kTruncated: return "truncated";
  }
  return "unknown";
}

std::vector<uint8_t> WriteKeys(uint64_t converter_id, const DeviceTag& device,
                               std::span<const PipelineKey> keys) {
  KeysHeader header = {};
  std::memcpy(header.magic, kKeysMagic, 4);
  header.version = kKeysVersion;
  header.key_size = sizeof(PipelineKey);
  header.count = uint32_t(keys.size());
  header.converter_id = converter_id;
  header.device = device;
  std::vector<uint8_t> file(sizeof(header) + keys.size_bytes());
  std::memcpy(file.data(), &header, sizeof(header));
  if (!keys.empty()) {
    std::memcpy(file.data() + sizeof(header), keys.data(), keys.size_bytes());
  }
  return file;
}

KeysLoad ReadKeys(std::span<const uint8_t> file, uint64_t converter_id, const DeviceTag& device) {
  KeysLoad load;
  KeysHeader header;
  if (file.empty()) {
    return load;
  }
  if (file.size() < sizeof(header)) {
    load.refusal = Refusal::kTruncated;
    return load;
  }
  std::memcpy(&header, file.data(), sizeof(header));
  if (std::memcmp(header.magic, kKeysMagic, 4) != 0 &&
      std::memcmp(header.magic, kOldKeysMagic, 4) != 0) {
    load.refusal = Refusal::kNotAStore;
  } else if (header.version != kKeysVersion || header.key_size != sizeof(PipelineKey)) {
    load.refusal = Refusal::kOtherFormat;
  } else if (header.converter_id != converter_id) {
    load.refusal = Refusal::kOtherConverter;
  } else if (file.size() < sizeof(header) + size_t(header.count) * sizeof(PipelineKey)) {
    load.refusal = Refusal::kTruncated;
  } else {
    // Keys do not depend on the GPU or driver: another device is named (it
    // marks the pipelines stale) but its keys are kept.
    load.refusal = CompareDevice(header.device, device);
    load.keys.resize(header.count);
    std::memcpy(load.keys.data(), file.data() + sizeof(header),
                load.keys.size() * sizeof(PipelineKey));
  }
  return load;
}

std::vector<uint8_t> WriteDriverCache(const DeviceTag& device, uint32_t features,
                                      std::span<const uint8_t> data) {
  DriverCacheHeader header = {};
  std::memcpy(header.magic, kDriverCacheMagic, 4);
  header.version = kDriverCacheVersion;
  header.device = device;
  header.features = features;
  std::vector<uint8_t> file(sizeof(header) + data.size());
  std::memcpy(file.data(), &header, sizeof(header));
  if (!data.empty()) {
    std::memcpy(file.data() + sizeof(header), data.data(), data.size());
  }
  return file;
}

DriverCacheLoad ReadDriverCache(std::span<const uint8_t> file, const DeviceTag& device,
                                uint32_t features) {
  DriverCacheLoad load;
  DriverCacheHeader header = {};
  if (file.empty()) {
    return load;
  }
  // The magic and version first: an older, shorter header is another format.
  constexpr size_t kPrefix = sizeof(header.magic) + sizeof(header.version);
  if (file.size() < kPrefix) {
    load.refusal = Refusal::kTruncated;
    return load;
  }
  std::memcpy(&header, file.data(), kPrefix);
  if (std::memcmp(header.magic, kDriverCacheMagic, 4) != 0) {
    load.refusal = Refusal::kNotAStore;
  } else if (header.version != kDriverCacheVersion) {
    load.refusal = Refusal::kOtherFormat;
  } else if (file.size() < sizeof(header)) {
    load.refusal = Refusal::kTruncated;
  } else {
    std::memcpy(&header, file.data(), sizeof(header));
    load.refusal = CompareDevice(header.device, device);
    if (load.refusal == Refusal::kNone && header.features != features) {
      load.refusal = Refusal::kOtherFeatures;
    }
  }
  if (load.refusal == Refusal::kNone) {
    load.data.assign(file.begin() + sizeof(header), file.end());
  }
  return load;
}

bool ReplacesDriverCache(uint64_t loaded_bytes, uint64_t current_bytes) {
  return current_bytes > 0 && current_bytes >= loaded_bytes;
}

bool MarksStale(Refusal refusal) {
  switch (refusal) {
    case Refusal::kOtherVendor:
    case Refusal::kOtherDevice:
    case Refusal::kOtherDriver:
    case Refusal::kOtherCacheUuid:
      return true;
    default:
      return false;
  }
}

std::string StaleMarkerText(Refusal refusal) {
  return "wwe-pipelines-stale 1\nreason=" + std::string(RefusalName(refusal)) + "\n";
}

std::filesystem::path StaleMarkerPath(const std::filesystem::path& directory) {
  return directory / "pipelines-stale";
}

std::filesystem::path Directory(std::string_view setting, const std::filesystem::path& cache_root) {
  if (!setting.empty()) {
    return std::filesystem::path(setting);
  }
  return cache_root / "native-pipelines";
}

}  // namespace wwe::native::pipeline_store
