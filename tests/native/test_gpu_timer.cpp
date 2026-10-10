// The GPU timer's query bookkeeping and CSV rows (low-end-preset ticket 01).

#include <catch_amalgamated.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "native/gpu_timer.h"

using wwe::native::gpu_timer::CsvHeader;
using wwe::native::gpu_timer::CsvRows;
using wwe::native::gpu_timer::ElapsedTicks;
using wwe::native::gpu_timer::FrameQueries;
using wwe::native::gpu_timer::Span;

TEST_CASE("a frame's queries: start and end first, then one pair per span") {
  FrameQueries frame(8);
  frame.Reset(42);
  CHECK(frame.Begin(Span::kPass, "1280x720 B8G8R8A8") == 2u);
  CHECK(frame.End() == 3u);
  CHECK(frame.Begin(Span::kResolve) == 4u);
  CHECK(frame.End() == 5u);
  CHECK(frame.used() == 6);
  CHECK(frame.spans().size() == 2);
}

TEST_CASE("spans past the pool's capacity are dropped and counted; the frame stays timed") {
  FrameQueries frame(6);
  frame.Reset(1);
  CHECK(frame.Begin(Span::kPass));
  frame.End();
  CHECK(frame.Begin(Span::kPass));
  frame.End();
  CHECK_FALSE(frame.Begin(Span::kPass));
  CHECK_FALSE(frame.End());
  CHECK(frame.dropped() == 1);
  CHECK(frame.used() == 6);
  CHECK(frame.timed());
  // Reset starts the next frame empty.
  frame.Reset(2);
  CHECK(frame.used() == 2);
  CHECK(frame.dropped() == 0);
}

TEST_CASE("spans do not nest: a second begin before the end is refused") {
  FrameQueries frame(16);
  frame.Reset(1);
  CHECK(frame.Begin(Span::kPass));
  CHECK_FALSE(frame.Begin(Span::kResolve));
  CHECK(frame.End());
  CHECK_FALSE(frame.End());
}

TEST_CASE("elapsed ticks wrap at the counter's valid bits") {
  CHECK(ElapsedTicks(100, 250, 64) == 150);
  // A 36-bit counter that wrapped between the two stamps.
  const uint64_t top = (1ull << 36) - 10;
  CHECK(ElapsedTicks(top, 5, 36) == 15);
  // Bits above the valid ones are ignored.
  CHECK(ElapsedTicks(0xF000000000000000ull | 10, 0xA000000000000000ull | 30, 36) == 20);
}

TEST_CASE("CSV rows: the frame's GPU and CPU ms, then each span, from the timestamps and period") {
  FrameQueries frame(8);
  frame.Reset(7);
  frame.Begin(Span::kPass, "2560x1440 R8G8B8A8");
  frame.End();
  frame.Begin(Span::kPresent);
  frame.End();
  frame.set_cpu_ms(1.25);
  frame.set_time("2026-10-04 21:48:01.250");
  // Ticks of 10 ns: frame 2 ms, pass 1.5 ms, present 0.25 ms.
  const std::vector<uint64_t> stamps = {1000, 201000, 2000, 152000, 160000, 185000};
  CHECK(CsvHeader() == "frame,time,item,label,gpu_ms,cpu_ms\n");
  CHECK(CsvRows(frame, stamps, 10.0, 64) ==
        "7,2026-10-04 21:48:01.250,frame,,2.0000,1.2500\n"
        "7,,pass,2560x1440 R8G8B8A8,1.5000,\n"
        "7,,present,,0.2500,\n");
  // The next frame in the slot starts without the old time.
  frame.Reset(10);
  CHECK(frame.time().empty());
}

TEST_CASE("CSV rows wrap like the counter") {
  FrameQueries frame(2);
  frame.Reset(3);
  const std::vector<uint64_t> stamps = {(1ull << 32) - 100, 900};  // 1000 ticks across the wrap
  CHECK(CsvRows(frame, stamps, 1.0, 32) == "3,,frame,,0.0010,0.0000\n");
}

TEST_CASE("without timestamps a frame logs its CPU ms only") {
  FrameQueries untimed(0);  // a device without timestamps on the graphics queue: no pool
  untimed.Reset(9);
  CHECK_FALSE(untimed.timed());
  CHECK_FALSE(untimed.Begin(Span::kPass));
  untimed.set_cpu_ms(0.5);
  CHECK(CsvRows(untimed, {}, 1.0, 64) == "9,,frame,,,0.5000\n");
  // Results that are missing (fewer than the queries used) are treated the same.
  FrameQueries timed(8);
  timed.Reset(10);
  timed.Begin(Span::kPass);
  timed.End();
  const std::vector<uint64_t> short_stamps = {1, 2};
  CHECK(CsvRows(timed, short_stamps, 1.0, 64) == "10,,frame,,,0.0000\n");
}
