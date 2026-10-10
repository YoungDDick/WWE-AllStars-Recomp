#include <catch_amalgamated.hpp>

#include <optional>

#include "fake_guest.h"
#include "vibration/force_feedback.h"

using wwe::test::FakeGuest;
using wwe::vibration::RumblePad;
using wwe::vibration::RumbleTarget;

TEST_CASE("a hit on a wrestler its player controls rumbles that player's controller") {
  FakeGuest guest;
  CHECK(RumbleTarget(guest.base(), FakeGuest::kPawn) == FakeGuest::kController);
}

TEST_CASE("a hit on a wrestler the AI possesses rumbles no pad") {
  FakeGuest guest;
  guest.PossessByAi();
  CHECK_FALSE(RumbleTarget(guest.base(), FakeGuest::kPawn));
}

TEST_CASE("a controller without a force-feedback manager is not a target") {
  FakeGuest guest;
  guest.Store32(FakeGuest::kController + 0x458, 0);
  CHECK_FALSE(RumbleTarget(guest.base(), FakeGuest::kPawn));

  FakeGuest other;
  other.Store32(FakeGuest::kManager, 0x821D7C90);  // some other object
  CHECK_FALSE(RumbleTarget(other.base(), FakeGuest::kPawn));
}

TEST_CASE("a pawn with no controller is not a target") {
  FakeGuest guest;
  guest.Store32(FakeGuest::kPawn + 0x200, 0);
  CHECK_FALSE(RumbleTarget(guest.base(), FakeGuest::kPawn));
}

TEST_CASE("a wrestler's pad is its local player's controller id") {
  const uint32_t pad = GENERATE(0u, 1u, 3u);
  FakeGuest guest(pad);
  CHECK(RumblePad(guest.base(), FakeGuest::kPawn) == pad);
}

TEST_CASE("the AI's wrestler has no pad") {
  FakeGuest guest(1);
  guest.PossessByAi();
  CHECK_FALSE(RumblePad(guest.base(), FakeGuest::kPawn));
}

TEST_CASE("a controller with no local player, or a stale one, has no pad") {
  FakeGuest none;
  none.Store32(FakeGuest::kController + 0x2E4, 0);
  CHECK_FALSE(RumblePad(none.base(), FakeGuest::kPawn));

  FakeGuest stale;  // the player's back link names another controller
  stale.Store32(FakeGuest::kPlayer + 0x38, 0x1400);
  CHECK_FALSE(RumblePad(stale.base(), FakeGuest::kPawn));
}

TEST_CASE("a controller id outside 0-3 has no pad") {
  FakeGuest guest(4);
  CHECK_FALSE(RumblePad(guest.base(), FakeGuest::kPawn));
  FakeGuest unset(0xFFFFFFFF);
  CHECK_FALSE(RumblePad(unset.base(), FakeGuest::kPawn));
}
