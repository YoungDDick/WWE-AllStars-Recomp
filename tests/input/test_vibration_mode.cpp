#include <catch_amalgamated.hpp>

#include <rex/cvar.h>

#include "fake_pads.h"
#include "vibration/mode.h"

using namespace rex::input;
using namespace wwe::test;

namespace {

// Restores the default mode and strength when a test ends, so the next one
// starts there.
struct ModeGuard {
  ~ModeGuard() {
    rex::cvar::ResetToDefault("wwe_vibration_mode");
    rex::cvar::ResetToDefault("wwe_vibration_strength");
  }
};

}  // namespace

TEST_CASE("the vibration mode is Enhanced by default") {
  CHECK(wwe::vibration::CurrentMode() == wwe::vibration::Mode::kEnhanced);
}

TEST_CASE("with the mode Off a request reaches the pad as zeros and still counts") {
  ModeGuard guard;
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  wwe::vibration::Configure(&rig.input);
  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_mode", "0"));

  CHECK(rig.Rumble(0, 54583, 30000) == X_ERROR_SUCCESS);

  REQUIRE(rig.received.size() == 1);
  CHECK(rig.received[0].id == kPadA);
  CHECK(rig.received[0].left == 0);
  CHECK(rig.received[0].right == 0);
  const VibrationLedger ledger = rig.input.vibration_ledger();
  CHECK(ledger.nonzero == 1);
  CHECK(ledger.peak_left == 54583);
  CHECK(ledger.peak_right == 30000);
}

TEST_CASE("a mode change applies to the next request") {
  ModeGuard guard;
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  wwe::vibration::Configure(&rig.input);

  rig.Rumble(0, 1000, 2000);
  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_mode", "0"));
  rig.Rumble(0, 1000, 2000);
  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_mode", "1"));
  rig.Rumble(0, 1000, 2000);

  REQUIRE(rig.received.size() == 3);
  CHECK(rig.received[0].left == 1000);
  CHECK(rig.received[1].left == 0);
  CHECK(rig.received[1].right == 0);
  CHECK(rig.received[2].left == 1000);
  CHECK(rig.received[2].right == 2000);
  CHECK(rig.input.vibration_ledger().nonzero == 3);
}

TEST_CASE("vibration strength is full by default") {
  ModeGuard guard;
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  wwe::vibration::Configure(&rig.input);

  rig.Rumble(0, 54583, 30000);

  REQUIRE(rig.received.size() == 1);
  CHECK(rig.received[0].left == 54583);
  CHECK(rig.received[0].right == 30000);
}

TEST_CASE("at 50 % strength the pad gets a quarter of the request and the ledger all of it") {
  // The perceptual curve: strength squared, so 50 % drives the motors at 25 %.
  ModeGuard guard;
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  wwe::vibration::Configure(&rig.input);
  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_strength", "50"));

  rig.Rumble(0, 40000, 20000);

  REQUIRE(rig.received.size() == 1);
  CHECK(rig.received[0].left == 10000);
  CHECK(rig.received[0].right == 5000);
  const VibrationLedger ledger = rig.input.vibration_ledger();
  CHECK(ledger.peak_left == 40000);
  CHECK(ledger.peak_right == 20000);
}

TEST_CASE("a strength change applies to the next request") {
  ModeGuard guard;
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  wwe::vibration::Configure(&rig.input);

  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_strength", "25"));
  rig.Rumble(0, 40000, 65535);
  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_strength", "0"));
  rig.Rumble(0, 40000, 65535);
  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_strength", "100"));
  rig.Rumble(0, 40000, 65535);

  REQUIRE(rig.received.size() == 3);
  CHECK(rig.received[0].left == 2500);
  CHECK(rig.received[0].right == 4096);  // 65535 / 16, rounded
  CHECK(rig.received[1].left == 0);
  CHECK(rig.received[1].right == 0);
  CHECK(rig.received[2].left == 40000);
  CHECK(rig.received[2].right == 65535);
}

TEST_CASE("Off silences the pads whatever the strength") {
  ModeGuard guard;
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  wwe::vibration::Configure(&rig.input);
  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_strength", "100"));
  REQUIRE(rex::cvar::SetFlagByName("wwe_vibration_mode", "0"));

  rig.Rumble(0, 40000, 20000);

  REQUIRE(rig.received.size() == 1);
  CHECK(rig.received[0].left == 0);
  CHECK(rig.received[0].right == 0);
}
