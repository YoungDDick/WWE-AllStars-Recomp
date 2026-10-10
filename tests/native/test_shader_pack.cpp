#include <catch_amalgamated.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "native/shader_pack.h"

using wwe::native::Fallback;
using wwe::native::FallbackName;
using wwe::native::LoadShaderPack;
using wwe::native::LoadShaderPackFile;

namespace {

constexpr uint64_t kConverterId = 0x19bc9795a8b5e348ull;

void Put32(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(value >> (8 * i)));
}

void Put64(std::vector<uint8_t>& out, uint64_t value) {
  for (int i = 0; i < 8; ++i) out.push_back(uint8_t(value >> (8 * i)));
}

// A zstd frame holding data in one raw (stored) block, so the tests need no
// compressor: magic, single-segment descriptor with a 4-byte content size,
// then a last raw block.
std::vector<uint8_t> ZstdStored(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out = {0x28, 0xB5, 0x2F, 0xFD, 0xA0};
  Put32(out, uint32_t(data.size()));
  const uint32_t block_header = 1u | (uint32_t(data.size()) << 3);
  out.push_back(uint8_t(block_header));
  out.push_back(uint8_t(block_header >> 8));
  out.push_back(uint8_t(block_header >> 16));
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

// The pack scripts/shader_pack.py writes, with two shaders and one fetch slot
// entry. Fields are public so a test can break one.
struct PackBuilder {
  char magic[4] = {'W', 'W', 'S', 'P'};
  uint32_t version = 1;
  uint64_t converter_id = kConverterId;
  std::vector<uint8_t> spirv = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  std::vector<uint8_t> Build() const {
    const std::vector<uint8_t> compressed = ZstdStored(spirv);
    std::vector<uint8_t> out(magic, magic + 4);
    Put32(out, version);
    Put64(out, converter_id);
    Put32(out, 2);  // shaders
    Put32(out, 1);  // fetch slot entries
    Put32(out, uint32_t(compressed.size()));
    Put32(out, uint32_t(spirv.size()));
    // hash, spirv offset, spirv size, spec constants mask, reserved
    Put64(out, 0x1111);
    Put32(out, 0);
    Put32(out, 4);
    Put32(out, 2);
    Put32(out, 0);
    Put64(out, 0x2222);
    Put32(out, 4);
    Put32(out, 6);
    Put32(out, 5);
    Put32(out, 0);
    // fnv, mask, reserved
    Put64(out, 0xABCD);
    Put32(out, 0x1F);
    Put32(out, 0);
    out.insert(out.end(), compressed.begin(), compressed.end());
    return out;
  }
};

}  // namespace

