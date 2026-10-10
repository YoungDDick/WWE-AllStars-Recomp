#include <catch_amalgamated.hpp>

#include "native/frame_dumps.h"

using wwe::native::FrameDumps;

TEST_CASE("a frame list names the frames to dump") {
  FrameDumps dumps;
  REQUIRE(dumps.Parse("300, 2400,3000"));
  CHECK(dumps.Contains(300));
  CHECK(dumps.Contains(2400));
  CHECK(dumps.Contains(3000));
  CHECK_FALSE(dumps.Contains(301));
  CHECK_FALSE(dumps.Contains(0));
}

TEST_CASE("an empty list dumps nothing") {
  FrameDumps dumps;
  REQUIRE(dumps.Parse(""));
  CHECK(dumps.Empty());
  CHECK_FALSE(dumps.Contains(0));
}

TEST_CASE("a malformed list is rejected and dumps nothing") {
  FrameDumps dumps;
  CHECK_FALSE(dumps.Parse("300,abc"));
  CHECK_FALSE(dumps.Parse("300,,400"));
  CHECK_FALSE(dumps.Parse("-5"));
  CHECK(dumps.Empty());
}

TEST_CASE("each dump is named by its frame number in the directory") {
  CHECK(FrameDumps::Path("D:/out/run1", 2400) == "D:/out/run1/frame-002400.ppm");
  CHECK(FrameDumps::Path("D:/out/run1/", 7) == "D:/out/run1/frame-000007.ppm");
}
