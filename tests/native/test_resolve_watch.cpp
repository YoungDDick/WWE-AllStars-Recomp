#include <catch_amalgamated.hpp>

#include "native/texture_layout.h"
#include "resolve_watch_ledger.h"

using wwe::resolve_watch::Ledger;

TEST_CASE("level 0 spans whole tiles when tiled, padded rows when linear") {
  using wwe::native::texture_layout::Level0Bytes;
  // 1280x720, 4 bytes per texel: tiled height rounds up to 736.
  CHECK(Level0Bytes(1280, 720, 2, true) == 1280u * 736u * 4u);
  // Linear rows of 100 texels x 4 bytes pad to 512 bytes.
  CHECK(Level0Bytes(100, 10, 2, false) == 512u * 10u);
}

namespace {
void ArmResolve(Ledger& ledger, uint32_t target, uint32_t first_page, uint32_t pages) {
  ledger.CountResolve(target, pages << 12);
  for (uint32_t i = 0; i < pages; ++i) ledger.Arm(first_page + i, target);
}
}  // namespace

TEST_CASE("a CPU read after a resolve is a readback, once per resolve") {
  Ledger ledger;
  ArmResolve(ledger, 0x1000000, 0x1000, 2);
  CHECK(ledger.Fault(0x1001, false, 605, 0xAA));
  CHECK_FALSE(ledger.Fault(0x1001, false, 606, 0xAA));  // disarmed until the next resolve
  const auto& t = ledger.Targets().at(0x1000000);
  CHECK(t.resolves == 1);
  CHECK(t.reads == 1);
  CHECK(t.writes_first == 0);
  CHECK(t.first_read_frame == 605);
  CHECK(t.read_sites.at(0xAA) == 1);

  ArmResolve(ledger, 0x1000000, 0x1000, 2);
  CHECK(ledger.Fault(0x1000, false, 661, 0xBB));
  CHECK(ledger.Targets().at(0x1000000).reads == 2);
  CHECK(ledger.Targets().at(0x1000000).first_read_frame == 605);
}

TEST_CASE("a write before any read is reuse, not a readback") {
  Ledger ledger;
  ArmResolve(ledger, 0x2000000, 0x2000, 1);
  CHECK(ledger.Fault(0x2000, true, 11, 0xCC));
  const auto& t = ledger.Targets().at(0x2000000);
  CHECK(t.reads == 0);
  CHECK(t.writes_first == 1);
  CHECK(t.write_sites.at(0xCC) == 1);
}

TEST_CASE("faults outside armed pages are not ours") {
  Ledger ledger;
  CHECK_FALSE(ledger.Fault(0x3000, false, 1, 0));
  ArmResolve(ledger, 0x3000000, 0x3000, 1);
  CHECK_FALSE(ledger.Fault(0x3001, false, 2, 0));
}

TEST_CASE("a page resolved again by another target belongs to the newest") {
  Ledger ledger;
  ArmResolve(ledger, 0x4000000, 0x4000, 2);
  ArmResolve(ledger, 0x4001000, 0x4001, 1);
  CHECK(ledger.Fault(0x4001, false, 3, 0x11));
  CHECK(ledger.Targets().at(0x4001000).reads == 1);
  CHECK(ledger.Targets().at(0x4000000).reads == 0);
}
