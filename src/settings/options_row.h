// The PC SETTINGS row in the game's own OPTIONS menu (in-game settings ticket
// 07). docs/research/options-menu-gfx.md has the evidence: the rows are an
// ActionScript literal in fe_options, labels come from UE3 Localize, and A on
// a row sends FSCommand Call_Kismet Event=SFX_UI_Menu_Select. Pure, so it is
// tested on its own (tests/core/test_options_row.cpp); src/settings/
// options_hooks.cpp wires it to the game.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace wwe::settings {

// fe_options' frame-1 action buffer: 4587 bytes, the row list at 0xD03.
constexpr uint32_t kOptionsActionSize = 4587;
constexpr uint32_t kOptionsRowsOffset = 0xD03;
constexpr uint32_t kOptionsPatchSize = 77;

// Rewrites `menuContents = new Array("Sound","GamePlay","Controls")` as
// [Sound, GamePlay, Controls, Options] in place, length unchanged: the two
// bytes for the extra key come from the release-dead trace calls after it.
// Returns false (and changes nothing) unless the buffer is fe_options' and
// still holds the original bytes.
inline bool PatchOptionsRows(uint8_t* actions, uint32_t size) {
  // push "menuContents" "Controls" "GamePlay" "Sound" 3 "Array"; newObject setVariable
  static constexpr uint8_t kHead[] = {0x96, 0x0F, 0x00, 0x08, 0x23, 0x08, 0x2D,
                                      0x08, 0x2C, 0x08, 0x2B, 0x07, 0x03, 0x00,
                                      0x00, 0x00, 0x08, 0x1A, 0x40, 0x1D};
  // push "menuContents" "Options" "Controls" "GamePlay" "Sound" 4 "Array"; newObject setVariable
  static constexpr uint8_t kNewHead[] = {0x96, 0x11, 0x00, 0x08, 0x23, 0x08, 0x35, 0x08,
                                         0x2D, 0x08, 0x2C, 0x08, 0x2B, 0x07, 0x04, 0x00,
                                         0x00, 0x00, 0x08, 0x1A, 0x40, 0x1D};
  // (push "+++", dup, trace, trace) x 2: balanced, and dead like the traces it replaces.
  static constexpr uint8_t kFiller[] = {0x96, 0x02, 0x00, 0x08, 0x48, 0x4C, 0x26, 0x26,
                                        0x96, 0x02, 0x00, 0x08, 0x48, 0x4C, 0x26, 0x26};
  constexpr uint32_t kMiddle = 39;  // pxGutter, desc_suffix, a playAudio call: kept
  constexpr uint32_t kTraces = 18;  // three `push <constant>; trace` pairs
  static_assert(sizeof(kHead) + kMiddle + kTraces == kOptionsPatchSize);
  static_assert(sizeof(kNewHead) + kMiddle + sizeof(kFiller) == kOptionsPatchSize);
  if (size != kOptionsActionSize) {
    return false;
  }
  uint8_t* region = actions + kOptionsRowsOffset;
  if (std::memcmp(region, kHead, sizeof(kHead)) != 0) {
    return false;
  }
  const uint8_t* traces = region + sizeof(kHead) + kMiddle;
  for (uint32_t i = 0; i < kTraces; i += 6) {
    if (traces[i] != 0x96 || traces[i + 1] != 0x02 || traces[i + 2] != 0x00 ||
        traces[i + 3] != 0x08 || traces[i + 5] != 0x26) {
      return false;
    }
  }
  uint8_t middle[kMiddle];
  std::memcpy(middle, region + sizeof(kHead), kMiddle);
  std::memcpy(region, kNewHead, sizeof(kNewHead));
  std::memcpy(region + sizeof(kNewHead), middle, kMiddle);
  std::memcpy(region + sizeof(kNewHead) + kMiddle, kFiller, sizeof(kFiller));
  return true;
}

// Follows the OPTIONS screen's cursor through its description lookups and
// supplies the new row's text.
class OptionsRow {
 public:
  // The game asks Localize(section, key, package). Returns the new row's text
  // for its two keys, or empty to let the game answer. Every "*_Desc" lookup
  // is the description line following the cursor.
  std::u16string Text(std::u16string_view section, std::u16string_view key,
                      std::u16string_view package) {
    const bool ours = section == u"Options" && package == u"Screens";
    if (key.size() > 5 && key.substr(key.size() - 5) == u"_Desc") {
      selected_ = ours && key == u"Options_Desc";
    }
    if (!ours) {
      return {};
    }
    if (key == u"Options") {
      return u"PC SETTINGS";
    }
    if (key == u"Options_Desc") {
      return u"Change display, graphics and audio settings for this PC version.";
    }
    return {};
  }

  // An FSCommand: true when it is A on the new row (the select sound).
  bool OnCommand(std::string_view command, std::string_view args) const {
    return selected_ && command == "Call_Kismet" && args == "Event=SFX_UI_Menu_Select";
  }

 private:
  bool selected_ = false;
};

}  // namespace wwe::settings
