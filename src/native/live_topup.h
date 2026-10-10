// Live shader top-ups (zero-stutter ticket 22): a shader the pack lacks is
// converted in the session that meets it, so its effect (the autosave icon)
// shows after a short delay instead of only in the next session. The pure
// parts: the conversion queue and its state, the one-shader pack built from
// XenosRecomp's output, and where the game finds XenosRecomp. The thread and
// the process live in native/topup_converter.cpp.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "native/shader_pack.h"

namespace wwe::native::live_topup {

// One container waiting for, or in, conversion.
struct Item {
  std::string name;  // the top-up file name, "<ps|vs>_<fnv1a64>.bin"
  std::vector<uint8_t> container;
  bool is_pixel_shader = false;
};

struct Counts {
  size_t queued = 0;      // offered and not yet finished
  size_t converted = 0;   // finished and in use
  size_t failed = 0;      // not converted; left for the launcher's shader step
};

// Each shader is converted at most once a session: a shader offered again,
// being converted, converted or failed is not queued again. Not thread-safe;
// the caller locks.
class Queue {
 public:
  // True when the shader was queued now.
  bool Offer(Item item);
  // The oldest queued shader, now being converted; none when nothing waits.
  std::optional<Item> Next();
  void Finish(const std::string& name, bool converted);
  Counts counts() const;

 private:
  enum class State { kQueued, kConverting, kConverted, kFailed };
  std::unordered_map<std::string, State> state_;
  std::vector<Item> waiting_;
};

// What XenosRecomp writes for a folder of containers: shader_cache.cpp's
// text, and the per-shader HLSL files (name, text) that give each shader's
// fetch slots.
struct ConverterOutput {
  std::string shader_cache;
  std::vector<std::pair<std::string, std::string>> hlsl;
};

struct PackBuild {
  std::vector<uint8_t> bytes;  // a shader pack (native/shader_pack.h); empty on failure
  std::string error;
};

// The shader pack for XenosRecomp's output, as the launcher's ShaderPackFile.Build and
// scripts/shader_pack.py write it.
PackBuild BuildPack(const ConverterOutput& output, uint64_t converter_id);

// The fetch slots an HLSL shader's body reads (scripts/shader_fetch_slots.py);
// all slots when a reference has no define.
uint32_t FetchSlotMask(std::string_view hlsl);

// Where a converted top-up goes: a one-shader pack beside its container in
// <cache_root>/shader-topup, "<ps|vs>_<fnv1a64>.shaderpack". The next session
// merges these into the pack it loads (MergeShaderPacks), and the launcher
// counts a container with one as converted.
std::string ConvertedFileName(std::string_view container_name);

// Writes the pack under a temporary name first, so no reader sees half a file.
bool SaveConverted(const std::filesystem::path& folder, std::string_view container_name,
                   const std::vector<uint8_t>& pack);

// The converted top-ups in folder that this build can use; the names of the
// others (stale, damaged) go to skipped. They are converted again when met.
std::vector<ShaderPack> LoadConverted(const std::filesystem::path& folder,
                                      uint64_t expected_converter_id,
                                      std::vector<std::string>& skipped);

// XenosRecomp and the shader_common.h it compiles with.
struct Converter {
  std::filesystem::path executable;
  std::filesystem::path include;
};

// Where the game finds XenosRecomp, first match wins: named (a cvar, with
// shader_common.h beside it); the release's xenosrecomp/ folder beside game/
// (scripts/package.ps1); the dev build's (dev, set at configure time). Both
// files must exist.
std::optional<Converter> FindConverter(const std::filesystem::path& game_directory,
                                       const std::filesystem::path& named,
                                       const Converter& dev);

}  // namespace wwe::native::live_topup
