#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "native/shader_topup.h"

namespace fs = std::filesystem;
using wwe::native::ShaderTopUp;

namespace {

// A temporary cache folder, removed after the test.
struct TempCache {
  fs::path root = fs::temp_directory_path() / "wwe_test_shader_topup";
  TempCache() { fs::remove_all(root); }
  ~TempCache() { fs::remove_all(root); }
  fs::path TopUp() const { return root / "shader-topup"; }
};

std::vector<uint8_t> Read(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

const std::vector<uint8_t> kContainer = {0x10, 0x2A, 0x11, 0x00, 0, 0, 0, 4, 0, 0, 0, 4,
                                         1,    2,    3,    4};

}  // namespace

TEST_CASE("a top-up file is named like the shader dumps, by the container's FNV-1a") {
  const std::vector<uint8_t> empty;
  CHECK(ShaderTopUp::FileName(empty.data(), 0, true) == "ps_cbf29ce484222325.bin");
  CHECK(ShaderTopUp::FileName(kContainer.data(), kContainer.size(), false).starts_with("vs_"));
}

TEST_CASE("a shader the pack lacks is written once to the top-up folder") {
  TempCache cache;
  ShaderTopUp topup(cache.root);
  const fs::path file =
      cache.TopUp() / ShaderTopUp::FileName(kContainer.data(), kContainer.size(), true);

  CHECK(topup.Offer(kContainer.data(), kContainer.size(), true, /*pack_holds=*/false));
  REQUIRE(fs::exists(file));
  CHECK(Read(file) == kContainer);

  CHECK_FALSE(topup.Offer(kContainer.data(), kContainer.size(), true, false));
  CHECK(std::distance(fs::directory_iterator(cache.TopUp()), fs::directory_iterator()) == 1);
}

TEST_CASE("a file an earlier session wrote is left alone") {
  TempCache cache;
  ShaderTopUp(cache.root).Offer(kContainer.data(), kContainer.size(), false, false);
  CHECK_FALSE(ShaderTopUp(cache.root).Offer(kContainer.data(), kContainer.size(), false, false));
}

TEST_CASE("a shader the pack holds is not written") {
  TempCache cache;
  ShaderTopUp topup(cache.root);
  CHECK_FALSE(topup.Offer(kContainer.data(), kContainer.size(), true, /*pack_holds=*/true));
  CHECK_FALSE(fs::exists(cache.TopUp()));
}

TEST_CASE("without a cache folder nothing is written") {
  ShaderTopUp topup({});
  CHECK_FALSE(topup.Offer(kContainer.data(), kContainer.size(), true, false));
}
