#include <catch_amalgamated.hpp>

#include <cstdint>
#include <optional>

#include "vibration/effect_log.h"
#include "vibration/pulse_tiers.h"
#include "vibration/shapes.h"

using wwe::vibration::ShapeFor;
using wwe::vibration::Tier;
using wwe::vibration::TierForCombatType;

TEST_CASE("every combat type from a move maps to its tier") {
  const std::pair<uint8_t, Tier> table[] = {
      {1, Tier::kLight},   {7, Tier::kLight},      {2, Tier::kStrong},     {9, Tier::kStrong},
      {16, Tier::kStrong}, {4, Tier::kStrong},     {3, Tier::kGrab},       {6, Tier::kGrab},
      {8, Tier::kGrab},    {10, Tier::kGrab},      {17, Tier::kGrab},      {11, Tier::kWeapon},
      {12, Tier::kWeapon}, {13, Tier::kWeapon},    {14, Tier::kWeapon},    {18, Tier::kWeapon},
      {5, Tier::kBigMoment}, {15, Tier::kBigMoment},
  };
  for (const auto& [combat_type, tier] : table) {
    INFO("combat type " << int(combat_type));
    CHECK(TierForCombatType(combat_type) == tier);
  }
}

TEST_CASE("damage that is not from a move, and unknown combat types, give no pulse") {
  CHECK_FALSE(TierForCombatType(0));
  CHECK_FALSE(TierForCombatType(19));
  CHECK_FALSE(TierForCombatType(0xFF));
}

TEST_CASE("an effect is logged with its event, role, pad, both motors and length") {
  CHECK(wwe::vibration::EffectLine("hit", "victim", 0, ShapeFor(Tier::kGrab),
                                   "tier=grab combat_type=3") ==
        "vibration effect: event=hit role=victim pad=0 left=70 right=60 ms=262 tier=grab "
        "combat_type=3");
  CHECK(wwe::vibration::EffectLine("critical", "victim", 1, ShapeFor(Tier::kCritical)) ==
        "vibration effect: event=critical role=victim pad=1 left=50 right=0 ms=260");
}

TEST_CASE("the pulse log stops after its cap with one notice") {
  wwe::vibration::EffectLog log;
  for (uint32_t i = 0; i < wwe::vibration::EffectLog::kMaxLines; ++i) {
    REQUIRE(log.Take("line"));
  }
  CHECK(log.Take("line") == "vibration effect: 300 lines logged, later effects are not logged");
  CHECK_FALSE(log.Take("line"));
}
