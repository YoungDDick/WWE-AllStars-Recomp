// Guest memory holding a wrestler's pawn, its controller, the controller's
// force-feedback manager and its local player, laid out as the game's UE3 objects
// are (docs/research/controller-vibration.md).

#pragma once

#include <cstdint>
#include <vector>

namespace wwe::test {

struct FakeGuest {
  static constexpr uint32_t kPawn = 0x100;
  static constexpr uint32_t kController = 0x1000;
  static constexpr uint32_t kManager = 0x1800;
  static constexpr uint32_t kManagerVtable = 0x82197DE8;
  static constexpr uint32_t kPlayer = 0x1C00;

  std::vector<uint8_t> memory = std::vector<uint8_t>(0x2000);

  // A human wrestler: possessed by its player controller, which has a manager
  // and the local player on pad `pad`.
  explicit FakeGuest(uint32_t pad = 0) {
    Store32(kPawn + 0x200, kController);
    Store32(kController + 0x458, kManager);
    Store32(kManager, kManagerVtable);
    Store32(kController + 0x2E4, kPlayer);
    Store32(kPlayer + 0x38, kController);
    Store32(kPlayer + 0x58, pad);
  }

  const uint8_t* base() const { return memory.data(); }

  uint32_t Load32(uint32_t address) const {
    return uint32_t(memory[address]) << 24 | uint32_t(memory[address + 1]) << 16 |
           uint32_t(memory[address + 2]) << 8 | memory[address + 3];
  }

  void Store32(uint32_t address, uint32_t value) {
    for (int i = 0; i < 4; ++i) memory[address + i] = uint8_t(value >> (24 - 8 * i));
  }

  // Pawn +0x6FC bit 0x100: the pawn's AI controller possesses it.
  void PossessByAi() { Store32(kPawn + 0x6FC, Load32(kPawn + 0x6FC) | 0x100); }
};

}  // namespace wwe::test
