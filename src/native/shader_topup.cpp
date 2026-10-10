#include "native/shader_topup.h"

#include <cstdio>
#include <system_error>
#include <utility>

#include <fmt/format.h>

namespace wwe::native {

std::string ShaderTopUp::FileName(const uint8_t* container, size_t size, bool is_pixel_shader) {
  uint64_t hash = 14695981039346656037ull;  // FNV-1a, as DumpShaderContainer
  for (size_t i = 0; i < size; ++i) {
    hash = (hash ^ container[i]) * 1099511628211ull;
  }
  return fmt::format("{}_{:016x}.bin", is_pixel_shader ? "ps" : "vs", hash);
}

ShaderTopUp::ShaderTopUp(std::filesystem::path cache_root)
    : folder_(cache_root.empty() ? std::filesystem::path() : cache_root / kFolderName) {}

bool ShaderTopUp::Offer(const uint8_t* container, size_t size, bool is_pixel_shader,
                        bool pack_holds) {
  if (pack_holds || folder_.empty()) {
    return false;
  }
  const std::filesystem::path path = folder_ / FileName(container, size, is_pixel_shader);
  std::lock_guard lock(mutex_);
  std::error_code error;
  if (std::filesystem::exists(path, error)) {
    return false;
  }
  std::filesystem::create_directories(folder_, error);
  // Written under a temporary name, so the launcher never reads half a file.
  std::filesystem::path temp = path;
  temp += ".tmp";
  FILE* file = std::fopen(temp.string().c_str(), "wb");
  if (!file) {
    return false;
  }
  const bool written = std::fwrite(container, 1, size, file) == size;
  std::fclose(file);
  if (written) {
    std::filesystem::rename(temp, path, error);
  }
  if (!written || error) {
    std::filesystem::remove(temp, error);
    return false;
  }
  return true;
}

}  // namespace wwe::native
