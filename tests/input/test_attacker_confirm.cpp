// Attacker confirm's guest read (seam 3): the hit's attacker at the hit hook,
// cast before any read and behind RumbleTarget.

#include <catch_amalgamated.hpp>

#include <cstdint>
#include <optional>

#include "fake_guest.h"
#include "vibration/force_feedback.h"
#include "vibration/mat_impact.h"

using wwe::test::FakeGuest;
using wwe::vibration::HitAttacker;
using wwe::vibration::RumblePad;

namespace {

// The victim, another pawn whose last attacker is the fake's wrestler.
constexpr uint32_t kVictim = 0x400;

// The game's pawn cast 0x82435EC0: the fake's pawn is a pawn, nothing else is.
uint32_t Cast(uint32_t object) { return object == FakeGuest::kPawn ? object : 0; }

}  // namespace

TEST_CASE("the hit's attacker is the victim's last attacker, as a pawn") {
  FakeGuest guest(2);
  guest.Store32(kVictim + 0x95C, FakeGuest::kPawn);

  const std::optional<uint32_t> attacker = HitAttacker(guest.base(), kVictim, Cast);

  REQUIRE(attacker == FakeGuest::kPawn);
  CHECK(RumblePad(guest.base(), *attacker) == 2u);
}

TEST_CASE("an attacker that is no pawn, none, or the victim itself confirms nothing") {
  FakeGuest weapon;
  weapon.Store32(kVictim + 0x95C, 0x700);  // not a pawn: the cast gives null
  CHECK_FALSE(HitAttacker(weapon.base(), kVictim, Cast));

  FakeGuest none;
  CHECK_FALSE(HitAttacker(none.base(), kVictim, Cast));

  FakeGuest self;
  self.Store32(FakeGuest::kPawn + 0x95C, FakeGuest::kPawn);
  CHECK_FALSE(HitAttacker(self.base(), FakeGuest::kPawn, Cast));
}

TEST_CASE("an AI attacker rumbles no pad") {
  FakeGuest guest(1);
  guest.PossessByAi();
  guest.Store32(kVictim + 0x95C, FakeGuest::kPawn);

  const std::optional<uint32_t> attacker = HitAttacker(guest.base(), kVictim, Cast);

  REQUIRE(attacker);
  CHECK_FALSE(RumblePad(guest.base(), *attacker));
}
