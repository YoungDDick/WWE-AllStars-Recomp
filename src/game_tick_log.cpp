// Times the game's own engine tick, so a hitch can be told apart from Native's work
// (--wwe_tick_log). FEngineLoop::Tick at 0x82417780 runs one fixed step per frame and
// waits twice, both by spinning on appSleep(0) (0x82608EF8): on the render thread (the
// game-thread fence, 0x8271CB70, which keeps the render thread at most a frame behind)
// and in a 60 Hz frame limiter at its end. The log splits the time in appSleep into the
// two waits; the rest is the game working or waiting on the GPU.
// See docs/research/movement-judder.md §1 for the tick's layout.
//
// The same hooks stop both waits spinning a host core (low-end-preset ticket 10,
// docs/research/spin-loops.md §1 and §5): the limiter sleeps until shortly before the
// deadline the guest loop tests, and the render-thread wait backs off like D3D's GPU wait
// (gpu_wait.cpp); the guest's own tests still decide when each wait ends.

#include "generated/default/wweallstars_pch.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/thread.h>

#include "spin_waits.h"

REXCVAR_DEFINE_BOOL(wwe_tick_log, false, "WWEAllStars",
                    "Log the game's engine tick: a line per slow tick and a summary each second");
REXCVAR_DEFINE_BOOL(wwe_limiter_sleep, true, "WWEAllStars",
                    "Let the game thread's frame waits (the 60 Hz limiter and the wait on the "
                    "render thread) sleep instead of spinning a host core (false: the game's "
                    "own appSleep(0) yield loops, for comparisons)");

namespace {

using Clock = std::chrono::steady_clock;

// FEngineLoop fields written by the tick (this = r3): the step and the time dilation.
constexpr uint32_t kDeltaSecondsOffset = 0x28;
constexpr uint32_t kDilationOffset = 0x2C;
// The limiter's clock (spin-loops.md §1): the frame start, a double at this+0xC
// (stored at 0x82417948/5C), and GCurrentTime, which the loop stores just before each
// appSleep call (0x82417D48 and 0x82417D8C).
constexpr uint32_t kFrameStartOffset = 0xC;
constexpr uint32_t kCurrentTime = 0x8370BD40;
// The limiter's call is bl 0x82608ef8 at 0x82417D70; the game calls appSleep(0.0) from
// other places inside the tick too (its own yields), where these values mean nothing.
constexpr uint32_t kLimiterReturn = 0x82417D74;
// The render-thread wait's call is bl 0x82608ef8 at 0x8271CC04, in its loop while the fence
// counter is above the target (0x8271CBC4-0x8271CC10, spin-loops.md §5).
constexpr uint32_t kRenderWaitReturn = 0x8271CC08;
// Set together with two clear flags, the tick steps 1/30 s instead of 1/60 s.
constexpr uint32_t kHalfRateFlag = 0x83709514;
constexpr double kSlowTickMs = 20.0;

thread_local bool t_in_tick = false;
thread_local uint32_t t_self = 0;  // the FEngineLoop whose tick runs
// Time in appSleep during the tick: the limiter's, the render-thread wait's, any other call's.
struct Waits {
  uint64_t limiter_ns = 0, render_ns = 0, other_ns = 0;
  uint64_t Total() const { return limiter_ns + render_ns + other_ns; }
};
thread_local Waits t_waits;
// When the current render-thread wait on this thread began.
thread_local Clock::time_point t_render_wait_start;

struct Second {
  Clock::time_point start{};
  uint32_t ticks = 0, half_rate_ticks = 0, other_step_ticks = 0;
  double work_sum_ms = 0, work_max_ms = 0, total_max_ms = 0;
  float dilation_min = 1e9f, dilation_max = -1e9f;
} g_second;

uint32_t LoadU32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return std::byteswap(value);
}

float LoadF32(const uint8_t* base, uint32_t address) {
  return std::bit_cast<float>(LoadU32(base, address));
}

double LoadF64(const uint8_t* base, uint32_t address) {
  return std::bit_cast<double>(uint64_t(LoadU32(base, address)) << 32 | LoadU32(base, address + 4));
}

// The limiter's time left before its deadline, in guest seconds, from the values the
// loop has just computed.
double LimiterRemaining(const uint8_t* base, uint32_t self) {
  const double dt = double(LoadF32(base, self + kDeltaSecondsOffset)) *
                    double(LoadF32(base, self + kDilationOffset));
  return LoadF64(base, self + kFrameStartOffset) + dt - LoadF64(base, kCurrentTime);
}

double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

