// Reads of the game's UE3 rumble objects in guest memory: which controller
// and pad an effect goes to (evidence in docs/research/controller-vibration.md).

#pragma once

#include <cstdint>
#include <optional>

namespace wwe::vibration {

namespace guest {

inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  const uint8_t* p = base + address;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

inline void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  for (int i = 0; i < 4; ++i) base[address + i] = uint8_t(value >> (24 - 8 * i));
}

constexpr uint32_t kPawnController = 0x200;          // Pawn.Controller
constexpr uint32_t kPawnFlags = 0x6FC;               // bit 0x100: the AI controller possesses it
constexpr uint32_t kPawnAiPossessed = 0x100;
constexpr uint32_t kControllerForceFeedback = 0x458;  // PlayerController.ForceFeedbackManager
constexpr uint32_t kForceFeedbackVtable = 0x82197DE8;  // written by its constructor, 0x829ED3EC
constexpr uint32_t kControllerPlayer = 0x2E4;          // PlayerController.Player
constexpr uint32_t kPlayerController = 0x38;           // Player.Actor, the back link
constexpr uint32_t kPlayerControllerId = 0x58;         // LocalPlayer.ControllerId

}  // namespace guest

// The controller whose pad a hit on `pawn` rumbles, or nothing. The game's own
// gate (vtable+0x2D0, 0x82ABD530) skips a pawn its AI controller possesses:
// that controller is a GameAIController, whose +0x458 is no force-feedback
// manager. The manager's vtable is checked as well, so the play-rumble native
// only ever writes into a real one.
inline std::optional<uint32_t> RumbleTarget(const uint8_t* base, uint32_t pawn) {
  using namespace guest;
  if (Load32(base, pawn + kPawnFlags) & kPawnAiPossessed) {
    return std::nullopt;
  }
  const uint32_t controller = Load32(base, pawn + kPawnController);
  if (!controller) {
    return std::nullopt;
  }
  const uint32_t manager = Load32(base, controller + kControllerForceFeedback);
  if (!manager || Load32(base, manager) != kForceFeedbackVtable) {
    return std::nullopt;
  }
  return controller;
}

// The pad index (0-3) of the wrestler `pawn`, behind RumbleTarget's gate:
// the controller's local player and its ControllerId, the index
// ApplyForceFeedback passes to XamInputSetState. The game's own 0x82AE18E0
// reads pawn +0x200, +0x2E4, +0x58 the same way; the back link stands in for
// its casts.
inline std::optional<uint32_t> RumblePad(const uint8_t* base, uint32_t pawn) {
  using namespace guest;
  const std::optional<uint32_t> controller = RumbleTarget(base, pawn);
  if (!controller) {
    return std::nullopt;
  }
  const uint32_t player = Load32(base, *controller + kControllerPlayer);
  if (!player || Load32(base, player + kPlayerController) != *controller) {
    return std::nullopt;
  }
  const uint32_t pad = Load32(base, player + kPlayerControllerId);
  if (pad > 3) {
    return std::nullopt;
  }
  return pad;
}

}  // namespace wwe::vibration
