// Shader top-up (launcher ticket 18): six shaders are not on the disc; the
// game compiles them at runtime from HLSL text in the XEX (splash, autosave
// busy icon, depth restore, movie YUV), so the launcher's disc reader cannot
// put them in the shader pack. When the game creates a shader the loaded pack
// does not hold, its container goes to <cache_root>/shader-topup, named as
// --wwe_shader_dump_dir names it; the launcher converts that folder into the
// pack ("Prepare shaders").

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace wwe::native {

class ShaderTopUp {
 public:
  static constexpr char kFolderName[] = "shader-topup";

  // "<ps|vs>_<fnv1a64>.bin".
  static std::string FileName(const uint8_t* container, size_t size, bool is_pixel_shader);

  // cache_root empty: nothing is ever written.
  explicit ShaderTopUp(std::filesystem::path cache_root);

  // Writes the container unless the pack holds it or its file exists; true
  // when it wrote the file. Any thread.
  bool Offer(const uint8_t* container, size_t size, bool is_pixel_shader, bool pack_holds);

 private:
  std::filesystem::path folder_;
  std::mutex mutex_;
};

}  // namespace wwe::native
