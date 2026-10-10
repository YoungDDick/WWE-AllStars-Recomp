#include <catch_amalgamated.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "script_input.h"

using namespace rex::input;
using rex::X_RESULT;
using std::chrono::milliseconds;

namespace {

// A command file in the temp directory plus a driver reading it on a
// hand-advanced clock and frame count.
struct Session {
  std::filesystem::path file;
  milliseconds now{0};
  uint64_t frame = 0;
  wwe::ScriptInputDriver driver;

  explicit Session(const char* name)
      : file(Reset(std::filesystem::temp_directory_path() / name)),
        driver(file, [this] { return now; }, [this] { return frame; }) {}

  static std::filesystem::path Reset(std::filesystem::path path) {
    std::ofstream(path, std::ios::binary | std::ios::trunc);
    return path;
  }

  void Append(const std::string& text) {
    std::ofstream(file, std::ios::binary | std::ios::app) << text;
  }

  void Truncate(const std::string& text) {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
  }

  // Advance the clock, then poll the pad as the guest does.
  X_INPUT_STATE At(int64_t ms) {
    now = milliseconds(ms);
    return Poll();
  }

  // Advance the frame count only.
  X_INPUT_STATE AtFrame(uint64_t f) {
    frame = f;
    return Poll();
  }

  X_INPUT_STATE Poll() {
    X_INPUT_STATE state{};
    REQUIRE(driver.GetDeviceState(wwe::ScriptInputDriver::kDevice, &state) ==
            X_ERROR_SUCCESS);
    return state;
  }
};

}  // namespace

TEST_CASE("press holds the buttons, then releases them automatically") {
  Session s("wwe_script_press.txt");
  s.Append("press A+START 200\n");
  CHECK(s.At(0).gamepad.buttons == (X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_START));
  CHECK(s.At(199).gamepad.buttons == (X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_START));
  CHECK(s.At(200).gamepad.buttons == 0);
}

TEST_CASE("each step replaces the whole pad state of the one before") {
  Session s("wwe_script_replace.txt");
  s.Append("stick L -40000 1234 100 LB 300\n"
           "trigger L 128 100\n"
           "wait 50\n");
  auto stick = s.At(0).gamepad;
  CHECK(stick.thumb_lx == -32768);  // clamped
  CHECK(stick.thumb_ly == 1234);
  CHECK(stick.buttons == X_INPUT_GAMEPAD_LEFT_SHOULDER);
  CHECK(stick.right_trigger == 255);  // clamped

  auto trigger = s.At(100).gamepad;
  CHECK(trigger.left_trigger == 128);
  CHECK(trigger.thumb_lx == 0);
  CHECK(trigger.buttons == 0);
  CHECK(trigger.right_trigger == 0);

  auto wait = s.At(200).gamepad;
  CHECK(wait.left_trigger == 0);
  CHECK(wait.buttons == 0);
}

TEST_CASE("the right stick and a '-' button list") {
  Session s("wwe_script_right.txt");
  s.Append("stick r 100 -200 100 - 7\n");
  auto pad = s.At(0).gamepad;
  CHECK(pad.thumb_rx == 100);
  CHECK(pad.thumb_ry == -200);
  CHECK(pad.thumb_lx == 0);
  CHECK(pad.buttons == 0);
  CHECK(pad.right_trigger == 7);
}

TEST_CASE("an empty queue gives a neutral pad") {
  Session s("wwe_script_empty.txt");
  auto pad = s.At(0);
  CHECK(pad.gamepad.buttons == 0);
  CHECK(pad.gamepad.thumb_lx == 0);
  CHECK(pad.gamepad.left_trigger == 0);
  // The packet number still moves, as a connected pad's does.
  CHECK(s.At(10).packet_number > pad.packet_number);
}

TEST_CASE("a partly written last line waits for its newline") {
  Session s("wwe_script_partial.txt");
  s.Append("press B 500");
  CHECK(s.At(0).gamepad.buttons == 0);
  s.Append("\n");
  CHECK(s.At(100).gamepad.buttons == X_INPUT_GAMEPAD_B);
}

TEST_CASE("lines appended later are read at the next poll, not before") {
  Session s("wwe_script_tail.txt");
  CHECK(s.At(0).gamepad.buttons == 0);
  s.Append("press X 500\n");
  CHECK(s.At(50).gamepad.buttons == 0);  // inside the 100 ms poll interval
  CHECK(s.At(100).gamepad.buttons == X_INPUT_GAMEPAD_X);
}

TEST_CASE("a shrunk file is read again from the start") {
  Session s("wwe_script_shrunk.txt");
  s.Append("# a long comment line that makes the file longer than its replacement\n");
  CHECK(s.At(0).gamepad.buttons == 0);
  s.Truncate("press Y 500\n");
  CHECK(s.At(100).gamepad.buttons == X_INPUT_GAMEPAD_Y);
}

TEST_CASE("comments, blank lines and unknown verbs and buttons are skipped") {
  Session s("wwe_script_unknown.txt");
  s.Append("# press A\n"
           "\n"
           "type hello\n"
           "touch 0.5 0.5\n"
           "PRESS a+NOPE 300\r\n");
  CHECK(s.At(0).gamepad.buttons == X_INPUT_GAMEPAD_A);
  CHECK(s.At(299).gamepad.buttons == X_INPUT_GAMEPAD_A);
  CHECK(s.At(300).gamepad.buttons == 0);
}

