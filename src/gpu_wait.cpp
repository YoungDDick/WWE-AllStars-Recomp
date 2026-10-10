// D3D's GPU-wait loops back off instead of spinning a host core (low-end-preset
// ticket 11; evidence in docs/research/spin-loops.md §2). Six wait loops call
// 0x82DB90A0 once (the wait begins), then the spin step 0x82DB91B0 each pass
// until the GPU has progressed. The spin step itself is never hooked (about
// 885k calls per present, docs/research/native-renderer.md); a midasm hook right
// after each of its six calls, WweGpuWaitBackoff, waits on the host instead.

#include "generated/default/wweallstars_pch.h"

#include <chrono>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/thread.h>

#include "spin_waits.h"

REXCVAR_DEFINE_BOOL(wwe_gpu_wait_backoff, true, "WWEAllStars",
                    "Let D3D's GPU-wait loops yield and sleep instead of spinning a host core "
                    "(false: the game's own spin, for comparisons)");

namespace {

using Clock = std::chrono::steady_clock;

// When the current GPU wait on this thread began.
thread_local Clock::time_point t_wait_start;

}  // namespace

// The wait's begin call: fills the wait record (device, reason, read pointer, time).
DECLARE_REX_FUNC(sub_82DB90A0);
REX_HOOK_RAW(sub_82DB90A0) {
  t_wait_start = Clock::now();
  __imp__sub_82DB90A0(ctx, base);
}

// After each pass's spin step, before the loop tests the GPU's progress.
void WweGpuWaitBackoff() {
  if (!REXCVAR_GET(wwe_gpu_wait_backoff)) {
    return;
  }
  const int64_t waited_us =
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t_wait_start).count();
  switch (wwe::spin_waits::WaitBackoff(waited_us)) {
    case wwe::spin_waits::Backoff::kNone:
      break;
    case wwe::spin_waits::Backoff::kYield:
      rex::thread::MaybeYield();
      break;
    case wwe::spin_waits::Backoff::kSleep:
      rex::thread::PreciseSleep(std::chrono::microseconds(wwe::spin_waits::kSleepUs));
      break;
  }
}
