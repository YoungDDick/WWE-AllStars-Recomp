#pragma once

#include <cstdint>
#include <filesystem>

namespace wwe {

// Frames the guest has presented: the D3DDevice_Present hook's count, which
// also numbers --wwe_native_dump_frame dumps: the frame-number clock that
// scenes and scripted input count in.
uint64_t PresentCount();

// Before guest threads run: where shaders the game compiles at runtime go when
// the shader pack lacks them (<cache_root>/shader-topup, native/shader_topup.h).
void ConfigureShaderTopUp(const std::filesystem::path& cache_root);

}  // namespace wwe
