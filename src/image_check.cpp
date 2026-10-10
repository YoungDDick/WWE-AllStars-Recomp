#include "image_check.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace wwe::image_check {

uint64_t TextHash(std::span<const uint8_t> text) {
  return XXH3_64bits(text.data(), text.size());
}

}  // namespace wwe::image_check
