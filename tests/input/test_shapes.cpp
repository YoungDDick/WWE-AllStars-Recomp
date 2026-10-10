#include <catch_amalgamated.hpp>

#include <cstdint>
#include <vector>

#include "vibration/mat_impact.h"
#include "vibration/shapes.h"

using wwe::vibration::AttackerShapeFor;
using wwe::vibration::MatchEndRole;
using wwe::vibration::MatchEndShape;
using wwe::vibration::Sample;
using wwe::vibration::Shape;
using wwe::vibration::ShapeFor;
using wwe::vibration::Tier;

namespace {

// One expected sample: per motor amplitude (percent of full speed, the
// native's 0-100 scale) and waveform function, and the length in ms.
struct Expected {
  uint8_t left;
  uint8_t left_function;
  uint8_t right;
  uint8_t right_function;
  int ms;
};

constexpr uint8_t kConstant = 0, kFalling = 2, kCosineFall = 4, kNoise = 6;
// A crack is two frames at 60 Hz; it is held for 2.5 so frame-time jitter
// cannot cut it to one.
constexpr int kCrackMs = 42;

void CheckShape(const Shape& shape, const std::vector<Expected>& expected) {
  REQUIRE(shape.samples.size() == expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    INFO("sample " << i);
    const Sample& s = shape.samples[i];
    CHECK(s.left.amplitude == expected[i].left);
    CHECK(s.left.function == expected[i].left_function);
    CHECK(s.right.amplitude == expected[i].right);
    CHECK(s.right.function == expected[i].right_function);
    CHECK(s.seconds * 1000 == Catch::Approx(expected[i].ms).margin(0.5));
  }
}

void CheckShape(Tier tier, const std::vector<Expected>& expected) {
  CheckShape(ShapeFor(tier), expected);
}

}  // namespace

TEST_CASE("a light hit is a crack on the right motor with almost no left") {
  CheckShape(Tier::kLight, {{10, kConstant, 55, kConstant, kCrackMs}});
}

TEST_CASE("a strong hit is a crack then a short falling thump on the left") {
  CheckShape(Tier::kStrong, {{35, kConstant, 85, kConstant, kCrackMs},
                             {40, kFalling, 0, kConstant, 100}});
}

TEST_CASE("a grab is an even crack then a heavy cosine fall on the left") {
  CheckShape(Tier::kGrab, {{60, kConstant, 60, kConstant, kCrackMs},
                           {70, kCosineFall, 0, kConstant, 220}});
}

TEST_CASE("a weapon hit is a full right crack then a noisy rattle over a falling left") {
  CheckShape(Tier::kWeapon, {{30, kConstant, 100, kConstant, kCrackMs},
                             {30, kFalling, 35, kNoise, 100}});
}

TEST_CASE("a big moment is the only full left motor, fading over half a second") {
  // The research's 50 ms impact, held 3.5 frames so jitter cannot cut it to two.
  CheckShape(Tier::kBigMoment, {{100, kConstant, 100, kConstant, 58},
                                {100, kCosineFall, 30, kFalling, 550}});
}

TEST_CASE("critical health is a heartbeat on the left motor only") {
  CheckShape(Tier::kCritical, {{50, kConstant, 0, kConstant, 70},
                               {0, kConstant, 0, kConstant, 120},
                               {50, kConstant, 0, kConstant, 70}});
}

TEST_CASE("no effect asks a motor for more than full speed") {
  using wwe::vibration::ImpactRole;
  using wwe::vibration::ImpactShape;
  using wwe::vibration::ImpactSize;
  using wwe::vibration::ReversalRole;
  using wwe::vibration::ReversalShape;
  std::vector<Shape> every;
  for (Tier tier : {Tier::kLight, Tier::kStrong, Tier::kGrab, Tier::kWeapon, Tier::kBigMoment,
                    Tier::kCritical}) {
    every.push_back(ShapeFor(tier));
    if (auto attacker = AttackerShapeFor(tier)) every.push_back(*attacker);
  }
  for (ImpactSize size : {ImpactSize::kStrong, ImpactSize::kMedium, ImpactSize::kLight}) {
    every.push_back(ImpactShape(size, ImpactRole::kImpact));
    every.push_back(ImpactShape(size, ImpactRole::kEcho));
  }
  every.push_back(ReversalShape(ReversalRole::kReverser));
  every.push_back(ReversalShape(ReversalRole::kReversed));
  every.push_back(wwe::vibration::WindUpShape());
  every.push_back(MatchEndShape(MatchEndRole::kWinner));
  every.push_back(MatchEndShape(MatchEndRole::kLoser));
  for (size_t i = 0; i < every.size(); ++i) {
    INFO("effect " << i);
    REQUIRE_FALSE(every[i].samples.empty());
    for (const Sample& s : every[i].samples) {
      CHECK(s.left.amplitude <= 100);
      CHECK(s.right.amplitude <= 100);
      CHECK(s.left.function <= 6);
      CHECK(s.right.function <= 6);
      CHECK(s.seconds > 0);
    }
  }
}