TEST_CASE("a valid shader pack loads its shaders, fetch slots and SPIR-V") {
  const auto result = LoadShaderPack(PackBuilder().Build(), kConverterId);
  REQUIRE(result.fallback == Fallback::kNone);
  CHECK(result.pack.converter_id == kConverterId);
  REQUIRE(result.pack.entries.size() == 2);
  CHECK(result.pack.entries[0].hash == 0x1111);
  CHECK(result.pack.entries[0].spirv_offset == 0);
  CHECK(result.pack.entries[0].spirv_size == 4);
  CHECK(result.pack.entries[0].spec_constants_mask == 2);
  CHECK(result.pack.entries[1].hash == 0x2222);
  CHECK(result.pack.entries[1].spirv_offset == 4);
  CHECK(result.pack.entries[1].spirv_size == 6);
  CHECK(result.pack.entries[1].spec_constants_mask == 5);
  REQUIRE(result.pack.fetch_slots.size() == 1);
  CHECK(result.pack.fetch_slots[0].fnv == 0xABCD);
  CHECK(result.pack.fetch_slots[0].mask == 0x1F);
  CHECK(result.pack.spirv == std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
}

TEST_CASE("a truncated shader pack is a renderer fallback") {
  const std::vector<uint8_t> whole = PackBuilder().Build();
  for (const size_t size : {size_t(2), size_t(20), size_t(40), whole.size() - 1}) {
    INFO("pack cut to " << size << " bytes");
    const auto result =
        LoadShaderPack(std::span(whole.data(), size), kConverterId);
    CHECK(result.fallback == Fallback::kTruncatedShaderPack);
    CHECK(std::string(FallbackName(result.fallback)) == "truncated_shader_pack");
    CHECK_FALSE(result.detail.empty());
  }
}

TEST_CASE("a file that is not a shader pack is a renderer fallback") {
  PackBuilder builder;
  builder.magic[0] = 'X';
  const auto result = LoadShaderPack(builder.Build(), kConverterId);
  CHECK(result.fallback == Fallback::kBadShaderPackMagic);
  CHECK(std::string(FallbackName(result.fallback)) == "bad_shader_pack_magic");
  CHECK_FALSE(result.detail.empty());
}

TEST_CASE("a shader pack of another format version is a renderer fallback") {
  PackBuilder builder;
  builder.version = 2;
  const auto result = LoadShaderPack(builder.Build(), kConverterId);
  CHECK(result.fallback == Fallback::kWrongShaderPackVersion);
  CHECK(std::string(FallbackName(result.fallback)) == "wrong_shader_pack_version");
  CHECK_FALSE(result.detail.empty());
}

TEST_CASE("a newer shader pack with a longer layout is still a version fallback") {
  // A later format may change the header, so only the magic and the version
  // are read before the version is checked.
  std::vector<uint8_t> bytes = {'W', 'W', 'S', 'P', 2, 0, 0, 0};
  CHECK(LoadShaderPack(bytes, kConverterId).fallback == Fallback::kWrongShaderPackVersion);
}

TEST_CASE("a shader pack converted with another XenosRecomp patch is stale") {
  PackBuilder builder;
  builder.converter_id = 0x9999;
  const auto result = LoadShaderPack(builder.Build(), kConverterId);
  CHECK(result.fallback == Fallback::kStaleShaderPack);
  CHECK(std::string(FallbackName(result.fallback)) == "stale_shader_pack");
  CHECK_FALSE(result.detail.empty());
}

TEST_CASE("no shader pack file is a missing shader pack") {
  const auto path = std::filesystem::temp_directory_path() / "wwe-test-no-such.shaderpack";
  std::filesystem::remove(path);
  const auto result = LoadShaderPackFile(path, kConverterId);
  CHECK(result.fallback == Fallback::kMissingShaderPack);
  CHECK(result.detail.find("wwe-test-no-such.shaderpack") != std::string::npos);
}

TEST_CASE("a shader pack file on disk loads like one in memory") {
  const auto path = std::filesystem::temp_directory_path() / "wwe-test-valid.shaderpack";
  const std::vector<uint8_t> bytes = PackBuilder().Build();
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  const auto result = LoadShaderPackFile(path, kConverterId);
  std::filesystem::remove(path);
  CHECK(result.fallback == Fallback::kNone);
  CHECK(result.pack.entries.size() == 2);
}

TEST_CASE("a shader pack whose SPIR-V does not decompress is corrupt") {
  std::vector<uint8_t> bytes = PackBuilder().Build();
  bytes[bytes.size() - 12] ^= 0xFF;  // inside the zstd frame header
  const auto result = LoadShaderPack(bytes, kConverterId);
  CHECK(result.fallback == Fallback::kCorruptShaderPack);
  CHECK(std::string(FallbackName(result.fallback)) == "corrupt_shader_pack");
}

TEST_CASE("a shader pack whose shader reaches past the SPIR-V is corrupt") {
  PackBuilder builder;
  builder.spirv.resize(9);  // the second shader ends at byte 10
  CHECK(LoadShaderPack(builder.Build(), kConverterId).fallback ==
        Fallback::kCorruptShaderPack);
}

// Opt-in check of a real pack from scripts/convert-shaders.ps1 (game-derived,
// so never in the repo): WWE_TEST_SHADER_PACK=<path> wweallstars_native_tests
// "[real_pack]".
TEST_CASE("the shader pack convert-shaders.ps1 wrote loads", "[.real_pack]") {
  const char* path = std::getenv("WWE_TEST_SHADER_PACK");
  if (!path) SKIP("WWE_TEST_SHADER_PACK is not set");
  const auto result = LoadShaderPackFile(path, WWE_EXPECTED_SHADER_CONVERTER_ID);
  INFO(result.detail);
  REQUIRE(result.fallback == Fallback::kNone);
  CHECK_FALSE(result.pack.entries.empty());
  CHECK_FALSE(result.pack.fetch_slots.empty());
}
