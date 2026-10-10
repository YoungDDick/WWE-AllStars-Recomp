#include <catch_amalgamated.hpp>

#include <rex/system/swap_choice.h>

using rex::system::external_frame::ChooseSwapFrame;
using rex::system::external_frame::SwapChoice;

// Native can still be drawing into the slot of the frame two behind its newest one, so a swap
// may show the newest frame or the one before it.
constexpr uint64_t kMaxLag = 1;

TEST_CASE("the first swap shows the newest frame") {
  const SwapChoice c = ChooseSwapFrame(std::nullopt, 7, kMaxLag);
  CHECK(c.show == 7);
  CHECK(c.skipped == 0);
  CHECK_FALSE(c.repeated);
}

TEST_CASE("a swap on time shows the next frame") {
  const SwapChoice c = ChooseSwapFrame(6, 7, kMaxLag);
  CHECK(c.show == 7);
  CHECK(c.skipped == 0);
  CHECK_FALSE(c.repeated);
}

TEST_CASE("a swap one frame late shows its own frame, not the newest") {
  // The command processor reaches swap 7 after Native already published 8: showing 8 would skip
  // 7 now and repeat 8 on the next swap.
  const SwapChoice c = ChooseSwapFrame(6, 8, kMaxLag);
  CHECK(c.show == 7);
  CHECK(c.skipped == 0);
  CHECK_FALSE(c.repeated);
}

TEST_CASE("the swap after a late one catches up without a repeat") {
  const SwapChoice c = ChooseSwapFrame(7, 8, kMaxLag);
  CHECK(c.show == 8);
  CHECK_FALSE(c.repeated);
}

TEST_CASE("a swap too far behind skips to the oldest frame still safe to show") {
  // Frames before newest - lag may already be overwritten by Native.
  const SwapChoice c = ChooseSwapFrame(3, 9, kMaxLag);
  CHECK(c.show == 8);
  CHECK(c.skipped == 4);  // 4, 5, 6, 7
  CHECK_FALSE(c.repeated);
}

TEST_CASE("a swap with no new frame shows the last one again") {
  const SwapChoice c = ChooseSwapFrame(7, 7, kMaxLag);
  CHECK(c.show == 7);
  CHECK(c.skipped == 0);
  CHECK(c.repeated);
}

TEST_CASE("a missing frame is skipped, not waited for") {
  // Native did not publish 7 (a failed submit): the swap after 6 sees only 8.
  const SwapChoice c = ChooseSwapFrame(6, 8, kMaxLag, /*available=*/[](uint64_t s) { return s != 7; });
  CHECK(c.show == 8);
  CHECK(c.skipped == 1);
  CHECK_FALSE(c.repeated);
}