TEST_CASE("only a big moment drives the left motor at full speed") {
  for (Tier tier : {Tier::kLight, Tier::kStrong, Tier::kGrab, Tier::kWeapon, Tier::kCritical}) {
    INFO(wwe::vibration::ToString(tier));
    CHECK(ShapeFor(tier).Peak().left < 100);
  }
  CHECK(ShapeFor(Tier::kBigMoment).Peak().left == 100);
}

TEST_CASE("an attacker's strike lands as a right-motor tap only") {
  CheckShape(*AttackerShapeFor(Tier::kLight), {{0, kConstant, 25, kConstant, kCrackMs}});
  CheckShape(*AttackerShapeFor(Tier::kStrong), {{0, kConstant, 35, kConstant, kCrackMs}});
  CheckShape(*AttackerShapeFor(Tier::kWeapon), {{0, kConstant, 35, kConstant, kCrackMs}});
}

TEST_CASE("an attacker's grab or slam lands as a tap and a short left fall") {
  CheckShape(*AttackerShapeFor(Tier::kGrab), {{0, kConstant, 35, kConstant, kCrackMs},
                                              {25, kFalling, 0, kConstant, 100}});
}

TEST_CASE("an attacker's finisher lands as a strong tap and a left fall") {
  CheckShape(*AttackerShapeFor(Tier::kBigMoment), {{0, kConstant, 60, kConstant, kCrackMs},
                                                   {50, kCosineFall, 0, kConstant, 300}});
}

TEST_CASE("critical health is the victim's alone: the attacker gets nothing") {
  CHECK_FALSE(AttackerShapeFor(Tier::kCritical));
}

TEST_CASE("an attacker confirm is lighter on both motors than the same hit taken") {
  for (Tier tier : {Tier::kLight, Tier::kStrong, Tier::kGrab, Tier::kWeapon, Tier::kBigMoment}) {
    INFO(wwe::vibration::ToString(tier));
    const auto attacker = AttackerShapeFor(tier)->Peak();
    const auto victim = ShapeFor(tier).Peak();
    CHECK(attacker.left < victim.left);
    CHECK(attacker.right < victim.right);
  }
}

TEST_CASE("frequent effects never drive the left motor above 40 %") {
  // Strikes taken and every attacker tap for a strike, the events that fire
  // most (the hierarchy rule; light impacts in test_mat_impact.cpp).
  CHECK(ShapeFor(Tier::kLight).Peak().left <= 40);
  CHECK(ShapeFor(Tier::kStrong).Peak().left <= 40);
  for (Tier tier : {Tier::kLight, Tier::kStrong, Tier::kWeapon}) {
    INFO(wwe::vibration::ToString(tier));
    CHECK(AttackerShapeFor(tier)->Peak().left <= 40);
  }
}

TEST_CASE("the match's loser feels a big moment") {
  CheckShape(MatchEndShape(MatchEndRole::kLoser), {{100, kConstant, 100, kConstant, 58},
                                                   {100, kCosineFall, 30, kFalling, 550}});
}

TEST_CASE("the match's winner feels a warm double pulse on the left, not a hit") {
  constexpr uint8_t kSineBump = 5;
  CheckShape(MatchEndShape(MatchEndRole::kWinner), {{40, kSineBump, 0, kConstant, 150},
                                                    {0, kConstant, 0, kConstant, 100},
                                                    {40, kSineBump, 0, kConstant, 150}});
}

TEST_CASE("only the loser's match end reaches full left") {
  CHECK(MatchEndShape(MatchEndRole::kLoser).Peak().left == 100);
  CHECK(MatchEndShape(MatchEndRole::kWinner).Peak().left < 100);
}
