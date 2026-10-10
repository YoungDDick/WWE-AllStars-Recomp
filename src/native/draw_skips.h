// Draw skips: draws the native renderer drops, counted by named reason with
// the first frame and key (shader hash, texture or buffer address) of each.
// Any draw skip fails a scene, except pipeline_pending on a first run
// (ticket 10). A new skip path gets a new reason here, never a catch-all.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <fmt/format.h>

namespace wwe::native {

enum class Skip : uint8_t {
  kNoShader,              // key: guest vertex | pixel shader objects
  kNoDeclaration,         // key: vertex shader hash
  kUnsupportedPrimitive,  // key: primitive type
  kBadStream,             // key: vertex fetch constant dword 0
  kBadUserVertices,       // key: stride (BeginVertices with an unusable stride)
  kNoIndexBuffer,         // key: 0
  kIndexBufferUpload,     // key: index buffer address
  kIndexRange,            // key: base vertex (indices outside the user vertices)
  kNoRenderTarget,        // key: pixel shader hash
  kNoTexture,             // key: texture base address
  kConstantUpload,        // key: pixel shader hash
  kVertexUpload,          // key: vertex bytes
  kIndexUpload,           // key: index count
  kPipelineFailed,        // key: pixel shader hash
  kPipelinePending,       // key: pixel shader hash; allowed on a first run only
  kDebugSkipPs,           // key: pixel shader hash (--wwe_native_debug_skip_ps)
  kCount
};

class DrawSkips {
 public:
  struct Record {
    uint64_t total = 0;
    uint64_t first_frame = 0;
    uint64_t first_key = 0;
  };

  static const char* Name(Skip reason) {
    static constexpr const char* kNames[] = {
        "no_shader",       "no_declaration",     "unsupported_primitive", "bad_stream",
        "bad_user_vertices", "no_index_buffer",  "index_buffer_upload",   "index_range",
        "no_render_target", "no_texture",        "constant_upload",       "vertex_upload",
        "index_upload",    "pipeline_failed",    "pipeline_pending",      "debug_skip_ps"};
    static_assert(std::size(kNames) == size_t(Skip::kCount));
    return kNames[size_t(reason)];
  }

  void Count(Skip reason, uint64_t frame, uint64_t key) {
    Record& r = records_[size_t(reason)];
    if (!r.total++) {
      r.first_frame = frame;
      r.first_key = key;
    }
    changed_ = true;
  }

  const Record& Get(Skip reason) const { return records_[size_t(reason)]; }

  uint64_t Total(bool include_pending) const {
    uint64_t total = 0;
    for (size_t i = 0; i < records_.size(); ++i) {
      if (include_pending || Skip(i) != Skip::kPipelinePending) total += records_[i].total;
    }
    return total;
  }

  // True once after any Count since the previous call.
  bool TakeChanged() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
  }

  // "no_texture=2 bad_stream=1", or "none".
  std::string Summary() const {
    std::string out;
    for (size_t i = 0; i < records_.size(); ++i) {
      if (records_[i].total) {
        out += fmt::format("{}{}={}", out.empty() ? "" : " ", Name(Skip(i)), records_[i].total);
      }
    }
    return out.empty() ? "none" : out;
  }

  std::string Json() const {
    std::string out;
    for (size_t i = 0; i < records_.size(); ++i) {
      const Record& r = records_[i];
      if (r.total) {
        out += fmt::format(R"({}"{}": {{"total": {}, "first_frame": {}, "first_key": "{:016x}"}})",
                           out.empty() ? "" : ", ", Name(Skip(i)), r.total, r.first_frame,
                           r.first_key);
      }
    }
    return "{" + out + "}";
  }

 private:
  std::array<Record, size_t(Skip::kCount)> records_{};
  bool changed_ = false;
};

}  // namespace wwe::native
