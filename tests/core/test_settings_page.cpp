#include <catch_amalgamated.hpp>

#include "settings/page_model.h"

using wwe::settings::Change;
using wwe::settings::Pad;
using wwe::settings::PageModel;
using wwe::settings::Setting;

namespace {

constexpr uint16_t kUp = PageModel::kUp, kDown = PageModel::kDown, kLeft = PageModel::kLeft,
                   kRight = PageModel::kRight, kStart = PageModel::kStart,
                   kBack = PageModel::kBack, kA = PageModel::kA, kB = PageModel::kB;

std::vector<Setting> TwoSettings() {
  return {
      {"audio_mute", "Mute", "GRAPHICS", {{"false", "Off"}, {"true", "On"}}, "Silences the game."},
      {"wwe_native_cas", "Sharpening", "GRAPHICS",
       {{"0", "Off"}, {"0.25", "Low"}, {"0.5", "Medium"}, {"1", "High"}}, "Sharpens."},
  };
}

Pad Press(uint16_t buttons) { return Pad{buttons, 0, 0}; }
const Pad kIdle = {0, 0, 0};

// Opens the page with the A that opened it still held, then lets go.
PageModel OpenedAndReleased(std::vector<std::string> values = {"false", "0.5"}) {
  PageModel page(TwoSettings());
  page.Open(values, 0);
  page.Update(Press(kA), 10);
  page.Update(kIdle, 200);
  return page;
}

}  // namespace

TEST_CASE("the press that opened the page changes nothing") {
  PageModel page(TwoSettings());
  page.Open({"false", "0.5"}, 0);
  CHECK(page.is_open());
  CHECK(page.holds_input());
  CHECK(page.Update(Press(kA), 10).empty());
  CHECK(page.Update(Press(kRight), 300).empty());  // still not released
  page.Update(kIdle, 320);
  const std::vector<Change> changes = page.Update(Press(kRight), 340);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].name == "audio_mute");
  CHECK(changes[0].value == "true");
}

TEST_CASE("input is ignored until 150 ms after opening even if nothing is held") {
  PageModel page(TwoSettings());
  page.Open({"false", "0.5"}, 1000);
  page.Update(kIdle, 1010);
  CHECK(page.Update(Press(kRight), 1050).empty());
  page.Update(kIdle, 1160);
  CHECK(page.Update(Press(kRight), 1170).size() == 1);
}

TEST_CASE("the page opens showing each setting's current value") {
  PageModel page = OpenedAndReleased({"true", "0.25"});
  CHECK(page.row() == 0);
  CHECK(page.choice(0) == 1);  // On
  CHECK(page.choice(1) == 1);  // Low
}

TEST_CASE("left and right step through a setting's values and wrap") {
  PageModel page = OpenedAndReleased();
  auto changes = page.Update(Press(kRight), 300);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].value == "true");
  page.Update(kIdle, 310);
  changes = page.Update(Press(kRight), 320);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].value == "false");  // wrapped
  page.Update(kIdle, 330);
  changes = page.Update(Press(kLeft), 340);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].value == "true");
}

TEST_CASE("A steps the selected value forward") {
  PageModel page = OpenedAndReleased();
  auto changes = page.Update(Press(kA), 300);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].value == "true");
  CHECK(page.choice(0) == 1);
}

TEST_CASE("up and down move between rows and wrap") {
  PageModel page = OpenedAndReleased();
  page.Update(Press(kDown), 300);
  CHECK(page.row() == 1);
  page.Update(kIdle, 310);
  page.Update(Press(kDown), 320);
  CHECK(page.row() == 0);
  page.Update(kIdle, 330);
  page.Update(Press(kUp), 340);
  CHECK(page.row() == 1);
  page.Update(kIdle, 350);
  const auto changes = page.Update(Press(kRight), 360);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].name == "wwe_native_cas");
  CHECK(changes[0].value == "1");
}

TEST_CASE("the left stick moves like the D-pad outside its dead zone") {
  PageModel page = OpenedAndReleased();
  page.Update(Pad{0, 0, -10000}, 300);  // inside the dead zone
  CHECK(page.row() == 0);
  page.Update(Pad{0, 0, -30000}, 320);  // down
  CHECK(page.row() == 1);
  page.Update(kIdle, 330);
  const auto changes = page.Update(Pad{0, 30000, 0}, 340);  // right
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].value == "1");
}

