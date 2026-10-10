#include <catch_amalgamated.hpp>

#include <cstdint>
#include <vector>

#include "xma_stats.h"

using wwe::xma::OutputValid;

namespace {

// Guest memory for 0x833A9AC8's inputs: a playback object at 0x100 with its
// stream records at 0x200 (0x60 bytes each) and hardware contexts at 0x800.
struct Guest {
  std::vector<uint8_t> memory = std::vector<uint8_t>(0x1000);

  void Store(uint32_t address, uint32_t value) {
    memory[address] = uint8_t(value >> 24);
    memory[address + 1] = uint8_t(value >> 16);
    memory[address + 2] = uint8_t(value >> 8);
    memory[address + 3] = uint8_t(value);
  }

  // Stream `index` uses the hardware context at `context`.
  void Stream(uint32_t index, uint32_t context, bool valid) {
    Store(0x200 + index * 0x60 + 0x40, context);
    Store(context + 4, valid ? 0x80000000u : 0x20000000u);
  }
};

}  // namespace

TEST_CASE("the readable-samples query reads output_buffer_valid of the stream's context") {
  Guest g;
  g.Store(0x104, 0);      // not locked: use the hardware context
  g.Store(0x108, 0x200);  // stream records
  g.Stream(0, 0x800, true);
  g.Stream(2, 0x840, false);
  CHECK(OutputValid(g.memory.data(), 0x100, 0));
  CHECK_FALSE(OutputValid(g.memory.data(), 0x100, 2));
}

TEST_CASE("a locked stream reads its local context copy at the record") {
  Guest g;
  g.Store(0x104, 0x20000);  // locked
  g.Store(0x108, 0x200);
  g.Stream(1, 0x800, true);            // hardware context says valid...
  g.Store(0x200 + 0x60 + 4, 0);        // ...the local copy says not valid
  CHECK_FALSE(OutputValid(g.memory.data(), 0x100, 1));
}
