// --wwe_native_dump_frames: the guest Present counts to dump in one run, on
// Emulated and on Native, each to <dump_dir>/frame-NNNNNN.ppm. The frame
// numbers are the clock of `at <frame>` in input scripts (ticket 12).

#pragma once

#include <charconv>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace wwe::native {

class FrameDumps {
 public:
  // "300,2400, 3000"; false (and no frames) when an entry is not a number.
  bool Parse(std::string_view list) {
    frames_.clear();
    std::set<uint64_t> frames;
    size_t start = 0;
    while (start <= list.size() && !list.empty()) {
      const size_t end = std::min(list.find(',', start), list.size());
      std::string_view item = list.substr(start, end - start);
      while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
      while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
      uint64_t frame = 0;
      const auto [ptr, error] = std::from_chars(item.data(), item.data() + item.size(), frame);
      if (item.empty() || error != std::errc() || ptr != item.data() + item.size()) {
        return false;
      }
      frames.insert(frame);
      start = end + 1;
    }
    frames_ = std::move(frames);
    return true;
  }

  bool Contains(uint64_t frame) const { return frames_.contains(frame); }
  bool Empty() const { return frames_.empty(); }

  static std::string Path(std::string_view dir, uint64_t frame) {
    const bool slash = !dir.empty() && (dir.back() == '/' || dir.back() == '\\');
    return fmt::format("{}{}frame-{:06}.ppm", dir, slash ? "" : "/", frame);
  }

 private:
  std::set<uint64_t> frames_;
};

}  // namespace wwe::native
