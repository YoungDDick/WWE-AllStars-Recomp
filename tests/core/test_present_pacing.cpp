#include <catch_amalgamated.hpp>

#include <rex/ui/present_pacing.h>
#include <rex/ui/present_waiter.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace rex::ui::pacing;
using namespace std::chrono_literals;

namespace {
// The owner's AMD driver: present-wait v2 on the device and the surface, no v1.
PresentWaitInputs Amd() {
  PresentWaitInputs in;
  in.enabled = true;
  in.fifo = true;
  in.device_wait2 = true;
  in.surface_wait2 = true;
  return in;
}
}  // namespace

TEST_CASE("present-wait v2 is chosen where the device and the surface have it", "[pacing]") {
  CHECK(ChoosePresentWait(Amd()) == PresentWaitMode::kWait2);
}

TEST_CASE("present-wait v1 is the fallback where v2 is missing", "[pacing]") {
  PresentWaitInputs nvidia_old = Amd();
  nvidia_old.device_wait2 = false;
  nvidia_old.device_wait1 = true;
  CHECK(ChoosePresentWait(nvidia_old) == PresentWaitMode::kWait1);

  // v2 on the device but not on this surface, and no v1: timer pacing.
  PresentWaitInputs surface_without = Amd();
  surface_without.surface_wait2 = false;
  CHECK(ChoosePresentWait(surface_without) == PresentWaitMode::kTimer);
}

TEST_CASE("a surface without v2 falls back to v1 where the device has both", "[pacing]") {
  PresentWaitInputs both = Amd();
  both.device_wait1 = true;
  CHECK(ChoosePresentWait(both) == PresentWaitMode::kWait2);
  both.surface_wait2 = false;
  CHECK(ChoosePresentWait(both) == PresentWaitMode::kWait1);
}

TEST_CASE("timer pacing stays without present-wait, without vsync and when switched off", "[pacing]") {
  PresentWaitInputs intel_arc;
  intel_arc.enabled = true;
  intel_arc.fifo = true;
  CHECK(ChoosePresentWait(intel_arc) == PresentWaitMode::kTimer);

  // Scene runs present without vsync (--present_vsync=false): nothing queues, nothing to wait on.
  PresentWaitInputs scene_run = Amd();
  scene_run.fifo = false;
  CHECK(ChoosePresentWait(scene_run) == PresentWaitMode::kTimer);

  PresentWaitInputs off = Amd();
  off.enabled = false;
  CHECK(ChoosePresentWait(off) == PresentWaitMode::kTimer);
}

TEST_CASE("each wait mode has the name the log uses", "[pacing]") {
  CHECK(std::string(PresentWaitModeName(PresentWaitMode::kWait2)) == "present_wait2");
  CHECK(std::string(PresentWaitModeName(PresentWaitMode::kWait1)) == "present_wait");
  CHECK(std::string(PresentWaitModeName(PresentWaitMode::kTimer)) == "timer");
}

// ---- Swap log (--present_pacing_log): one row per paced present. ----

namespace {
SwapLogRow TimerRow() {
  SwapLogRow row;
  row.present_us = 1000000;
  row.interval_us = 16667;
  row.repeated_vblanks = 0;
  row.arrival_after_vblank_us = 1500;
  row.latency_us = 3600;
  row.guest_vblank = 61;
  row.frames_replaced = 0;
  row.external_sequence = 7;
  return row;
}
}  // namespace

TEST_CASE("the swap log keeps its columns and adds the present-wait ones at the end", "[pacing]") {
  CHECK(std::string(SwapLogHeader()) ==
        "present_us,interval_us,repeated_vblanks,arrival_after_vblank_us,latency_us,"
        "guest_vblank,frames_replaced,external_sequence,"
        "present_id,wait_us,unshown,shown_id,shown_us,shown_after_present_us\n");
}

TEST_CASE("a timer-paced row leaves the present-wait columns empty", "[pacing]") {
  CHECK(FormatSwapLogRow(TimerRow()) == "1000000,16667,0,1500,3600,61,0,7,,,,,,\n");
}

