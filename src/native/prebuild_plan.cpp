#include "native/prebuild_plan.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <utility>

#include "native/pipeline_parts.h"

namespace wwe::native::prebuild {

namespace {

const ShaderPackEntry* FindShader(std::span<const ShaderPackEntry> pack, uint64_t hash) {
  const auto it = std::lower_bound(pack.begin(), pack.end(), hash,
                                   [](const ShaderPackEntry& e, uint64_t h) { return e.hash < h; });
  return it != pack.end() && it->hash == hash ? &*it : nullptr;
}

struct KeyLess {
  bool operator()(const PipelineKey& a, const PipelineKey& b) const {
    return std::memcmp(&a, &b, sizeof(PipelineKey)) < 0;
  }
};

// The key as this session's colour mode draws it (texture_formats.h): only
// the PWL output bit of a pixel shader that reads it depends on the mode.
PipelineKey ForColour(PipelineKey key, const ShaderPackEntry* pixel_shader, ColourMode colour) {
  if (pixel_shader && (pixel_shader->spec_constants_mask & kSpecPwlGammaOutput)) {
    key.spec_constants = (key.spec_constants & ~kSpecPwlGammaOutput) |
                         GammaOutputSpec(colour, key.color_format == VK_FORMAT_R8G8B8A8_SRGB);
  }
  return key;
}

}  // namespace

Plan MakePlan(std::span<const ShaderPackEntry> pack, uint64_t converter_id,
              std::span<const KeySource> sources, const DeviceFeatures& device,
              ColourMode colour) {
  Plan plan;
  std::set<PipelineKey, KeyLess> planned;
  // Fast linking is what makes libraries worth it: without it a link costs
  // about a full compile, so such a GPU takes the full-pipeline path.
  const bool libraries = device.pipeline_libraries && device.fast_link;
  std::set<std::pair<uint64_t, uint32_t>> variants;
  for (const KeySource& source : sources) {
    if (source.converter_id != converter_id) {
      plan.other_converter += source.keys.size();
      continue;
    }
    for (const PipelineKey& recorded : source.keys) {
      const ShaderPackEntry* vertex_shader = FindShader(pack, recorded.vertex_shader);
      const ShaderPackEntry* pixel_shader =
          IsDepthOnly(recorded) ? nullptr : FindShader(pack, recorded.pixel_shader);
      if (!vertex_shader || (!IsDepthOnly(recorded) && !pixel_shader)) {
        ++plan.missing_shader;
        continue;
      }
      const PipelineKey key = ForColour(recorded, pixel_shader, colour);
      const PipelineKey planned_key = libraries ? parts::LinkKey(key) : key;
      if (!planned.insert(planned_key).second) {
        continue;
      }
      plan.pipelines.push_back(planned_key);
      if (libraries) {
        for (const ShaderPackEntry* shader : {vertex_shader, pixel_shader}) {
          if (shader) {
            variants.emplace(shader->hash, key.spec_constants & shader->spec_constants_mask);
          }
        }
      }
    }
  }
  for (const auto& [shader, spec_constants] : variants) {
    plan.libraries.push_back({shader, spec_constants});
  }
  if (libraries) {
    for (const ShaderPackEntry& shader : pack) {
      // Every subset of the mask, in increasing order.
      const uint32_t mask = shader.spec_constants_mask &
                            ~(colour == ColourMode::kNatural ? kSpecPwlGammaOutput : 0u);
      for (uint32_t value = 0;; value = (value - mask) & mask) {
        if (!variants.count({shader.hash, value})) {
          plan.pack_libraries.push_back({shader.hash, value});
        }
        if (value == mask) {
          break;
        }
      }
    }
  }
  return plan;
}

KeyListLoad ReadKeyList(std::string_view text) {
  KeyListLoad load;
  std::vector<std::string_view> lines;
  while (!text.empty()) {
    const size_t end = std::min(text.find('\n'), text.size());
    std::string_view line = text.substr(0, end);
    text.remove_prefix(std::min(end + 1, text.size()));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.remove_suffix(1);
    }
    if (!line.empty() && line.front() != '#') {
      lines.push_back(line);
    }
  }
  constexpr std::string_view kConverter = "converter ";
  if (lines.size() < 3 || lines[0] != "wwe-pipeline-keys 1" ||
      lines[1] != "key-size " + std::to_string(sizeof(PipelineKey)) ||
      !lines[2].starts_with(kConverter)) {
    return load;
  }
  const auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  const std::string_view converter = lines[2].substr(kConverter.size());
  if (converter.empty() || converter.size() > 16) {  // a 64-bit id
    return load;
  }
  for (const char c : converter) {
    if (hex(c) < 0) {
      return load;
    }
    load.converter_id = load.converter_id << 4 | uint64_t(hex(c));
  }
  load.keys.resize(lines.size() - 3);
  for (size_t i = 3; i < lines.size(); ++i) {
    if (lines[i].size() != 2 * sizeof(PipelineKey)) {
      load.keys.clear();
      return load;
    }
    auto* bytes = reinterpret_cast<uint8_t*>(&load.keys[i - 3]);
    for (size_t b = 0; b < sizeof(PipelineKey); ++b) {
      const int high = hex(lines[i][2 * b]);
      const int low = hex(lines[i][2 * b + 1]);
      if (high < 0 || low < 0) {
        load.keys.clear();
        return load;
      }
      bytes[b] = uint8_t(high << 4 | low);
    }
  }
  load.ok = true;
  return load;
}

void Progress::Start(std::span<const PipelineKey> planned) {
  left_.insert(planned.begin(), planned.end());
  total_ = left_.size();
  failed_ = 0;
}

bool Progress::Finish(const PipelineKey& key, bool failed) {
  if (!left_.erase(key)) {
    return false;
  }
  failed_ += failed;
  return left_.empty();
}

}  // namespace wwe::native::prebuild
