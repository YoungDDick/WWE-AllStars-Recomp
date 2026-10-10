// What Native compiles before play: from the shader pack, the pipeline keys
// shipped with the game and stored by earlier sessions, and the GPU's
// features. Pure: it decides nothing about threads or Vulkan handles.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "native/bytes_key.h"
#include "native/pipelines.h"
#include "native/shader_pack.h"
#include "native/texture_formats.h"

namespace wwe::native::prebuild {

struct DeviceFeatures {
  bool pipeline_libraries = false;  // VK_EXT_graphics_pipeline_library
  bool fast_link = false;           // graphicsPipelineLibraryFastLinking
};

// Keys from one place (the shipped list, the player's store), recorded with
// shaders converted by converter_id.
struct KeySource {
  uint64_t converter_id = 0;
  std::span<const PipelineKey> keys;
};

// A shader compiled into its pipeline library with one spec-constant value
// (the key's value masked to the bits the shader reads).
struct ShaderVariant {
  uint64_t shader = 0;
  uint32_t spec_constants = 0;
  bool operator==(const ShaderVariant&) const = default;
};

struct Plan {
  // Library path only. libraries: the variants the keys use, built first;
  // pack_libraries: every other value each pack shader's spec-constant bits
  // can take, so a key never seen still finds its libraries. Both sorted by
  // shader, then value.
  std::vector<ShaderVariant> libraries;
  std::vector<ShaderVariant> pack_libraries;
  // Full pipelines per key; on the library path optimized pipelines per link
  // key (parts::LinkKey), which keys differing only in dynamic state share.
  std::vector<PipelineKey> pipelines;
  size_t missing_shader = 0;   // keys dropped: a shader is not in the pack
  size_t other_converter = 0;  // keys dropped: recorded with another converter
};

// pack is sorted by hash (ShaderPack::entries); converter_id is the pack's.
// colour is the Colour setting this session draws with: a key into a gamma
// target is planned with that mode's PWL output bit, whichever mode recorded
// it, and Natural builds no PWL output libraries.
Plan MakePlan(std::span<const ShaderPackEntry> pack, uint64_t converter_id,
              std::span<const KeySource> sources, const DeviceFeatures& device,
              ColourMode colour);

// The shipped key list (data/pipeline-keys.txt, beside the exe as
// pipeline-keys.txt; scripts/pipeline_keys.py writes it): a version line, the
// key size, the converter id, then one PipelineKey per line in hex.
struct KeyListLoad {
  bool ok = false;
  uint64_t converter_id = 0;
  std::vector<PipelineKey> keys;
};
KeyListLoad ReadKeyList(std::string_view text);

// How far the workers are through the plan's pipelines. Keys queued later
// (by the render thread) are not part of the plan and do not count; each
// planned key counts once. Not thread-safe; the caller locks.
class Progress {
 public:
  void Start(std::span<const PipelineKey> planned);
  // A key is built (or failed); true when it was the plan's last one.
  bool Finish(const PipelineKey& key, bool failed);
  size_t total() const { return total_; }
  size_t done() const { return total_ - left_.size(); }
  size_t failed() const { return failed_; }

 private:
  std::unordered_set<PipelineKey, BytesHash, BytesEqual> left_;
  size_t total_ = 0;
  size_t failed_ = 0;
};

}  // namespace wwe::native::prebuild