TEST_CASE("a present-wait row carries the wait and the newest shown present", "[pacing]") {
  SwapLogRow row = TimerRow();
  row.present_wait = true;
  row.present_id = 42;
  row.wait_us = 250;
  row.unshown = 1;
  row.has_shown = true;
  row.shown_id = 41;
  row.shown_us = 990100;
  row.shown_after_present_us = 6800;
  CHECK(FormatSwapLogRow(row) == "1000000,16667,0,1500,3600,61,0,7,42,250,1,41,990100,6800\n");

  // No present was shown since the last row: the shown columns stay empty.
  row.has_shown = false;
  CHECK(FormatSwapLogRow(row) == "1000000,16667,0,1500,3600,61,0,7,42,250,1,,,\n");
}

// ---- Pacing thread priority. ----

TEST_CASE("the pacing thread joins MMCSS Games when it can", "[pacing]") {
  CHECK(ChoosePacingThreadPriority(/*enabled=*/true, /*mmcss_joined=*/true) ==
        ThreadPriorityChoice::kMmcssGames);
}

TEST_CASE("without MMCSS the pacing thread runs at highest, never time critical", "[pacing]") {
  CHECK(ChoosePacingThreadPriority(true, false) == ThreadPriorityChoice::kHighest);
}

TEST_CASE("switched off, the pacing thread keeps its priority", "[pacing]") {
  CHECK(ChoosePacingThreadPriority(false, false) == ThreadPriorityChoice::kUnchanged);
  CHECK(ChoosePacingThreadPriority(false, true) == ThreadPriorityChoice::kUnchanged);
}

TEST_CASE("each priority choice has the name the log uses", "[pacing]") {
  CHECK(std::string(ThreadPriorityChoiceName(ThreadPriorityChoice::kMmcssGames)) == "MMCSS Games");
  CHECK(std::string(ThreadPriorityChoiceName(ThreadPriorityChoice::kHighest)) == "highest");
  CHECK(std::string(ThreadPriorityChoiceName(ThreadPriorityChoice::kUnchanged)) == "unchanged");
}

// ---- The present offset after the guest vblank (vblank + 5 ms). ----

TEST_CASE("the offset stays where the guest vblank is not locked to a fixed refresh", "[pacing]") {
  // Host clock: variable refresh (the owner's 360 Hz FreeSync) or a rate that is no multiple.
  CHECK(ChoosePresentOffsetUs(5000, /*display_period_us=*/0, 1000) == 5000);
}

TEST_CASE("the offset stays where it leaves room before the next refresh", "[pacing]") {
  CHECK(ChoosePresentOffsetUs(5000, 16667, 1000) == 5000);  // 60 Hz: 11.7 ms left
  CHECK(ChoosePresentOffsetUs(5000, 8333, 1000) == 5000);   // 120 Hz: 3.3 ms left
  CHECK(ChoosePresentOffsetUs(5000, 4167, 1000) == 5000);   // 240 Hz: 3.3 ms left
}

TEST_CASE("at a fixed 360 Hz the present moves to just before the following refresh",
          "[pacing]") {
  // Vblank + 5 ms is 0.56 ms before the refresh at 5.56 ms: a late present straddles it and
  // shows 2.78 ms later. Presenting 1 ms before the refresh at 8.33 ms always shows there.
  CHECK(ChoosePresentOffsetUs(5000, 2778, 1000) == 2 * 2778 + 2778 - 1000);
  // 180 Hz has the same 0.56 ms margin.
  CHECK(ChoosePresentOffsetUs(5000, 5556, 1000) == 5556 + 5556 - 1000);
}

// ---- A guest vblank without a new frame (ticket 23). ----
// Under variable refresh a skipped present holds the panel 33 ms; the next three frames then
// show 22.2, 12.6 and 15.0 ms (capture 20261004-192110). Presenting the last frame again keeps
// the panel at the guest's 60 Hz, as a fixed-refresh display does by itself.

