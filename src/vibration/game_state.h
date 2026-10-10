// What the rumble mixer reads of the game at each pad request: whether the
// pause menu is up and which world is loaded (evidence in
// docs/research/controller-vibration.md, "Full-coverage vibration research").

#pragma once

#include <cstdint>

namespace wwe::vibration {

struct GameState {
  bool paused = false;  // the pause menu is up
  uint32_t world = 0;   // GWorld: a change is a map load, such as leaving a match
};

namespace guest {

constexpr uint32_t kPauseMenu = 0x835E5180;       // the pause menu singleton
constexpr uint32_t kPauseMenuUp = 0x64;           // set by the ig_pause loader 0x82AF8464
constexpr uint32_t kPauseMenuUpToo = 0x68;        // a second "menu up" word, 0x82AFA7A4
constexpr uint32_t kPauseMenuOverride = 0x83740590;  // non-zero: never paused
constexpr uint32_t kWorld = 0x837218A0;           // GWorld

}  // namespace guest

// The game's own IsPauseMenuUp 0x82B00950, and GWorld. `load` reads a
// big-endian word of guest memory.
template <typename Load32>
GameState ReadGameState(Load32 load) {
  using namespace guest;
  GameState state;
  state.world = load(kWorld);
  if (load(kPauseMenuOverride) != 0) {
    return state;
  }
  if (const uint32_t menu = load(kPauseMenu)) {
    state.paused = load(menu + kPauseMenuUp) != 0 || load(menu + kPauseMenuUpToo) != 0;
  }
  return state;
}

}  // namespace wwe::vibration
