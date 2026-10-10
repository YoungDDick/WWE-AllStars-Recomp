#include <catch_amalgamated.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <rex/input/device_assignment.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/sdl/sdl_input_driver.h>

#include "fake_pads.h"

using namespace rex::input;
using rex::X_RESULT;
using rex::X_STATUS;

using namespace wwe::test;

TEST_CASE("a non-zero request reaches the pad unchanged and is counted with its peaks") {
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  CHECK(rig.Rumble(0, 30000, 1200) == X_ERROR_SUCCESS);
  CHECK(rig.Rumble(0, 500, 64000) == X_ERROR_SUCCESS);

  REQUIRE(rig.received.size() == 2);
  CHECK(rig.received[0].id == kPadA);
  CHECK(rig.received[0].left == 30000);
  CHECK(rig.received[0].right == 1200);
  CHECK(rig.received[1].left == 500);
  CHECK(rig.received[1].right == 64000);

  const VibrationLedger ledger = rig.input.vibration_ledger();
  CHECK(ledger.requests == 2);
  CHECK(ledger.nonzero == 2);
  CHECK(ledger.peak_left == 30000);
  CHECK(ledger.peak_right == 64000);
  CHECK(ledger.last_left == 500);
  CHECK(ledger.last_right == 64000);
  CHECK(ledger.pad_result == X_ERROR_SUCCESS);
}

TEST_CASE("zero requests count toward the total but not as non-zero") {
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  rig.Rumble(0, 0, 0);
  rig.Rumble(0, 0, 0);
  rig.Rumble(0, 0, 9);

  const VibrationLedger ledger = rig.input.vibration_ledger();
  CHECK(ledger.requests == 3);
  CHECK(ledger.nonzero == 1);
  CHECK(ledger.peak_left == 0);
  CHECK(ledger.peak_right == 9);
}

TEST_CASE("a failing pad beside a succeeding synthetic device gives the pad's error") {
  constexpr X_RESULT kRefused = X_RESULT(0x8007001F);
  Rig rig({{{kPadA, false, kRefused}}, {{kKeyboard, true, X_ERROR_SUCCESS}}});
  CHECK(rig.Rumble(0, 1000, 1000) == kRefused);

  CHECK(rig.received.size() == 2);  // both devices were asked
  CHECK(rig.input.vibration_ledger().pad_result == kRefused);
}

TEST_CASE("user 0xFF reaches user 0's pad and not user 1's") {
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}, {kPadB, false, X_ERROR_SUCCESS}}});
  CHECK(rig.Rumble(0xFF, 4000, 0) == X_ERROR_SUCCESS);
  CHECK(rig.Rumble(0, 5000, 0) == X_ERROR_SUCCESS);

  REQUIRE(rig.received.size() == 2);
  CHECK(rig.received[0].id == kPadA);
  CHECK(rig.received[0].left == 4000);
  CHECK(rig.received[1].id == kPadA);
  CHECK(rig.input.vibration_ledger().last_user == 0);

  CHECK(rig.Rumble(1, 6000, 0) == X_ERROR_SUCCESS);
  REQUIRE(rig.received.size() == 3);
  CHECK(rig.received[2].id == kPadB);
}

TEST_CASE("with no pads the request still counts and the result is not connected") {
  Rig none({});
  CHECK(none.Rumble(0, 7000, 7000) == X_ERROR_DEVICE_NOT_CONNECTED);
  CHECK(none.input.vibration_ledger().requests == 1);
  CHECK(none.input.vibration_ledger().pad_result == X_ERROR_DEVICE_NOT_CONNECTED);

  // A synthetic device accepts the rumble but is not a pad.
  Rig keyboard({{{kKeyboard, true, X_ERROR_SUCCESS}}});
  CHECK(keyboard.Rumble(0, 7000, 7000) == X_ERROR_SUCCESS);
  CHECK(keyboard.input.vibration_ledger().requests == 1);
  CHECK(keyboard.input.vibration_ledger().pad_result == X_ERROR_DEVICE_NOT_CONNECTED);
}

TEST_CASE("the log line names the backend and the counts") {
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}}, {"sdl"});
  rig.Rumble(0, 30000, 1200);
  rig.Rumble(0, 0, 0);
  CHECK(rig.input.VibrationLine(VibrationLineKind::kPeriodic) ==
        "vibration periodic: backend=sdl requests=2 nonzero=1 peak_left=30000 peak_right=1200 "
        "last_user=0 last_left=0 last_right=0 pad_result=success "
        "by_raw_index=0:2/1,1:0/0,2:0/0,3:0/0,ff:0/0,other:0/0");

  Rig refused({{{kPadA, false, X_RESULT(0x8007001F)}}}, {"xinput"});
  refused.Rumble(0, 1, 2);
  CHECK(refused.input.VibrationLine(VibrationLineKind::kFinal) ==
        "vibration final: backend=xinput requests=1 nonzero=1 peak_left=1 peak_right=2 "
        "last_user=0 last_left=1 last_right=2 pad_result=0x8007001F "
        "by_raw_index=0:1/1,1:0/0,2:0/0,3:0/0,ff:0/0,other:0/0");
}

