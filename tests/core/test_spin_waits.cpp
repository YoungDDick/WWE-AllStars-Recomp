// The wait decisions of the game thread's frame waits and the GPU-wait loops
// (low-end-preset tickets 10 and 11).

#include <catch_amalgamated.hpp>

#include <limits>

#include "spin_waits.h"

using wwe::spin_waits::Backoff;
using wwe::spin_waits::WaitBackoff;
using wwe::spin_waits::LimiterSleepUs;

TEST_CASE("the limiter sleeps until a millisecond before its deadline") {
  // Early in a 1/60 s frame: 12 ms left, sleep 11 ms.
  CHECK(LimiterSleepUs(0.012) == 11000);
  CHECK(LimiterSleepUs(0.0166667) == 15666);
  // The last millisecond stays the game's own yield loop, so the exit is exact.
  CHECK(LimiterSleepUs(0.0011) == 100);
  CHECK(LimiterSleepUs(0.001) == 0);
  CHECK(LimiterSleepUs(0.0004) == 0);
}

TEST_CASE("the limiter never sleeps past its deadline or on values that are not a frame") {
  CHECK(LimiterSleepUs(0.0) == 0);
  CHECK(LimiterSleepUs(-0.003) == 0);  // already late
  CHECK(LimiterSleepUs(0.5) == 0);     // not a frame step: leave it to the game
  CHECK(LimiterSleepUs(std::numeric_limits<double>::quiet_NaN()) == 0);
}

TEST_CASE("a wait on the GPU or the render thread spins briefly, then yields, then sleeps") {
  CHECK(WaitBackoff(0) == Backoff::kNone);
  CHECK(WaitBackoff(49) == Backoff::kNone);
  CHECK(WaitBackoff(50) == Backoff::kYield);
  CHECK(WaitBackoff(999) == Backoff::kYield);
  CHECK(WaitBackoff(1000) == Backoff::kSleep);
  CHECK(WaitBackoff(250000) == Backoff::kSleep);
}

TEST_CASE("a clock that went backwards counts as a wait that just began") {
  CHECK(WaitBackoff(-5) == Backoff::kNone);
}
