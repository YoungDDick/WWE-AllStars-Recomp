// How many worker threads Native starts (low-end-preset ticket 12): pure, tested in
// tests/native/test_thread_budget.cpp, used by pipelines.cpp and textures.cpp.
//
// During play the guest's own threads (game, render, the SDK's command processor, audio)
// need most of the cores. On 8 or fewer logical CPUs (the Steam Deck has 8) Native keeps
// one pipeline and one texture worker; larger machines get a quarter of the cores each,
// from 2 to 4. A prepare runs no game, so its pipeline compiles use every core.

#pragma once

#include <algorithm>
#include <cstdint>

namespace wwe::native::thread_budget {

struct Workers {
  uint32_t pipelines;
  uint32_t textures;
};

inline constexpr uint32_t kSmallMachineCpus = 8;

// logical_cpus as std::thread::hardware_concurrency() reports it (0: unknown).
inline Workers ForPlay(uint32_t logical_cpus) {
  if (logical_cpus <= kSmallMachineCpus) {
    return {1, 1};
  }
  const uint32_t each = std::clamp(logical_cpus / 4, 2u, 4u);
  return {each, each};
}

inline Workers ForPrepare(uint32_t logical_cpus) {
  return {std::max(logical_cpus, 2u), ForPlay(logical_cpus).textures};
}

}  // namespace wwe::native::thread_budget