TEST_CASE("the log line appears only when something changed") {
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  CHECK_FALSE(rig.input.LogVibration(VibrationLineKind::kPeriodic));  // nothing asked yet

  rig.Rumble(0, 0, 0);
  CHECK(rig.input.LogVibration(VibrationLineKind::kPeriodic));  // the first request is news
  CHECK_FALSE(rig.input.LogVibration(VibrationLineKind::kPeriodic));

  rig.Rumble(0, 0, 0);  // the same zero again, as a game does every frame
  CHECK_FALSE(rig.input.LogVibration(VibrationLineKind::kPeriodic));

  rig.Rumble(0, 100, 0);
  CHECK(rig.input.LogVibration(VibrationLineKind::kPeriodic));
  CHECK_FALSE(rig.input.LogVibration(VibrationLineKind::kPeriodic));

  rig.Rumble(0, 0, 0);  // back to rest
  CHECK(rig.input.LogVibration(VibrationLineKind::kPeriodic));
}

TEST_CASE("shutting down stops every real pad on every driver once") {
  constexpr DeviceId kPadC = DeviceId{4};
  Rig rig({{{kKeyboard, true, X_ERROR_SUCCESS}, {kPadA, false, X_ERROR_SUCCESS}},
           {{kPadB, false, X_ERROR_DEVICE_NOT_CONNECTED}},
           {{kPadC, false, X_ERROR_SUCCESS}}});
  rig.Rumble(0, 20000, 20000);
  rig.received.clear();

  rig.input.Shutdown();

  // The keyboard is skipped, and pad B's "not connected" does not stop pad C.
  REQUIRE(rig.received.size() == 3);
  CHECK(rig.received[0].id == kPadA);
  CHECK(rig.received[1].id == kPadB);
  CHECK(rig.received[2].id == kPadC);
  for (const auto& r : rig.received) {
    CHECK(r.left == 0);
    CHECK(r.right == 0);
  }
}

TEST_CASE("a request for a user with no pad does not hide the pad's result") {
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  CHECK(rig.Rumble(0, 3000, 3000) == X_ERROR_SUCCESS);
  CHECK(rig.Rumble(1, 3000, 3000) == X_ERROR_DEVICE_NOT_CONNECTED);

  const VibrationLedger ledger = rig.input.vibration_ledger();
  CHECK(ledger.requests == 2);
  CHECK(ledger.last_user == 1);
  CHECK(ledger.pad_result == X_ERROR_SUCCESS);
}

TEST_CASE("a request with no device assignment still counts, as not connected") {
  InputSystem input{nullptr};
  X_INPUT_VIBRATION vibration;
  vibration.left_motor_speed = 800;
  vibration.right_motor_speed = 0;
  CHECK(input.SetState(0, &vibration) == X_ERROR_DEVICE_NOT_CONNECTED);

  const VibrationLedger ledger = input.vibration_ledger();
  CHECK(ledger.requests == 1);
  CHECK(ledger.nonzero == 1);
  CHECK(ledger.peak_left == 800);
  CHECK(ledger.pad_result == X_ERROR_DEVICE_NOT_CONNECTED);
}

TEST_CASE("after the pads are stopped, later requests do not restart them") {
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}});
  rig.Rumble(0, 20000, 20000);
  rig.input.StopVibration();
  rig.received.clear();

  // A guest thread still running on the window-X path keeps asking.
  rig.Rumble(0, 30000, 30000);
  CHECK(rig.received.empty());
}

TEST_CASE("the backend is the pad driver that was added, or none") {
  // SDL failed to set up, so only the keyboard driver (no pads) was added.
  Rig fallback({{{kKeyboard, true, X_ERROR_SUCCESS}}});
  CHECK(fallback.input.vibration_backend() == "none");

  Rig sdl({{{kPadA, false, X_ERROR_SUCCESS}}, {{kKeyboard, true, X_ERROR_SUCCESS}}}, {"sdl"});
  CHECK(sdl.input.vibration_backend() == "sdl");

  Rig both({{{kPadA, false, X_ERROR_SUCCESS}}, {{kPadB, false, X_ERROR_SUCCESS}}},
           {"xinput", "script"});
  CHECK(both.input.vibration_backend() == "xinput+script");
}