TEST_CASE("only the script device answers") {
  Session s("wwe_script_device.txt");
  X_INPUT_STATE state{};
  CHECK(s.driver.GetDeviceState(static_cast<DeviceId>(1), &state) ==
        X_ERROR_DEVICE_NOT_CONNECTED);
  std::vector<DeviceInfo> devices;
  s.driver.EnumerateDevices(devices);
  REQUIRE(devices.size() == 1);
  CHECK(devices[0].id == wwe::ScriptInputDriver::kDevice);
  CHECK_FALSE(devices[0].synthetic);  // takes ordinal 0, so it is user 0
  X_INPUT_KEYSTROKE stroke{};
  CHECK(s.driver.GetDeviceKeystroke(wwe::ScriptInputDriver::kDevice, 0xFF, &stroke) ==
        X_ERROR_EMPTY);
}

TEST_CASE("press without a duration lasts 120 ms, trailing blanks or not") {
  Session s("wwe_script_default.txt");
  s.Append("press A\npress B \t\n");
  CHECK(s.At(0).gamepad.buttons == X_INPUT_GAMEPAD_A);
  CHECK(s.At(119).gamepad.buttons == X_INPUT_GAMEPAD_A);
  CHECK(s.At(120).gamepad.buttons == 0);  // automatic release, 120 ms
  CHECK(s.At(240).gamepad.buttons == X_INPUT_GAMEPAD_B);
  CHECK(s.At(359).gamepad.buttons == X_INPUT_GAMEPAD_B);
  CHECK(s.At(360).gamepad.buttons == 0);
}

TEST_CASE("a frame-suffixed press ends when the frame count reaches start + n") {
  Session s("wwe_script_press_frames.txt");
  s.frame = 100;
  s.Append("press A 6f\n");
  CHECK(s.At(0).gamepad.buttons == X_INPUT_GAMEPAD_A);  // starts at frame 100
  CHECK(s.At(5000).gamepad.buttons == X_INPUT_GAMEPAD_A);  // wall time does not end it
  CHECK(s.AtFrame(105).gamepad.buttons == X_INPUT_GAMEPAD_A);
  CHECK(s.AtFrame(106).gamepad.buttons == 0);
}

TEST_CASE("at holds a neutral pad until the guest has presented that frame") {
  Session s("wwe_script_at.txt");
  s.frame = 10;
  s.Append("at 50\npress B 3f\n");
  CHECK(s.At(0).gamepad.buttons == 0);
  CHECK(s.AtFrame(49).gamepad.buttons == 0);
  CHECK(s.AtFrame(50).gamepad.buttons == X_INPUT_GAMEPAD_B);  // released at frame 50
  CHECK(s.AtFrame(52).gamepad.buttons == X_INPUT_GAMEPAD_B);
  CHECK(s.AtFrame(53).gamepad.buttons == 0);
}

TEST_CASE("at a frame already past is a no-op") {
  Session s("wwe_script_at_past.txt");
  s.frame = 500;
  s.Append("at 120\npress Y 3f\n");
  CHECK(s.At(0).gamepad.buttons == X_INPUT_GAMEPAD_Y);  // straight to the press
}

TEST_CASE("mixed ms and frame steps run in order; plain numbers stay ms") {
  Session s("wwe_script_mixed.txt");
  s.Append("wait 2f\nhold X 100\nwait 5f\npress A\n");
  CHECK(s.At(0).gamepad.buttons == 0);
  CHECK(s.At(1000).gamepad.buttons == 0);  // a frame wait ignores wall time
  CHECK(s.AtFrame(2).gamepad.buttons == X_INPUT_GAMEPAD_X);  // hold starts at t=1000
  CHECK(s.AtFrame(100).gamepad.buttons == X_INPUT_GAMEPAD_X);  // and ignores frames
  CHECK(s.At(1099).gamepad.buttons == X_INPUT_GAMEPAD_X);
  CHECK(s.At(1100).gamepad.buttons == 0);  // 120 ms release
  CHECK(s.At(1220).gamepad.buttons == 0);  // wait 5f starts at frame 100
  CHECK(s.AtFrame(104).gamepad.buttons == 0);
  CHECK(s.AtFrame(105).gamepad.buttons == X_INPUT_GAMEPAD_A);
}

TEST_CASE("hold and at take frame counts too; a frame press releases for 7 frames") {
  Session s("wwe_script_frames_more.txt");
  s.Append("at 20f\nhold LB 4f\npress RB 2f\n");
  CHECK(s.At(0).gamepad.buttons == 0);
  CHECK(s.AtFrame(20).gamepad.buttons == X_INPUT_GAMEPAD_LEFT_SHOULDER);
  CHECK(s.AtFrame(24).gamepad.buttons == 0);  // hold's own release, 7 frames
  CHECK(s.AtFrame(30).gamepad.buttons == 0);
  CHECK(s.AtFrame(31).gamepad.buttons == X_INPUT_GAMEPAD_RIGHT_SHOULDER);
}

TEST_CASE("a bad duration keeps the default; an at without a frame is dropped") {
  Session s("wwe_script_bad.txt");
  s.frame = 0;
  s.Append("at soon\npress A # trailing comment\n");
  CHECK(s.At(0).gamepad.buttons == X_INPUT_GAMEPAD_A);  // no `at 0` placeholder first
  CHECK(s.At(119).gamepad.buttons == X_INPUT_GAMEPAD_A);
  CHECK(s.At(120).gamepad.buttons == 0);
}
