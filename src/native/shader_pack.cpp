#include "native/shader_pack.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <utility>

#include <zstd.h>

namespace wwe::native {

namespace {

constexpr size_t kHeaderSize = 32;
constexpr size_t kEntrySize = 24;
constexpr size_t kFetchSlotSize = 16;

uint32_t Load32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

uint64_t Load64(const uint8_t* p) { return Load32(p) | uint64_t(Load32(p + 4)) << 32; }

}  // namespace

ShaderPackLoad LoadShaderPack(std::span<const uint8_t> bytes, uint64_t expected_converter_id) {
  ShaderPackLoad result;
  const auto fail = [&result](Fallback fallback, std::string detail) {
    result.fallback = fallback;
    result.detail = std::move(detail);
    result.pack = {};
    return result;
  };
  const uint8_t* p = bytes.data();
  if (bytes.size() >= sizeof(kShaderPackMagic) &&
      std::memcmp(p, kShaderPackMagic, sizeof(kShaderPackMagic)) != 0) {
    return fail(Fallback::kBadShaderPackMagic, "the file does not start with \"WWSP\"");
  }
  if (bytes.size() >= 8 && Load32(p + 4) != kShaderPackVersion) {
    return fail(Fallback::kWrongShaderPackVersion,
                std::format("format version {}, this build reads {}", Load32(p + 4),
                            kShaderPackVersion));
  }
  if (bytes.size() < kHeaderSize) {
    return fail(Fallback::kTruncatedShaderPack,
                std::format("{} bytes, shorter than the {}-byte header", bytes.size(),
                            kHeaderSize));
  }
  ShaderPack& pack = result.pack;
  pack.converter_id = Load64(p + 8);
  const uint32_t entry_count = Load32(p + 16);
  const uint32_t slot_count = Load32(p + 20);
  const uint32_t compressed_size = Load32(p + 24);
  const uint32_t spirv_size = Load32(p + 28);
  const uint64_t expected_size = kHeaderSize + uint64_t(entry_count) * kEntrySize +
                                 uint64_t(slot_count) * kFetchSlotSize + compressed_size;
  if (bytes.size() < expected_size) {
    return fail(Fallback::kTruncatedShaderPack,
                std::format("{} bytes, the header describes {}", bytes.size(), expected_size));
  }
  if (bytes.size() > expected_size) {
    return fail(Fallback::kCorruptShaderPack,
                std::format("{} bytes, the header describes {}", bytes.size(), expected_size));
  }
  if (pack.converter_id != expected_converter_id) {
    return fail(Fallback::kStaleShaderPack,
                std::format("converted with XenosRecomp patch {:016x}, this build expects {:016x}",
                            pack.converter_id, expected_converter_id));
  }
  if (!entry_count) {
    return fail(Fallback::kMissingShaderPack, "the pack holds no shaders");
  }
  const uint8_t* entries = p + kHeaderSize;
  const uint8_t* slots = entries + size_t(entry_count) * kEntrySize;
  const uint8_t* compressed = slots + size_t(slot_count) * kFetchSlotSize;

  pack.entries.resize(entry_count);
  for (uint32_t i = 0; i < entry_count; ++i) {
    const uint8_t* e = entries + size_t(i) * kEntrySize;
    ShaderPackEntry& entry = pack.entries[i];
    entry = {Load64(e), Load32(e + 8), Load32(e + 12), Load32(e + 16)};
    if (uint64_t(entry.spirv_offset) + entry.spirv_size > spirv_size) {
      return fail(Fallback::kCorruptShaderPack,
                  std::format("shader {:016X} lies outside the {}-byte SPIR-V", entry.hash,
                              spirv_size));
    }
    if (i && entry.hash <= pack.entries[i - 1].hash) {
      return fail(Fallback::kCorruptShaderPack, "shaders are not sorted by hash");
    }
  }
  pack.fetch_slots.resize(slot_count);
  for (uint32_t i = 0; i < slot_count; ++i) {
    const uint8_t* s = slots + size_t(i) * kFetchSlotSize;
    pack.fetch_slots[i] = {Load64(s), Load32(s + 8)};
    if (i && pack.fetch_slots[i].fnv <= pack.fetch_slots[i - 1].fnv) {
      return fail(Fallback::kCorruptShaderPack, "fetch slots are not sorted by hash");
    }
  }
  pack.spirv.resize(spirv_size);
  const size_t decompressed =
      ZSTD_decompress(pack.spirv.data(), pack.spirv.size(), compressed, compressed_size);
  if (ZSTD_isError(decompressed) || decompressed != spirv_size) {
    return fail(Fallback::kCorruptShaderPack, "the SPIR-V does not decompress");
  }
  return result;
}

ShaderPackLoad LoadShaderPackFile(const std::filesystem::path& path,
                                  uint64_t expected_converter_id) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    ShaderPackLoad result;
    result.fallback = Fallback::kMissingShaderPack;
    result.detail = std::format("no shader pack at {}", path.string());
    return result;
  }
  const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
  ShaderPackLoad result = LoadShaderPack(bytes, expected_converter_id);
  if (result.fallback != Fallback::kNone) {
    result.detail = std::format("{}: {}", path.string(), result.detail);
  }
  return result;
}

ShaderPack MergeShaderPacks(ShaderPack base, std::span<const ShaderPack> extras) {
  const auto by_hash = [](const ShaderPackEntry& a, const ShaderPackEntry& b) {
    return a.hash < b.hash;
  };
  const auto by_fnv = [](const ShaderPackFetchSlots& a, const ShaderPackFetchSlots& b) {
    return a.fnv < b.fnv;
  };
  for (const ShaderPack& extra : extras) {
    for (const ShaderPackEntry& entry : extra.entries) {
      if (std::binary_search(base.entries.begin(), base.entries.end(), entry, by_hash) ||
          uint64_t(entry.spirv_offset) + entry.spirv_size > extra.spirv.size()) {
        continue;
      }
      ShaderPackEntry added = entry;
      added.spirv_offset = uint32_t(base.spirv.size());
      base.spirv.insert(base.spirv.end(), extra.spirv.begin() + entry.spirv_offset,
                        extra.spirv.begin() + entry.spirv_offset + entry.spirv_size);
      base.entries.insert(
          std::upper_bound(base.entries.begin(), base.entries.end(), added, by_hash), added);
    }
    for (const ShaderPackFetchSlots& slots : extra.fetch_slots) {
      const auto at = std::lower_bound(base.fetch_slots.begin(), base.fetch_slots.end(), slots,
                                       by_fnv);
      if (at == base.fetch_slots.end() || at->fnv != slots.fnv) {
        base.fetch_slots.insert(at, slots);
      }
    }
  }
  return base;
}

}  // namespace wwe::native