void Record(const uint8_t* base, uint32_t self, Clock::duration total, const Waits& waits) {
  const double total_ms = Ms(total);
  const double work_ms = std::max(0.0, total_ms - waits.Total() / 1e6);
  const float step = LoadF32(base, self + kDeltaSecondsOffset);
  const float dilation = LoadF32(base, self + kDilationOffset);
  const bool half_rate = LoadU32(base, kHalfRateFlag) != 0;
  if (total_ms > kSlowTickMs) {
    // scripts/analyze_hitches.py reads this line (GAME_TICK).
    REXLOG_INFO("game tick: {:.1f} ms, work {:.1f} ms, limiter {:.1f} ms, render thread "
                "{:.1f} ms, other waits {:.1f} ms; step {:.5f} s, dilation {:.3f}, 1/30 flag {}",
                total_ms, work_ms, waits.limiter_ns / 1e6, waits.render_ns / 1e6,
                waits.other_ns / 1e6, step, dilation, int(half_rate));
  }
  Second& s = g_second;
  const auto now = Clock::now();
  if (s.start == Clock::time_point{}) {
    s.start = now;
  }
  ++s.ticks;
  s.half_rate_ticks += std::abs(step - 1.0f / 30) < 1e-4f;
  s.other_step_ticks += std::abs(step - 1.0f / 60) >= 1e-4f && std::abs(step - 1.0f / 30) >= 1e-4f;
  s.work_sum_ms += work_ms;
  s.work_max_ms = std::max(s.work_max_ms, work_ms);
  s.total_max_ms = std::max(s.total_max_ms, total_ms);
  s.dilation_min = std::min(s.dilation_min, dilation);
  s.dilation_max = std::max(s.dilation_max, dilation);
  if (now - s.start >= std::chrono::seconds(1)) {
    REXLOG_INFO("game tick 1s: {} ticks ({} at 1/30 s, {} other steps), work mean {:.2f} ms "
                "max {:.1f} ms, tick max {:.1f} ms, dilation {:.3f}..{:.3f}",
                s.ticks, s.half_rate_ticks, s.other_step_ticks, s.work_sum_ms / s.ticks,
                s.work_max_ms, s.total_max_ms, s.dilation_min, s.dilation_max);
    s = Second{};
    s.start = now;
  }
}

}  // namespace

// FEngineLoop::Tick.
DECLARE_REX_FUNC(sub_82417780);
REX_HOOK_RAW(sub_82417780) {
  const uint32_t self = ctx.r3.u32;
  const auto start = Clock::now();
  t_in_tick = true;
  t_self = self;
  t_waits = {};
  __imp__sub_82417780(ctx, base);
  t_in_tick = false;
  if (REXCVAR_GET(wwe_tick_log)) {
    Record(base, self, Clock::now() - start, t_waits);
  }
}

// The game-thread fence wait (r3 = the fence counter, r4 = how many frames the render
// thread may lag): runs once per wait, so it only marks when the wait began.
DECLARE_REX_FUNC(sub_8271CB70);
REX_HOOK_RAW(sub_8271CB70) {
  t_render_wait_start = Clock::now();
  __imp__sub_8271CB70(ctx, base);
}

// appSleep(seconds). Only the limiter's and the render-thread wait's own appSleep(0.0) wait on
// the host (spin-loops.md §1, §5); every other call runs as shipped.
DECLARE_REX_FUNC(sub_82608EF8);
REX_HOOK_RAW(sub_82608EF8) {
  const bool zero = ctx.f1.f64 == 0.0;
  const bool limiter = t_in_tick && zero && ctx.lr == kLimiterReturn;
  const bool render_wait = zero && ctx.lr == kRenderWaitReturn;
  if (!t_in_tick && !render_wait) {
    __imp__sub_82608EF8(ctx, base);
    return;
  }
  const auto start = Clock::now();
  const bool sleep_on = REXCVAR_GET(wwe_limiter_sleep);
  int64_t sleep_us = 0;
  if (sleep_on && limiter) {
    sleep_us = wwe::spin_waits::LimiterSleepUs(LimiterRemaining(base, t_self));
  } else if (sleep_on && render_wait) {
    const int64_t waited_us =
        std::chrono::duration_cast<std::chrono::microseconds>(start - t_render_wait_start).count();
    if (wwe::spin_waits::WaitBackoff(waited_us) == wwe::spin_waits::Backoff::kSleep) {
      sleep_us = wwe::spin_waits::kSleepUs;
    }
    // kNone and kYield: the game's own appSleep(0) is already a yield.
  }
  if (sleep_us > 0) {
    rex::thread::PreciseSleep(std::chrono::microseconds(sleep_us));
  } else {
    __imp__sub_82608EF8(ctx, base);
  }
  if (!t_in_tick) {
    return;
  }
  const auto took = uint64_t(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
  (limiter ? t_waits.limiter_ns : render_wait ? t_waits.render_ns : t_waits.other_ns) += took;
}
