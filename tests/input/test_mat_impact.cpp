#include <catch_amalgamated.hpp>

#include <cstdint>
#include <optional>

#include "fake_guest.h"
#include "vibration/mat_impact.h"
#include "vibration/effect_log.h"

using wwe::test::FakeGuest;
using wwe::vibration::ImpactRole;
using wwe::vibration::ImpactShape;
using wwe::vibration::ImpactSize;
using wwe::vibration::ImpactSizeForGroundEvent;
using wwe::vibration::ImpactSizeForShake;
using wwe::vibration::Partner;
using wwe::vibration::Shape;

TEST_CASE("slams and heavy stomps map to their impact size") {
  // AnimNotify_FmodGroundEvent.EAnimNotifyEventType (docs/research/impact-events.md).
  CHECK(ImpactSizeForGroundEvent(9) == ImpactSize::kStrong);   // Slam_Heavy
  CHECK(ImpactSizeForGroundEvent(13) == ImpactSize::kStrong);  // HalfSlam_Mat
  CHECK(ImpactSizeForGroundEvent(8) == ImpactSize::kMedium);   // Slam_Med
  CHECK(ImpactSizeForGroundEvent(7) == ImpactSize::kLight);    // Slam_Reg
  CHECK(ImpactSizeForGroundEvent(6) == ImpactSize::kLight);    // Stomp_Heavy
  CHECK(ImpactSizeForGroundEvent(12) == ImpactSize::kLight);   // Barrier_Impact
}

TEST_CASE("walking, footsteps, light stomps, rattles and barriers never rumble") {
  for (uint8_t type : {0, 1, 2, 3, 4, 5, 10, 11, 14, 0xFF}) {
    INFO("type " << int(type));
    CHECK_FALSE(ImpactSizeForGroundEvent(type));
  }
}

namespace {

// Sample 1's motors and the body's left motor, in percent.
struct Expected {
  uint8_t crack_left;
  uint8_t crack_right;
  uint8_t body_left;
};

void CheckImpact(ImpactSize size, ImpactRole role, Expected expected) {
  const Shape shape = ImpactShape(size, role);
  REQUIRE(shape.samples.size() == 2);
  CHECK(shape.samples[0].left.amplitude == expected.crack_left);
  CHECK(shape.samples[0].right.amplitude == expected.crack_right);
  CHECK(shape.samples[1].left.amplitude == expected.body_left);
  CHECK(shape.samples[1].left.function == 4);  // cosine fall, the grab's body
  CHECK(shape.samples[1].seconds == Catch::Approx(0.220f));
}

}  // namespace

TEST_CASE("a strong impact is the grab shape, medium and light scaled down") {
  CheckImpact(ImpactSize::kStrong, ImpactRole::kImpact, {60, 60, 70});
  CheckImpact(ImpactSize::kMedium, ImpactRole::kImpact, {45, 45, 53});
  CheckImpact(ImpactSize::kLight, ImpactRole::kImpact, {30, 30, 35});
}

TEST_CASE("the other wrestler's echo is half the impact") {
  CheckImpact(ImpactSize::kStrong, ImpactRole::kEcho, {30, 30, 35});
  CheckImpact(ImpactSize::kMedium, ImpactRole::kEcho, {23, 23, 26});
  CheckImpact(ImpactSize::kLight, ImpactRole::kEcho, {15, 15, 18});
}

TEST_CASE("the echo is lighter than the impact on both motors") {
  for (ImpactSize size : {ImpactSize::kStrong, ImpactSize::kMedium, ImpactSize::kLight}) {
    const auto impact = ImpactShape(size, ImpactRole::kImpact).Peak();
    const auto echo = ImpactShape(size, ImpactRole::kEcho).Peak();
    CHECK(echo.left < impact.left);
    CHECK(echo.right < impact.right);
  }
}

namespace {

constexpr uint32_t kTarget = 0x300, kVictim = 0x400, kAttacker = 0x500;

}  // namespace

TEST_CASE("the partner is the wrestler's current target") {
  FakeGuest guest;
  guest.Store32(FakeGuest::kPawn + 0x81C, kTarget);
  guest.Store32(FakeGuest::kPawn + 0x938, kVictim);
  guest.Store32(FakeGuest::kPawn + 0x95C, kAttacker);
  CHECK(Partner(guest.base(), FakeGuest::kPawn) == kTarget);
}

TEST_CASE("without a target the partner is the last hit victim, then the last attacker") {
  FakeGuest guest;
  guest.Store32(FakeGuest::kPawn + 0x938, kVictim);
  guest.Store32(FakeGuest::kPawn + 0x95C, kAttacker);
  CHECK(Partner(guest.base(), FakeGuest::kPawn) == kVictim);

  FakeGuest attacked;
  attacked.Store32(FakeGuest::kPawn + 0x95C, kAttacker);
  CHECK(Partner(attacked.base(), FakeGuest::kPawn) == kAttacker);
}

TEST_CASE("a wrestler alone in the exchange has no partner") {
  FakeGuest guest;
  CHECK_FALSE(Partner(guest.base(), FakeGuest::kPawn));
}

TEST_CASE("a camera shake's size gives the impact size") {
  // SETSCSK +0x10, the designers' strength (docs/research/impact-events.md B).
  CHECK(ImpactSizeForShake(0.6f) == ImpactSize::kLight);
  CHECK(ImpactSizeForShake(1.0f) == ImpactSize::kLight);
  CHECK(ImpactSizeForShake(1.1f) == ImpactSize::kLight);
  CHECK(ImpactSizeForShake(2.0f) == ImpactSize::kMedium);
  CHECK(ImpactSizeForShake(2.5f) == ImpactSize::kMedium);
  CHECK(ImpactSizeForShake(3.0f) == ImpactSize::kStrong);
  CHECK(ImpactSizeForShake(4.0f) == ImpactSize::kStrong);
}

TEST_CASE("a camera shake of no size rumbles nothing") {
  CHECK_FALSE(ImpactSizeForShake(0.0f));
  CHECK_FALSE(ImpactSizeForShake(-1.0f));
}

TEST_CASE("a light shake or impact stays under 40 % left, and every echo is lighter") {
  CHECK(ImpactShape(ImpactSize::kLight, ImpactRole::kImpact).Peak().left <= 40);
  CHECK(ImpactShape(ImpactSize::kLight, ImpactRole::kEcho).Peak().left <= 40);
  for (ImpactSize size : {ImpactSize::kStrong, ImpactSize::kMedium, ImpactSize::kLight}) {
    const auto impact = ImpactShape(size, ImpactRole::kImpact).Peak();
    const auto echo = ImpactShape(size, ImpactRole::kEcho).Peak();
    CHECK(echo.left < impact.left);
    CHECK(echo.right < impact.right);
  }
}
