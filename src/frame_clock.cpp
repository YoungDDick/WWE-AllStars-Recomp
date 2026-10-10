#include "frame_clock.h"

#include <atomic>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(wwe_lock_guest_time, false, "WWEAllStars",
                    "Scene runs: guest time advances 1/60 s per presented frame instead of with "
                    "the host clock, so frame N is the same game state on every run");

namespace wwe::frame_clock {

namespace {

using rex::chrono::Clock;

// The game's release week; a fixed date keeps anything derived from the
// system time the same on every run.
constexpr uint64_t kSystemTimeBase = 0x01CBEFFFBE624000ull;  // 2011-04-01 00:00 UTC

bool g_enabled = false;
uint64_t g_ticks_per_frame = 0;
// The host tick count when the source was installed: the SDK's clock works on
// deltas from its last host reading, so the source continues from there.
uint64_t g_base_tick = 0;
// Host ticks without a present after which guest time follows the host.
uint64_t g_stall_after = 0;
// Ticks carried from stalls.
std::atomic<uint64_t> g_carried{0};
// Written by OnPresent under a sequence counter (odd while writing); Source
// rereads until it sees one consistent present.
std::atomic<uint64_t> g_sequence{0};
std::atomic<uint64_t> g_last_present_tick{0};
std::atomic<uint64_t> g_presents{0};
// Readers on several threads: the running maximum keeps guest time monotonic.
std::atomic<uint64_t> g_last_returned{0};

uint64_t Source() {
  uint64_t presents, last, carried, sequence;
  do {
    sequence = g_sequence.load(std::memory_order_acquire);
    presents = g_presents.load(std::memory_order_acquire);
    last = g_last_present_tick.load(std::memory_order_acquire);
    carried = g_carried.load(std::memory_order_acquire);
  } while ((sequence & 1) || g_sequence.load(std::memory_order_acquire) != sequence);
  const uint64_t now = Clock::QueryHostTickCount();
  const uint64_t ticks = g_base_tick + carried +
                         LockedTicks(presents, now > last ? now - last : 0, g_ticks_per_frame,
                                     g_stall_after);
  uint64_t previous = g_last_returned.load(std::memory_order_relaxed);
  while (ticks > previous &&
         !g_last_returned.compare_exchange_weak(previous, ticks, std::memory_order_relaxed)) {
  }
  return std::max(ticks, previous);
}

}  // namespace

void Configure() {
  if (!REXCVAR_GET(wwe_lock_guest_time)) {
    return;
  }
  g_enabled = true;
  g_ticks_per_frame = Clock::QueryHostTickFrequency() / 60;
  g_stall_after = g_ticks_per_frame * 4;
  g_base_tick = Clock::QueryHostTickCount();
  g_last_present_tick.store(g_base_tick, std::memory_order_release);
  Clock::set_guest_system_time_base(kSystemTimeBase);
  Clock::set_host_tick_source(Source);
  REXLOG_INFO("frame clock: guest time locked to presents ({} host ticks per frame)",
              g_ticks_per_frame);
}

void OnPresent(uint64_t presents) {
  if (!g_enabled) {
    return;
  }
  const uint64_t now = Clock::QueryHostTickCount();
  const uint64_t last = g_last_present_tick.load(std::memory_order_relaxed);
  g_sequence.fetch_add(1, std::memory_order_acq_rel);  // odd: writing
  g_carried.fetch_add(CarriedTicks(now > last ? now - last : 0, g_ticks_per_frame, g_stall_after),
                      std::memory_order_release);
  g_last_present_tick.store(now, std::memory_order_release);
  g_presents.store(presents, std::memory_order_release);
  g_sequence.fetch_add(1, std::memory_order_acq_rel);  // even: done
}

}  // namespace wwe::frame_clock
