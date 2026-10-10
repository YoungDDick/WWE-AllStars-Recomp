#include <catch_amalgamated.hpp>

#include <cstdint>
#include <vector>

#include "image_check.h"

using wwe::image_check::kMismatchMessage;
using wwe::image_check::kTextHash;
using wwe::image_check::TextHash;

TEST_CASE("a text that is not the TU2 one does not hash to the TU2 value") {
  std::vector<uint8_t> text(4096, 0x60);  // nops
  CHECK(TextHash(text) != kTextHash);
  CHECK(TextHash({}) != kTextHash);
}

TEST_CASE("one changed byte changes the text hash") {
  std::vector<uint8_t> text(4096, 0x60);
  const uint64_t before = TextHash(text);
  text[2048] ^= 1;
  CHECK(TextHash(text) != before);
  // XXH3-64 of "abc", the value the image test computes in Python too.
  const uint8_t abc[] = {'a', 'b', 'c'};
  CHECK(TextHash(abc) == 0x78AF5F94892F3950ULL);
}

TEST_CASE("the mismatch message names Title Update 2 and the launcher") {
  CHECK(kMismatchMessage.find("Title Update 2") != std::string_view::npos);
  CHECK(kMismatchMessage.find("WWEAllStarsLauncher") != std::string_view::npos);
}
