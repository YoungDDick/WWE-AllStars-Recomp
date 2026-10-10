// --wwe_lock_guest_time: guest time driven by presented frames, for scene runs
// (ticket 30). The game advances by elapsed time, so on the host clock a run
// that presents unevenly reaches a different state at the same frame number.
// Locked, every guest clock (timebase, system time, uptime) reads
// presents x 1/60 s plus host time since the last present, capped at one
// frame: frame N is the same game time on every run, and a wait shorter than a
// frame still ends. When nothing presents for a while (boot, loading), time
// follows the host again so the game's own waits end; the extra is carried
// into later frames, so only stalls add host-dependent time.

#pragma once

#include <algorithm>
#include <cstdint>

namespace rex {
class Runtime;
}

namespace wwe::frame_clock {

// Host-tick reading for the guest clock after `presents` presents and
// `since_present` host ticks into the next frame (before any carried ticks).
// Past stall_after ticks without a present, time follows the host.
constexpr uint64_t LockedTicks(uint64_t presents, uint64_t since_present,
                               uint64_t ticks_per_frame, uint64_t stall_after) {
  return presents * ticks_per_frame +
         (since_present > stall_after ? since_present : std::min(since_present, ticks_per_frame));
}

// What a present that ends a stall adds to the carried ticks, so the clock
// continues from where the stall left it.
constexpr uint64_t CarriedTicks(uint64_t since_present, uint64_t ticks_per_frame,
                                uint64_t stall_after) {
  return since_present > stall_after ? since_present - ticks_per_frame : 0;
}

// Installs the locked source when the cvar is set; call before guest threads run.
void Configure();

// The guest presented a frame (count presents so far).
void OnPresent(uint64_t presents);

}  // namespace wwe::frame_clock