TEST_CASE("a new guest frame is presented", "[pacing]") {
  CHECK(ChooseVblankPresent(/*new_frame=*/true, /*repeat_held=*/true, /*locked_fixed=*/false,
                            /*presented_before=*/true) == VblankPresent::kNewFrame);
  CHECK(ChooseVblankPresent(true, false, true, false) == VblankPresent::kNewFrame);
}

TEST_CASE("off a fixed-refresh lock, a held vblank presents the last frame again", "[pacing]") {
  CHECK(ChooseVblankPresent(false, true, /*locked_fixed=*/false, true) == VblankPresent::kRepeat);
}

TEST_CASE("a fixed-refresh display repeats the frame itself, so the held vblank is skipped",
          "[pacing]") {
  CHECK(ChooseVblankPresent(false, true, /*locked_fixed=*/true, true) == VblankPresent::kSkip);
}

TEST_CASE("switched off, a held vblank is skipped as before", "[pacing]") {
  CHECK(ChooseVblankPresent(false, /*repeat_held=*/false, false, true) == VblankPresent::kSkip);
}

TEST_CASE("nothing is repeated before the first frame was presented", "[pacing]") {
  CHECK(ChooseVblankPresent(false, true, false, /*presented_before=*/false) ==
        VblankPresent::kSkip);
}

// ---- Sleep then spin. ----

namespace {
// A clock the test moves: sleeping overshoots by `oversleep`, one spin step takes 10 us.
struct FakeClock {
  std::chrono::microseconds now{0};
  std::chrono::microseconds oversleep{0};
  std::vector<std::chrono::microseconds> sleeps;
  int spins = 0;
  void Wait(std::chrono::microseconds deadline, std::chrono::microseconds wake_ahead) {
    SleepThenSpin(
        deadline, wake_ahead, [this] { return now; },
        [this](std::chrono::microseconds d) {
          sleeps.push_back(d);
          now += d + oversleep;
        },
        [this] {
          ++spins;
          now += 10us;
        });
  }
};
}  // namespace

TEST_CASE("a far deadline sleeps until the wake-ahead and spins the rest", "[pacing]") {
  FakeClock clock;
  clock.oversleep = 300us;  // a coarse timer
  clock.Wait(16000us, 1000us);
  REQUIRE(clock.sleeps.size() == 1);
  CHECK(clock.sleeps[0] == 15000us);
  CHECK(clock.now >= 16000us);
  CHECK(clock.now < 16010us);
  CHECK(clock.spins == 70);
}

TEST_CASE("a deadline inside the wake-ahead only spins", "[pacing]") {
  FakeClock clock;
  clock.Wait(500us, 1000us);
  CHECK(clock.sleeps.empty());
  CHECK(clock.spins == 50);
}

TEST_CASE("a passed deadline returns at once", "[pacing]") {
  FakeClock clock;
  clock.now = 2000us;
  clock.Wait(1000us, 1000us);
  CHECK(clock.sleeps.empty());
  CHECK(clock.spins == 0);
}

TEST_CASE("a sleep that overshoots the deadline does not spin", "[pacing]") {
  FakeClock clock;
  clock.oversleep = 1500us;
  clock.Wait(16000us, 1000us);
  CHECK(clock.sleeps.size() == 1);
  CHECK(clock.spins == 0);
}

// ---- Present waiter: one thread follows the presents as the display shows them. ----

