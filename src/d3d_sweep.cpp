#include "d3d_sweep.h"

namespace wwe::sweep {
namespace {

std::atomic<uint32_t> g_device{0};
constexpr uint32_t kRingCursorOffset = 0x30;

uint32_t LoadBE32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint32_t Cursor(const uint8_t* base) {
  const uint32_t device = g_device.load(std::memory_order_relaxed);
  return device ? LoadBE32(base + device + kRingCursorOffset) : 0;
}

}  // namespace

std::atomic<uint64_t> g_calls[kCount];
std::atomic<uint64_t> g_draws[kCount];

void SetDevice(uint32_t device) { g_device.store(device, std::memory_order_relaxed); }

uint32_t Enter(int index, const uint8_t* base) {
  g_calls[index].fetch_add(1, std::memory_order_relaxed);
  return Cursor(base);
}

void Exit(int index, const uint8_t* base, uint32_t before) {
  const uint32_t after = Cursor(base);
  // Skip wraps, ring switches and implausible spans.
  if (!before || after <= before || after - before > 0x10000) return;
  uint64_t draws = 0;
  for (uint32_t at = before; at + 4 <= after;) {
    const uint32_t header = LoadBE32(base + at);
    if ((header >> 30) != 3) {
      at += 4;  // not a type-3 header: step one word (type-0 data, NOPs)
      continue;
    }
    const uint32_t opcode = (header >> 8) & 0x7F;
    if (opcode == 0x22 || opcode == 0x36) ++draws;
    at += 4 * (((header >> 16) & 0x3FFF) + 2);
  }
  if (draws) g_draws[index].fetch_add(draws, std::memory_order_relaxed);
}

}  // namespace wwe::sweep