TEST_CASE("a held direction repeats after a delay, then steadily") {
  PageModel page = OpenedAndReleased();
  page.Update(Press(kDown), 1000);  // row 1
  page.Update(Press(kDown), 1300);  // held, before the delay
  CHECK(page.row() == 1);
  page.Update(Press(kDown), 1400);  // 400 ms: first repeat
  CHECK(page.row() == 0);
  page.Update(Press(kDown), 1500);  // 100 ms later: no repeat yet
  CHECK(page.row() == 0);
  page.Update(Press(kDown), 1530);  // 130 ms after the first repeat
  CHECK(page.row() == 1);
}

TEST_CASE("buttons do not repeat while held") {
  PageModel page = OpenedAndReleased();
  CHECK(page.Update(Press(kA), 1000).size() == 1);
  CHECK(page.Update(Press(kA), 1500).empty());
  CHECK(page.Update(Press(kA), 2000).empty());
}

TEST_CASE("B, Start and Back close the page and keep input held until released") {
  for (uint16_t close : {kB, kStart, kBack}) {
    PageModel page = OpenedAndReleased();
    page.Update(Press(close), 300);
    CHECK_FALSE(page.is_open());
    CHECK(page.holds_input());
    page.Update(Press(close), 400);
    CHECK(page.holds_input());
    page.Update(kIdle, 500);
    CHECK_FALSE(page.holds_input());
  }
}

TEST_CASE("a closed page ignores the pad") {
  PageModel page = OpenedAndReleased();
  page.Update(Press(kB), 300);
  page.Update(kIdle, 400);
  CHECK(page.Update(Press(kRight), 500).empty());
  CHECK_FALSE(page.is_open());
}

TEST_CASE("closing from outside (F4 again) keeps input held until the pad is released") {
  PageModel page = OpenedAndReleased();
  page.Update(Press(kRight), 300);
  page.Close();
  CHECK_FALSE(page.is_open());
  CHECK(page.holds_input());
  page.Update(Press(kRight), 400);
  CHECK(page.holds_input());
  page.Update(kIdle, 500);
  CHECK_FALSE(page.holds_input());
}

namespace {

constexpr uint16_t kLB = PageModel::kLB, kRB = PageModel::kRB;

std::vector<Setting> ThreeTabs() {
  Setting renderer{"wwe_native_renderer", "Renderer", "GRAPHICS",
                   {{"false", "Emulated"}, {"true", "Native"}}, "Which renderer draws."};
  renderer.restart = true;
  return {
      {"fullscreen", "Display mode", "DISPLAY", {{"false", "Windowed"}, {"true", "Fullscreen"}},
       "Window or full screen."},
      renderer,
      {"wwe_native_cas", "Sharpening", "GRAPHICS", {{"0", "Off"}, {"0.5", "Medium"}}, "Sharpens."},
      {"audio_mute", "Mute", "AUDIO", {{"false", "Off"}, {"true", "On"}}, "Silences."},
  };
}

PageModel OpenTabs() {
  PageModel page(ThreeTabs());
  page.Open({"true", "true", "0.5", "false"}, 0);
  page.Update(kIdle, 200);
  return page;
}

}  // namespace

TEST_CASE("tabs follow the order settings first name them") {
  PageModel page = OpenTabs();
  REQUIRE(page.tabs() == std::vector<std::string>{"DISPLAY", "GRAPHICS", "AUDIO"});
  CHECK(page.tab() == 0);
  CHECK(page.rows() == std::vector<size_t>{0});
}

TEST_CASE("RB and LB switch tabs, wrap, and start each tab on its first row") {
  PageModel page = OpenTabs();
  page.Update(Press(kRB), 300);
  CHECK(page.tab() == 1);
  CHECK(page.rows() == std::vector<size_t>{1, 2});
  page.Update(kIdle, 310);
  page.Update(Press(kDown), 320);  // second GRAPHICS row: sharpening
  page.Update(kIdle, 330);
  auto changes = page.Update(Press(kRight), 340);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].name == "wwe_native_cas");
  page.Update(kIdle, 350);
  page.Update(Press(kRB), 360);
  CHECK(page.tab() == 2);
  page.Update(kIdle, 370);
  page.Update(Press(kRB), 380);
  CHECK(page.tab() == 0);  // wrapped
  page.Update(kIdle, 390);
  page.Update(Press(kLB), 400);
  CHECK(page.tab() == 2);
  changes = page.Update(kIdle, 410);
  changes = page.Update(Press(kRight), 420);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].name == "audio_mute");
}

