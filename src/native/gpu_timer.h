// Native's GPU timer (low-end-preset ticket 01): the bookkeeping of one
// frame's timestamp queries and the per-frame CSV, without Vulkan, so the
// format is tested on its own (tests/native/test_gpu_timer.cpp). The Vulkan
// side (query pools per frame slot, reads after the slot's fence) is in
// native_renderer.cpp.

#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wwe::native::gpu_timer {

// What a span of GPU work was.
enum class Span : uint8_t { kPass, kResolve, kPresent };

inline const char* SpanName(Span span) {
  switch (span) {
    case Span::kPass: return "pass";
    case Span::kResolve: return "resolve";
    case Span::kPresent: return "present";
  }
  return "?";
}

struct SpanQueries {
  Span span = Span::kPass;
  std::string label;  // the pass name (pass_names.h); empty for resolves and the present
  uint32_t begin = 0;
  uint32_t end = 0;
};

// One frame's queries: 0 and 1 are the frame's start and end, spans follow in
// pairs. A frame with more spans than the pool holds keeps the first ones and
// counts the rest, so the frame total stays right.
class FrameQueries {
 public:
  static constexpr uint32_t kFrameBegin = 0, kFrameEnd = 1;

  explicit FrameQueries(uint32_t capacity = 0) : capacity_(capacity) {}

  // Starts a new frame; the frame's start query is kFrameBegin.
  void Reset(uint64_t frame) {
    frame_ = frame;
    cpu_ms_ = 0;
    used_ = capacity_ >= 2 ? 2 : 0;
    time_.clear();
    spans_.clear();
    dropped_ = 0;
    open_ = false;
  }

  // The query to write at the span's start, or none when the pool is full or
  // a span is already open (spans do not nest).
  std::optional<uint32_t> Begin(Span span, std::string label = {}) {
    if (open_ || used_ < 2 || used_ + 2 > capacity_) {
      if (!open_) ++dropped_;
      return std::nullopt;
    }
    spans_.push_back({span, std::move(label), used_, used_ + 1});
    used_ += 2;
    open_ = true;
    return spans_.back().begin;
  }

  // The query to write at the open span's end, or none without one.
  std::optional<uint32_t> End() {
    if (!open_) return std::nullopt;
    open_ = false;
    return spans_.back().end;
  }

  void set_cpu_ms(double ms) { cpu_ms_ = ms; }
  // The local wall-clock time the frame was submitted ("2026-10-04 21:48:01.250"), so the
  // smoothness report can split the CSV at a capture's marks.
  void set_time(std::string time) { time_ = std::move(time); }
  const std::string& time() const { return time_; }
  uint64_t frame() const { return frame_; }
  double cpu_ms() const { return cpu_ms_; }
  uint32_t used() const { return used_; }  // queries to read back, from 0
  uint32_t dropped() const { return dropped_; }
  bool timed() const { return used_ >= 2; }
  const std::vector<SpanQueries>& spans() const { return spans_; }

 private:
  uint32_t capacity_;
  uint64_t frame_ = 0;
  double cpu_ms_ = 0;
  std::string time_;
  uint32_t used_ = 0;
  uint32_t dropped_ = 0;
  bool open_ = false;
  std::vector<SpanQueries> spans_;
};

// Ticks from begin to end of a counter with valid_bits bits, which wraps.
inline uint64_t ElapsedTicks(uint64_t begin, uint64_t end, uint32_t valid_bits) {
  const uint64_t mask = valid_bits >= 64 ? ~0ull : (1ull << valid_bits) - 1;
  return (end - begin) & mask;
}

inline std::string CsvHeader() { return "frame,time,item,label,gpu_ms,cpu_ms\n"; }

namespace detail {
inline std::string Ms(double ms) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.4f", ms);
  return text;
}
}  // namespace detail

// The frame's rows: one "frame" row (its submit time, GPU ms from start to
// end, and the renderer's CPU ms), then one row per span. timestamps holds the frame's
// query results from query 0; empty (a device without timestamps on the
// graphics queue, or results not available) gives the CPU-only frame row.
inline std::string CsvRows(const FrameQueries& frame, std::span<const uint64_t> timestamps,
                           double period_ns, uint32_t valid_bits) {
  const std::string number = std::to_string(frame.frame());
  const bool timed = frame.timed() && timestamps.size() >= frame.used();
  auto ms = [&](uint32_t begin, uint32_t end) {
    return detail::Ms(double(ElapsedTicks(timestamps[begin], timestamps[end], valid_bits)) *
                      period_ns / 1e6);
  };
  std::string rows = number + "," + frame.time() + ",frame,," +
                     (timed ? ms(FrameQueries::kFrameBegin, FrameQueries::kFrameEnd) : "") + "," +
                     detail::Ms(frame.cpu_ms()) + "\n";
  if (!timed) return rows;
  for (const SpanQueries& span : frame.spans()) {
    rows += number + ",," + SpanName(span.span) + "," + span.label + "," +
            ms(span.begin, span.end) + ",\n";
  }
  return rows;
}

}  // namespace wwe::native::gpu_timer
