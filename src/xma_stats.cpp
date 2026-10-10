// Stale-XMA-read counters (see xma_stats.h). The hook only reads guest
// memory and calls the original, so the game sees exactly what it did before.

#include "generated/default/wweallstars_pch.h"

#include "xma_stats.h"

#include <atomic>

#include <rex/hook.h>
#include <rex/logging.h>

namespace {

// Return address of the call at 0x82FD5054 inside the busy-wait loop
// 0x82FD5048-0x82FD505C.
constexpr uint32_t kBusyWaitReturn = 0x82FD5058;

std::atomic<uint64_t> g_calls;      // every readable-samples query
std::atomic<uint64_t> g_not_valid;  // queries that report the whole ring (stale PCM)
std::atomic<uint64_t> g_wait_calls;  // queries from the busy-wait
std::atomic<uint64_t> g_wait_zero;   // ... that returned 0, so the wait went on

uint64_t Get(const std::atomic<uint64_t>& counter) {
  return counter.load(std::memory_order_relaxed);
}

}  // namespace

void wwe::xma::LogStats(const char* when) {
  REXLOG_INFO("xma-stats {}: readable_queries={} not_valid_full_ring={} busy_wait_queries={} "
              "busy_wait_zero={}",
              when, Get(g_calls), Get(g_not_valid), Get(g_wait_calls), Get(g_wait_zero));
}

// The readable-samples query (r3 = playback object, r4 = stream index).
DECLARE_REX_FUNC(sub_833A9AC8);
REX_HOOK_RAW(sub_833A9AC8) {
  const bool from_wait = ctx.lr == kBusyWaitReturn;
  const bool valid = wwe::xma::OutputValid(base, ctx.r3.u32, ctx.r4.u32);
  __imp__sub_833A9AC8(ctx, base);
  g_calls.fetch_add(1, std::memory_order_relaxed);
  if (!valid) {
    g_not_valid.fetch_add(1, std::memory_order_relaxed);
  }
  if (from_wait) {
    g_wait_calls.fetch_add(1, std::memory_order_relaxed);
    if (ctx.r3.u32 == 0) {
      g_wait_zero.fetch_add(1, std::memory_order_relaxed);
    }
  }
}
