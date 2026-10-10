// Byte-wise hashing and comparison for plain, zero-padded keys (PipelineKey
// and its parts), for the unordered maps and sets that hold them.

#pragma once

#include <cstddef>
#include <cstring>

#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include <xxhash.h>

namespace wwe::native {

struct BytesHash {
  template <typename T>
  size_t operator()(const T& value) const {
    return size_t(XXH3_64bits(&value, sizeof(value)));
  }
};

struct BytesEqual {
  template <typename T>
  bool operator()(const T& a, const T& b) const {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
  }
};

}  // namespace wwe::native