TEST_CASE("a restart setting is marked once changed, and unmarked when changed back") {
  PageModel page = OpenTabs();
  page.Update(Press(kRB), 300);
  page.Update(kIdle, 310);
  CHECK_FALSE(page.needs_restart(1));
  page.Update(Press(kRight), 320);  // renderer Native -> Emulated
  CHECK(page.needs_restart(1));
  page.Update(kIdle, 330);
  page.Update(Press(kRight), 340);  // back to Native
  CHECK_FALSE(page.needs_restart(1));
}

TEST_CASE("a live setting is never marked") {
  PageModel page = OpenTabs();
  page.Update(Press(kRight), 300);  // fullscreen, live
  CHECK_FALSE(page.needs_restart(0));
}

TEST_CASE("a number the cvar reports in another form still finds its choice") {
  PageModel page(ThreeTabs());
  page.Open({"true", "true", "0.500000", "false"}, 0);
  CHECK(page.choice(2) == 1);  // Medium
}

namespace {
constexpr uint16_t kL3 = 0x0040, kR3 = 0x0080;  // XInput's LEFT_THUMB and RIGHT_THUMB
static_assert((kL3 | kR3) == PageModel::kCombo);
}  // namespace

TEST_CASE("clicking both sticks asks to open the page once per press") {
  wwe::settings::ComboWatch combo;
  CHECK_FALSE(combo.Update(Press(kL3)));        // one stick alone
  CHECK(combo.Update(Press(kL3 | kR3)));        // both: open
  CHECK_FALSE(combo.Update(Press(kL3 | kR3)));  // still held: once only
  CHECK_FALSE(combo.Update(Press(kR3)));
  CHECK_FALSE(combo.Update(kIdle));
  CHECK(combo.Update(Press(kL3 | kR3)));        // a new press
}

TEST_CASE("the combo's buttons stay held from the game after it opens the page") {
  PageModel page(TwoSettings());
  page.Open({"false", "0.5"}, 0);
  CHECK(page.Update(Press(kL3 | kR3 | kRight), 300).empty());
  CHECK(page.is_open());
  CHECK(page.holds_input());
  page.Update(kIdle, 320);
  CHECK(page.Update(Press(kRight), 340).size() == 1);
}

TEST_CASE("the combo again closes the page") {
  PageModel page = OpenedAndReleased();
  page.Update(Press(kL3 | kR3), 300);
  CHECK_FALSE(page.is_open());
  CHECK(page.holds_input());
  page.Update(kIdle, 400);
  CHECK_FALSE(page.holds_input());
}

TEST_CASE("a restart setting changed earlier stays marked when the page reopens") {
  // Renderer was changed to Emulated and saved, but Native still runs.
  PageModel page(ThreeTabs());
  page.Open({"true", "false", "0.5", "false"}, 0, {"true", "true", "0.5", "false"});
  CHECK(page.choice(1) == 0);  // Emulated shown
  CHECK(page.needs_restart(1));
  CHECK_FALSE(page.needs_restart(0));
}

TEST_CASE("a value no choice names is shown as it is and kept until changed") {
  std::vector<Setting> settings = {
      {"wwe_native_anisotropy", "Texture filtering", "GRAPHICS", {{"0", "Off"}, {"3", "4x"}},
       "Filtering."},
  };
  PageModel page(settings);
  page.Open({"-1"}, 0);
  CHECK(page.settings()[0].choices[page.choice(0)].label == "-1");
  page.Update(kIdle, 200);
  const auto changes = page.Update(Press(kRight), 300);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].value == "0");
}

TEST_CASE("each row says when a change applies") {
  std::vector<Setting> settings = ThreeTabs();  // fullscreen live, renderer restart
  settings[0].pinned = false;
  PageModel page(settings);
  page.Open({"true", "true", "0.5", "false"}, 0);
  page.Update(kIdle, 200);
  CHECK(page.ApplyNote(0).empty());                                       // live
  CHECK(page.ApplyNote(1) == "Applies the next time the game starts.");  // restart, unchanged
  page.Update(Press(kRB), 300);
  page.Update(kIdle, 310);
  page.Update(Press(kRight), 320);
  CHECK(page.ApplyNote(1) == "* Applies the next time the game starts.");
}

TEST_CASE("a setting pinned by the launch options says so") {
  std::vector<Setting> settings = ThreeTabs();
  settings[1].pinned = true;  // --wwe_native_renderer on the command line
  settings[0].pinned = true;  // a live setting pinned too
  PageModel page(settings);
  page.Open({"true", "true", "0.5", "false"}, 0);
  CHECK(page.ApplyNote(1) ==
        "Set by launch options: a change here does not apply at the next start.");
  CHECK(page.ApplyNote(0) ==
        "Set by launch options: a change here lasts until the game closes.");
}
