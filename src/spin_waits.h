// How the game's busy waits wait on the host (low-end-preset tickets 10
// and 11; evidence in docs/research/spin-loops.md): pure decisions, tested in
// tests/core/test_spin_waits.cpp, used by the appSleep hook
// (game_tick_log.cpp) and the GPU-wait hooks (gpu_wait.cpp).

#pragma once

#include <cstdint>

namespace wwe::spin_waits {

// --- The frame limiter ---------------------------------------------------------
// The guest loops on appSleep(0.0) until its own clock passes the deadline.
// Sleeping until kLimiterMarginUs before it leaves the last stretch to the
// guest's yield loop, so the exit (and the 1/60 s step) is exactly the game's;
// the margin absorbs a late wake of the high-resolution timer.
inline constexpr int64_t kLimiterMarginUs = 1000;
// Longer than any frame step: a value past it means the clock values are not
// what the loop holds (a dilation ramp, a reset), so the hook does not sleep.
inline constexpr int64_t kLimiterLongestUs = 100000;

// How long to sleep for remaining_s (guest seconds left before the limiter's
// deadline); 0: yield as the game does.
inline int64_t LimiterSleepUs(double remaining_s) {
  const double us = remaining_s * 1e6;
  if (!(us > double(kLimiterMarginUs)) || us > double(kLimiterLongestUs)) return 0;
  return int64_t(us) - kLimiterMarginUs;
}

// --- Waits on another thread: D3D's GPU wait, the render-thread fence ------------
// Neither loop knows when it will end, so it backs off by how long it has waited.
enum class Backoff { kNone, kYield, kSleep };

// The first stretch costs no latency: a GPU or render thread that is nearly
// done keeps the game's own spin. Then yield to other ready threads, then
// sleep briefly.
inline constexpr int64_t kSpinUs = 50;
inline constexpr int64_t kYieldUs = 1000;
inline constexpr int64_t kSleepUs = 100;

// What one pass of a wait loop does, by time since the wait began.
inline Backoff WaitBackoff(int64_t waited_us) {
  if (waited_us < kSpinUs) return Backoff::kNone;
  if (waited_us < kYieldUs) return Backoff::kYield;
  return Backoff::kSleep;
}

}  // namespace wwe::spin_waits