TEST_CASE("requests are counted by raw player index, 0xFF apart from 0") {
  Rig rig({{{kPadA, false, X_ERROR_SUCCESS}}}, {"sdl"});
  rig.Rumble(0xFF, 0, 0);
  rig.Rumble(0xFF, 9000, 9000);
  rig.Rumble(0, 4000, 0);
  rig.Rumble(2, 0, 0);
  // XamInputSetState pins 0xFF to user 0 before the input system sees it, so
  // it passes the raw index alongside.
  X_INPUT_VIBRATION vibration;
  vibration.left_motor_speed = 0;
  vibration.right_motor_speed = 0;
  rig.input.SetState(0, &vibration, 0xFF);

  const VibrationLedger ledger = rig.input.vibration_ledger();
  CHECK(ledger.ForRawIndex(0xFF).requests == 3);
  CHECK(ledger.ForRawIndex(0xFF).nonzero == 1);
  CHECK(ledger.ForRawIndex(0).requests == 1);
  CHECK(ledger.ForRawIndex(0).nonzero == 1);
  CHECK(ledger.ForRawIndex(2).requests == 1);
  CHECK(ledger.ForRawIndex(1).requests == 0);
  CHECK(rig.input.VibrationLine(VibrationLineKind::kPeriodic) ==
        "vibration periodic: backend=sdl requests=5 nonzero=2 peak_left=9000 peak_right=9000 "
        "last_user=0 last_left=0 last_right=0 pad_result=success "
        "by_raw_index=0:1/1,1:0/0,2:1/0,3:0/0,ff:3/1,other:0/0");
}

TEST_CASE("each non-zero request and the zero that ends its pulse are traced") {
  using namespace std::chrono_literals;
  VibrationTrace trace;
  const auto start = VibrationTrace::Clock::time_point{} + 100s;
  // A zero at rest says nothing, but the first request starts the clock.
  CHECK_FALSE(trace.Take(0, 0, 0, 0, X_ERROR_SUCCESS, start));

  CHECK(trace.Take(0xFF, 0, 54583, 54583, X_ERROR_SUCCESS, start + 500ms) ==
        "vibration request: t=0.500s raw_index=ff user=0 left=54583 right=54583 "
        "pad_result=success");
  // Player 0's zero does not end the 0xFF pulse.
  CHECK_FALSE(trace.Take(0, 0, 0, 0, X_ERROR_SUCCESS, start + 510ms));
  CHECK(trace.Take(0xFF, 0, 100, 0, std::nullopt, start + 517ms) ==
        "vibration request: t=0.517s raw_index=ff user=0 left=100 right=0 pad_result=no_pad");
  CHECK(trace.Take(0xFF, 0, 0, 0, X_ERROR_SUCCESS, start + 584ms) ==
        "vibration request: t=0.584s raw_index=ff user=0 left=0 right=0 pad_result=success "
        "pulse_end pulse_ms=84 pulse_requests=2");
  CHECK_FALSE(trace.Take(0xFF, 0, 0, 0, X_ERROR_SUCCESS, start + 600ms));
}

TEST_CASE("the request trace stops after its cap with one notice") {
  VibrationTrace trace;
  const auto now = VibrationTrace::Clock::time_point{};
  for (uint32_t i = 0; i < VibrationTrace::kMaxLines; ++i) {
    REQUIRE(trace.Take(1, 1, 1000, 0, X_ERROR_SUCCESS, now));
  }
  CHECK(trace.Take(1, 1, 1000, 0, X_ERROR_SUCCESS, now) ==
        "vibration trace: 300 lines logged, later requests are only counted");
  CHECK_FALSE(trace.Take(1, 1, 1000, 0, X_ERROR_SUCCESS, now));
  CHECK_FALSE(trace.Take(1, 1, 0, 0, X_ERROR_SUCCESS, now));  // nor the pulse end
}

// Closing with the window's X runs OnClosing inside SDL's event watch, which
// holds SDL's watcher lock; any SDL_PushEvent from another thread, such as a
// queued pump for the UI thread, then waits for the close that waits for it.
// So a vibration request must not depend on the UI thread at all. A driver
// with no window has no UI thread to reach, and still answers.
TEST_CASE("an SDL pad vibration request needs no UI thread") {
  rex::input::sdl::SDLInputDriver driver(nullptr, 0);
  X_INPUT_VIBRATION zero = {};
  CHECK(driver.SetDeviceVibration(kPadA, &zero) == X_ERROR_DEVICE_NOT_CONNECTED);
}
