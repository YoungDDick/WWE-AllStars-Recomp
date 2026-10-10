// The game state the rumble mixer reads (seam 3, guest reads): the game's own
// pause-menu test and GWorld, on sparse fake guest memory.

#include <catch_amalgamated.hpp>

#include <cstdint>
#include <map>

#include "vibration/game_state.h"

using wwe::vibration::GameState;
using wwe::vibration::ReadGameState;

namespace {

struct SparseGuest {
  static constexpr uint32_t kMenu = 0x40001000;
  std::map<uint32_t, uint32_t> words;

  GameState Read() const {
    return ReadGameState([this](uint32_t address) {
      const auto it = words.find(address);
      return it == words.end() ? 0u : it->second;
    });
  }
};

}  // namespace

TEST_CASE("in a match with the pause menu down the game is not paused") {
  SparseGuest guest;
  guest.words = {{0x837218A0, 0x50000000}, {0x835E5180, SparseGuest::kMenu}};
  const GameState state = guest.Read();
  CHECK_FALSE(state.paused);
  CHECK(state.world == 0x50000000);
}

TEST_CASE("either menu-up word of the pause menu pauses") {
  for (uint32_t field : {0x64u, 0x68u}) {
    SparseGuest guest;
    guest.words = {{0x835E5180, SparseGuest::kMenu}, {SparseGuest::kMenu + field, 1}};
    CHECK(guest.Read().paused);
  }
}

TEST_CASE("no pause menu built yet, or the override set, is not paused") {
  SparseGuest none;
  CHECK_FALSE(none.Read().paused);

  SparseGuest overridden;
  overridden.words = {{0x835E5180, SparseGuest::kMenu},
                      {SparseGuest::kMenu + 0x64, 1},
                      {0x83740590, 1}};
  CHECK_FALSE(overridden.Read().paused);
}
