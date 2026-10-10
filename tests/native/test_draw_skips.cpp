#include <catch_amalgamated.hpp>

#include <string>

#include "native/draw_skips.h"

using wwe::native::DrawSkips;
using wwe::native::Skip;

TEST_CASE("each reason keeps its total and its first frame and key") {
  DrawSkips skips;
  skips.Count(Skip::kNoTexture, 120, 0x1F260000);
  skips.Count(Skip::kNoTexture, 130, 0x15010000);
  skips.Count(Skip::kNoShader, 140, 0xABCDEF);
  const auto& texture = skips.Get(Skip::kNoTexture);
  CHECK(texture.total == 2);
  CHECK(texture.first_frame == 120);
  CHECK(texture.first_key == 0x1F260000);
  CHECK(skips.Get(Skip::kNoShader).total == 1);
  CHECK(skips.Get(Skip::kBadStream).total == 0);
}

TEST_CASE("every reason has its own name") {
  for (size_t a = 0; a < size_t(Skip::kCount); ++a) {
    CHECK(std::string(DrawSkips::Name(Skip(a))).size() > 0);
    for (size_t b = a + 1; b < size_t(Skip::kCount); ++b) {
      CHECK(std::string(DrawSkips::Name(Skip(a))) != DrawSkips::Name(Skip(b)));
    }
  }
}

TEST_CASE("totals leave out pipeline_pending on request") {
  DrawSkips skips;
  skips.Count(Skip::kPipelinePending, 1, 0);
  skips.Count(Skip::kPipelinePending, 2, 0);
  skips.Count(Skip::kIndexRange, 3, 0);
  CHECK(skips.Total(true) == 3);
  CHECK(skips.Total(false) == 1);
}

TEST_CASE("changed reports new counts once") {
  DrawSkips skips;
  CHECK_FALSE(skips.TakeChanged());
  skips.Count(Skip::kBadStream, 5, 0);
  CHECK(skips.TakeChanged());
  CHECK_FALSE(skips.TakeChanged());
}

TEST_CASE("the summary names nonzero reasons only") {
  DrawSkips skips;
  CHECK(skips.Summary() == "none");
  skips.Count(Skip::kNoTexture, 7, 0x10);
  skips.Count(Skip::kNoTexture, 8, 0x20);
  CHECK(skips.Summary() == "no_texture=2");
}

TEST_CASE("the JSON report lists every nonzero reason with first frame and key") {
  DrawSkips skips;
  CHECK(skips.Json() == "{}");
  skips.Count(Skip::kNoShader, 42, 0xDEADBEEF);
  CHECK(skips.Json() ==
        R"({"no_shader": {"total": 1, "first_frame": 42, "first_key": "00000000deadbeef"}})");
}
