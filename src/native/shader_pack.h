// The shader pack: the game's shaders converted by XenosRecomp, in one file
// under the cache folder (scripts/shader_pack.py writes it). Native loads it
// at startup; a pack that is missing, damaged, of another format version or
// converted with another XenosRecomp patch is a renderer fallback.
//
// Layout, little-endian:
//   header   magic "WWSP", u32 format version, u64 converter id,
//            u32 shader count, u32 fetch slot count,
//            u32 compressed SPIR-V size, u32 decompressed SPIR-V size
//   shaders  u64 container hash (XXH3), u32 SPIR-V offset, u32 SPIR-V size,
//            u32 spec constants mask, u32 reserved; sorted by hash
//   slots    u64 container hash (FNV-1a), u32 fetch slot mask, u32 reserved;
//            sorted by hash
//   SPIR-V   one zstd frame: the shaders' smol-v streams, back to back

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "native/renderer_fallback.h"

namespace wwe::native {

inline constexpr char kShaderPackMagic[4] = {'W', 'W', 'S', 'P'};
inline constexpr uint32_t kShaderPackVersion = 1;
// The pack's name in the cache folder when no path is given.
inline constexpr char kShaderPackFileName[] = "wweallstars.shaderpack";

struct ShaderPackEntry {
  uint64_t hash = 0;  // XXH3-64 of the whole shader container
  uint32_t spirv_offset = 0;  // into ShaderPack::spirv, smol-v encoded
  uint32_t spirv_size = 0;
  uint32_t spec_constants_mask = 0;
};

// The texture fetch slots a shader's body reads, by the FNV-1a hash of its
// container (the name --wwe_shader_dump_dir gives it).
struct ShaderPackFetchSlots {
  uint64_t fnv = 0;
  uint32_t mask = 0;
};

struct ShaderPack {
  uint64_t converter_id = 0;  // XenosRecomp patch the shaders were converted with
  std::vector<ShaderPackEntry> entries;  // sorted by hash
  std::vector<ShaderPackFetchSlots> fetch_slots;  // sorted by fnv
  std::vector<uint8_t> spirv;  // decompressed smol-v streams
};

struct ShaderPackLoad {
  Fallback fallback = Fallback::kNone;  // kNone: pack is usable
  std::string detail;  // what is wrong, for the log
  ShaderPack pack;
};

// Reads a pack from memory; expected_converter_id is the XenosRecomp patch
// this build was configured with.
ShaderPackLoad LoadShaderPack(std::span<const uint8_t> bytes, uint64_t expected_converter_id);

// Reads the pack file at path; a file that does not exist is a missing pack.
ShaderPackLoad LoadShaderPackFile(const std::filesystem::path& path,
                                  uint64_t expected_converter_id);

// The pack with the shaders of extras it does not hold yet (converted shader
// top-ups, native/live_topup.h); for a shader in both, base wins. Keeps
// base's converter id.
ShaderPack MergeShaderPacks(ShaderPack base, std::span<const ShaderPack> extras);

}  // namespace wwe::native
