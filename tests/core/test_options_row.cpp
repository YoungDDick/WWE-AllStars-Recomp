#include <catch_amalgamated.hpp>

#include <cstdint>
#include <vector>

#include "settings/options_row.h"

using wwe::settings::OptionsRow;
using wwe::settings::PatchOptionsRows;

namespace {

// fe_options frame-1 action buffer around offset 0xD03, as
// docs/research/options-menu-gfx.md "The movie patch" lists it.
const std::vector<uint8_t> kOriginalHead = {0x96, 0x0F, 0x00, 0x08, 0x23, 0x08, 0x2D, 0x08, 0x2C,
                                            0x08, 0x2B, 0x07, 0x03, 0x00, 0x00, 0x00, 0x08, 0x1A,
                                            0x40, 0x1D};
const std::vector<uint8_t> kTraces = {0x96, 0x02, 0x00, 0x08, 0x41, 0x26, 0x96, 0x02, 0x00,
                                      0x08, 0x42, 0x26, 0x96, 0x02, 0x00, 0x08, 0x43, 0x26};
const std::vector<uint8_t> kPatchedHead = {0x96, 0x11, 0x00, 0x08, 0x23, 0x08, 0x35, 0x08, 0x2D,
                                           0x08, 0x2C, 0x08, 0x2B, 0x07, 0x04, 0x00, 0x00, 0x00,
                                           0x08, 0x1A, 0x40, 0x1D};
const std::vector<uint8_t> kFiller = {0x96, 0x02, 0x00, 0x08, 0x48, 0x4C, 0x26, 0x26,
                                      0x96, 0x02, 0x00, 0x08, 0x48, 0x4C, 0x26, 0x26};

std::vector<uint8_t> Middle() {
  std::vector<uint8_t> middle(39);
  for (size_t i = 0; i < middle.size(); ++i) middle[i] = uint8_t(0xA0 + i);
  return middle;
}

std::vector<uint8_t> FeOptionsBuffer() {
  std::vector<uint8_t> buffer(4587, 0x00);
  std::vector<uint8_t> region = kOriginalHead;
  const auto middle = Middle();
  region.insert(region.end(), middle.begin(), middle.end());
  region.insert(region.end(), kTraces.begin(), kTraces.end());
  std::copy(region.begin(), region.end(), buffer.begin() + 0xD03);
  return buffer;
}

}  // namespace

TEST_CASE("fe_options gets a fourth row key and keeps its length") {
  std::vector<uint8_t> buffer = FeOptionsBuffer();
  REQUIRE(PatchOptionsRows(buffer.data(), uint32_t(buffer.size())));
  std::vector<uint8_t> expected = kPatchedHead;
  const auto middle = Middle();
  expected.insert(expected.end(), middle.begin(), middle.end());
  expected.insert(expected.end(), kFiller.begin(), kFiller.end());
  REQUIRE(expected.size() == 77);
  CHECK(std::vector<uint8_t>(buffer.begin() + 0xD03, buffer.begin() + 0xD03 + 77) == expected);
  CHECK(buffer.size() == 4587);
}

TEST_CASE("other action buffers are left alone") {
  std::vector<uint8_t> other(4000, 0x00);
  CHECK_FALSE(PatchOptionsRows(other.data(), uint32_t(other.size())));
  std::vector<uint8_t> changed = FeOptionsBuffer();
  changed[0xD03 + 4] = 0x24;  // a different first string
  const std::vector<uint8_t> before = changed;
  CHECK_FALSE(PatchOptionsRows(changed.data(), uint32_t(changed.size())));
  CHECK(changed == before);
  std::vector<uint8_t> patched = FeOptionsBuffer();
  PatchOptionsRows(patched.data(), uint32_t(patched.size()));
  CHECK_FALSE(PatchOptionsRows(patched.data(), uint32_t(patched.size())));  // once only
}

TEST_CASE("the new row is named PC SETTINGS in the OPTIONS screen only") {
  OptionsRow row;
  CHECK(row.Text(u"Options", u"Options", u"Screens") == u"PC SETTINGS");
  CHECK_FALSE(row.Text(u"Options", u"Options_Desc", u"Screens").empty());
  CHECK(row.Text(u"Options", u"Sound", u"Screens").empty());       // the game's own rows
  CHECK(row.Text(u"MainMenu", u"Options", u"Screens").empty());    // the main menu's OPTIONS
  CHECK(row.Text(u"Options", u"Options", u"Other").empty());
}

TEST_CASE("selecting the new row opens the page; the game's rows do not") {
  OptionsRow row;
  CHECK_FALSE(row.OnCommand("Call_Kismet", "Event=SFX_UI_Menu_Select"));
  row.Text(u"Options", u"Options_Desc", u"Screens");  // the cursor moved onto it
  CHECK(row.OnCommand("Call_Kismet", "Event=SFX_UI_Menu_Select"));
  CHECK_FALSE(row.OnCommand("Call_Kismet", "Event=SFX_UI_Menu_Cancel"));
  CHECK_FALSE(row.OnCommand("Set_Game_Option", "Event=SFX_UI_Menu_Select"));
  row.Text(u"Options", u"Options", u"Screens");  // a label lookup does not move the cursor
  CHECK(row.OnCommand("Call_Kismet", "Event=SFX_UI_Menu_Select"));
  row.Text(u"Options", u"Controls_Desc", u"Screens");
  CHECK_FALSE(row.OnCommand("Call_Kismet", "Event=SFX_UI_Menu_Select"));
  row.Text(u"Options", u"Options_Desc", u"Screens");
  row.Text(u"MainMenu", u"Exhibition_Desc", u"Screens");  // left OPTIONS
  CHECK_FALSE(row.OnCommand("Call_Kismet", "Event=SFX_UI_Menu_Select"));
}
