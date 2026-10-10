// The GPU plugin (rexgpu-xenos) defines some player settings itself
// (vblank_follows_display). Its cvars register when the DLL loads, which the
// runtime does only at setup, so code that reads the settings earlier (the
// launcher's schema export, the tests) loads it first.

#pragma once

#include <rex/system/gpu_plugin.h>

namespace wwe::settings {

// Loads the plugin the launcher passes (--gpu_plugin=xenos, startup_paths.cpp)
// and drops the graphics system it creates; the DLL stays loaded. False when
// the plugin is missing next to the exe.
inline bool LoadGpuPluginSettings() { return rex::system::LoadGpuPlugin("xenos") != nullptr; }

}  // namespace wwe::settings
