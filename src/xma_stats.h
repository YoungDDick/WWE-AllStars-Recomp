// Stale-XMA-read counters (ticket 15, log only).
//
// 0x833A9AC8 returns how many decoded PCM samples one XMA context of a
// multi-context stream has ready. When the context's output_buffer_valid bit
// is clear it reports the whole ring as readable (0x833A9B34), so the game
// reads stale PCM. The busy-wait at 0x82FD5048 calls it until it returns
// non-zero. A hook counts both paths without changing what the game sees
// (docs/research/stuck-audio-loop.md).

#pragma once

#include <cstdint>

namespace wwe::xma {

namespace detail {
inline uint32_t LoadBE32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
}  // namespace detail

// The output_buffer_valid bit 0x833A9AC8 tests for stream `stream` of the
// playback object at `player` (0x833A9AC8-0x833A9B00): flag 0x20000 at +4
// selects the locked local copy at the stream's 0x60-byte record, otherwise
// the hardware context the record points to at +0x40. Bit 31 of context
// dword 1 is the flag.
inline bool OutputValid(const uint8_t* base, uint32_t player, uint32_t stream) {
  const uint32_t flags = detail::LoadBE32(base + player + 4);
  const uint32_t record = detail::LoadBE32(base + player + 8) + stream * 0x60;
  const uint32_t context =
      (flags & 0x20000) ? record : detail::LoadBE32(base + record + 0x40);
  return (detail::LoadBE32(base + context + 4) & 0x80000000u) != 0;
}

// Presents between periodic log lines (10 s at 60 a second).
constexpr uint64_t kLogEveryPresents = 600;

// Logs the counts so far ("periodic" from the Present hook, "final" at exit).
void LogStats(const char* when);

}  // namespace wwe::xma
