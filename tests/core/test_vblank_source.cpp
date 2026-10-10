#include <catch_amalgamated.hpp>

#include <rex/graphics/vblank_source.h>

using rex::graphics::ChooseVblankSource;
using rex::graphics::VblankSource;

namespace {
// Played, fixed refresh: the case the rule exists for.
auto Played(double hz) { return ChooseVblankSource(hz, /*variable_refresh=*/false, /*allowed=*/true); }
}  // namespace

TEST_CASE("a display at a multiple of 60 Hz drives the guest vblank") {
  for (auto [hz, every] : {std::pair{60.0, 1u}, {120.0, 2u}, {240.0, 4u}, {360.0, 6u}}) {
    const auto d = Played(hz);
    CHECK(d.source == VblankSource::kDisplay);
    CHECK(d.display_per_guest == every);
  }
}

TEST_CASE("NTSC-style rates just under a multiple of 60 Hz still lock") {
  CHECK(Played(59.94).source == VblankSource::kDisplay);
  CHECK(Played(59.94).display_per_guest == 1);
  CHECK(Played(119.88).display_per_guest == 2);
  CHECK(Played(359.6).display_per_guest == 6);
}

TEST_CASE("rates that are not near a multiple of 60 Hz keep the host clock") {
  for (double hz : {144.0, 165.0, 75.0, 100.0, 50.0}) {
    CHECK(Played(hz).source == VblankSource::kHostClock);
  }
}

TEST_CASE("variable refresh keeps the host clock: the display follows the frames") {
  CHECK(ChooseVblankSource(360.0, true, true).source == VblankSource::kHostClock);
  CHECK(ChooseVblankSource(60.0, true, true).source == VblankSource::kHostClock);
}

TEST_CASE("scene runs keep the host clock") {
  CHECK(ChooseVblankSource(60.0, false, /*allowed=*/false).source == VblankSource::kHostClock);
}

TEST_CASE("no measurement keeps the host clock") {
  CHECK(ChooseVblankSource(0.0, false, true).source == VblankSource::kHostClock);
}
