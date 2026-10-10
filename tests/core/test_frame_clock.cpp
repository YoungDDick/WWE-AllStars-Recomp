#include <catch_amalgamated.hpp>

#include "frame_clock.h"

using wwe::frame_clock::CarriedTicks;
using wwe::frame_clock::LockedTicks;

// One frame is 1000 ticks; time follows the host after a 4000-tick stall.
constexpr uint64_t kFrame = 1000, kStall = 4000;

TEST_CASE("guest ticks advance one frame per present") {
  CHECK(LockedTicks(0, 0, kFrame, kStall) == 0);
  CHECK(LockedTicks(3, 0, kFrame, kStall) == 3000);
}

TEST_CASE("within a frame time runs with the host, capped at the next frame") {
  CHECK(LockedTicks(3, 250, kFrame, kStall) == 3250);
  CHECK(LockedTicks(3, 1000, kFrame, kStall) == 4000);
  CHECK(LockedTicks(3, 3999, kFrame, kStall) == 4000);  // a hitch: time waits for the present
}

TEST_CASE("a stall without presents lets time follow the host so waits end") {
  // Boot before the first present, loading: nothing presents for a while.
  CHECK(LockedTicks(0, 4001, kFrame, kStall) == 4001);
  CHECK(LockedTicks(3, 10000, kFrame, kStall) == 13000);
}

TEST_CASE("a stall's extra time is carried into the next frames") {
  CHECK(CarriedTicks(3999, kFrame, kStall) == 0);
  CHECK(CarriedTicks(10000, kFrame, kStall) == 9000);
  // Continuous across the present that ends the stall: just before it the
  // clock reads 13000; just after, offset 9000 + present 4 x 1000.
  CHECK(LockedTicks(3, 10000, kFrame, kStall) ==
        CarriedTicks(10000, kFrame, kStall) + LockedTicks(4, 0, kFrame, kStall));
}

TEST_CASE("time never runs backwards across a normal present") {
  CHECK(LockedTicks(3, 3999, kFrame, kStall) <= LockedTicks(4, 0, kFrame, kStall));
}
