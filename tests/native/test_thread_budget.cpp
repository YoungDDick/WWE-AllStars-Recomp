// Native's worker counts by logical CPU count (low-end-preset ticket 12).

#include <catch_amalgamated.hpp>

#include "native/thread_budget.h"

using wwe::native::thread_budget::ForPlay;
using wwe::native::thread_budget::ForPrepare;

TEST_CASE("on 8 or fewer logical CPUs play keeps one pipeline and one texture worker") {
  for (uint32_t cpus : {1u, 2u, 4u, 6u, 8u}) {
    const auto workers = ForPlay(cpus);
    CHECK(workers.pipelines == 1);
    CHECK(workers.textures == 1);
  }
}

TEST_CASE("larger machines scale both kinds of worker with the cores, up to four") {
  CHECK(ForPlay(12).pipelines == 3);
  CHECK(ForPlay(12).textures == 3);
  CHECK(ForPlay(16).pipelines == 4);
  CHECK(ForPlay(16).textures == 4);
  CHECK(ForPlay(32).pipelines == 4);
  CHECK(ForPlay(32).textures == 4);
}

TEST_CASE("an unknown CPU count plays like a small machine") {
  CHECK(ForPlay(0).pipelines == 1);
  CHECK(ForPlay(0).textures == 1);
}

TEST_CASE("prepare runs no game, so it compiles on every core") {
  CHECK(ForPrepare(4).pipelines == 4);
  CHECK(ForPrepare(8).pipelines == 8);
  CHECK(ForPrepare(16).pipelines == 16);
  CHECK(ForPrepare(32).pipelines == 32);
  CHECK(ForPrepare(0).pipelines == 2);
  CHECK(ForPrepare(1).pipelines == 2);
}

TEST_CASE("prepare keeps the texture workers of play") {
  for (uint32_t cpus : {0u, 4u, 8u, 16u, 32u}) {
    CHECK(ForPrepare(cpus).textures == ForPlay(cpus).textures);
  }
}