namespace {
// Stands in for vkWaitForPresent2KHR: the test decides which present ids the display has shown.
class FakeDisplay {
 public:
  void Show(uint64_t id) {
    {
      std::lock_guard lock(mutex_);
      shown_ = id;
    }
    cv_.notify_all();
  }
  void Fail() {
    {
      std::lock_guard lock(mutex_);
      failed_ = true;
    }
    cv_.notify_all();
  }
  PresentWaiter::WaitFn Fn() {
    return [this](uint64_t id, std::chrono::nanoseconds timeout) {
      std::unique_lock lock(mutex_);
      if (!cv_.wait_for(lock, timeout, [&] { return failed_ || shown_ >= id; })) {
        return PresentWaiter::WaitResult::kTimeout;
      }
      return failed_ ? PresentWaiter::WaitResult::kFailed : PresentWaiter::WaitResult::kShown;
    };
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  uint64_t shown_ = 0;
  bool failed_ = false;
};
}  // namespace

TEST_CASE("the waiter numbers presents from 1 and reports when each was shown", "[pacing]") {
  FakeDisplay display;
  PresentWaiter waiter;
  waiter.Attach(display.Fn());
  const uint64_t id = waiter.NextId();
  CHECK(id == 1);
  CHECK(waiter.LastId() == 1);
  const auto presented = PresentWaiter::Clock::now();
  waiter.Presented(id, presented);
  CHECK_FALSE(waiter.TakeLatestShown().has_value());
  display.Show(1);
  // Waiting for "nothing unshown" returns once the display has shown present 1.
  const auto wait = waiter.WaitForShown(0, 2s);
  CHECK(wait.active);
  CHECK(wait.unshown_before == 1);
  CHECK_FALSE(wait.timed_out);
  const auto shown = waiter.TakeLatestShown();
  REQUIRE(shown.has_value());
  CHECK(shown->id == 1);
  CHECK(shown->presented == presented);
  CHECK(shown->shown >= presented);
  // Taken once.
  CHECK_FALSE(waiter.TakeLatestShown().has_value());
}

TEST_CASE("waiting on the previous present returns at once when it was already shown", "[pacing]") {
  FakeDisplay display;
  PresentWaiter waiter;
  waiter.Attach(display.Fn());
  waiter.Presented(waiter.NextId(), PresentWaiter::Clock::now());
  display.Show(1);
  // Let the waiter thread see it.
  while (!waiter.TakeLatestShown()) {
    std::this_thread::sleep_for(1ms);
  }
  const auto wait = waiter.WaitForShown(0, 2s);
  CHECK(wait.active);
  CHECK(wait.unshown_before == 0);
  CHECK(wait.waited == PresentWaiter::Clock::duration::zero());
}

TEST_CASE("a present the display never shows ends the wait at the timeout", "[pacing]") {
  FakeDisplay display;
  PresentWaiter waiter;
  waiter.Attach(display.Fn());
  waiter.Presented(waiter.NextId(), PresentWaiter::Clock::now());
  const auto wait = waiter.WaitForShown(0, 30ms);
  CHECK(wait.active);
  CHECK(wait.timed_out);
  CHECK(wait.waited >= 30ms);
}

TEST_CASE("a failing wait falls back to timer pacing for that swapchain", "[pacing]") {
  FakeDisplay display;
  PresentWaiter waiter;
  waiter.Attach(display.Fn());
  waiter.Presented(waiter.NextId(), PresentWaiter::Clock::now());
  display.Fail();
  while (!waiter.failed()) {
    std::this_thread::sleep_for(1ms);
  }
  CHECK(waiter.NextId() == 0);
  CHECK_FALSE(waiter.WaitForShown(0, 2s).active);
}

TEST_CASE("detaching stops waiting on the old swapchain and a new one counts from 1", "[pacing]") {
  FakeDisplay old_display;
  PresentWaiter waiter;
  waiter.Attach(old_display.Fn());
  waiter.Presented(waiter.NextId(), PresentWaiter::Clock::now());
  waiter.Presented(waiter.NextId(), PresentWaiter::Clock::now());
  // Returns although present 2 was never shown: the swapchain may be destroyed afterwards.
  waiter.Detach();
  CHECK(waiter.NextId() == 0);
  CHECK_FALSE(waiter.WaitForShown(0, 2s).active);

  FakeDisplay new_display;
  waiter.Attach(new_display.Fn());
  CHECK(waiter.NextId() == 1);
}
