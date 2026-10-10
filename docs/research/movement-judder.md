# Movement judder with a flat frame-time graph

Date 2026-10-03. The owner reports that in matches, movement sometimes looks
stuttery or juddery while the RTSS frame-time graph stays completely flat. The
goal is butter-smooth motion. Addresses are base XEX v5. ReXGlue paths are
relative to `.tools/rexglue-source` (`f5337cdc` plus `patches/`). The
disassembly is `python scripts/disasm.py`, run on `analysis/xex/default.pe`.

> **Update 2026-10-04 (zero-stutter ticket 13).** Where this note says the swapchain
> is `IMMEDIATE`, that is no longer true. `present_vsync` (default on) now creates a
> `FIFO` swapchain, with one image per refresh and no tearing; under VRR the display
> still shows each frame when it arrives. `IMMEDIATE`/`MAILBOX`/`FIFO_RELAXED` are used
> only with `--present_vsync=false`, as in scene runs.
> - Presents are paced to the guest vblank at +5 ms (`present_pacing_offset_us`), and
>   the guest vblank follows the display where it can (ADR-0016).
> - Where the driver has present-wait (v2, then v1), the pacing thread also waits for
>   the previous present to be shown, so one frame stays queued. The swap log records
>   each present's id, wait and shown time.
> - On a fixed refresh locked to the display, a present that would land less than 1 ms
>   before a refresh moves to 1 ms before the refresh after it. This happens at 180 and
>   360 Hz.
>
> H1, §2 "Presentation" and the plans in §5 and §6 below describe the old
> `IMMEDIATE` setup.

Each claim is marked **confirmed** (read in source, disassembly or logs) or
**hypothesis** (plausible, not yet measured).

## Answer

- **The game advances a fixed 1/60 s per engine tick. It does not use measured
  frame time.** This is confirmed from `FEngineLoop::Tick` at `0x82417890`
  (§1). Every frame it shows is one tick further on, so any judder must come
  from *how frames reach the screen*, not from uneven simulation steps. That is
  good news, because it makes even presentation the right model, as it was for
  SvR 2011.
- **The flat RTSS line is expected and proves little.** Our presenter holds
  each guest frame and calls `vkQueuePresentKHR` from a dedicated thread, 5 ms
  after each guest vblank, by spinning on a host timer (§2). RTSS sees those
  calls, so the graph is flat by construction. RTSS does not see when the
  display actually scans each frame out, and it does not see *which* game frame
  each present carried.
- **Ranked hypotheses:**

| # | Cause | Confidence | RTSS sees it? |
|---|---|---|---|
| H1 | **The display cadence does not match 60 Hz.** The swapchain is `IMMEDIATE`, and presents run on the host clock, not on the monitor's vblank. On a fixed 120/144/165 Hz display that gives a 2-3-2-3 cadence. On a "60 Hz" display that is really 59.94–59.95 Hz, the two clocks drift, which gives periodic duplicated or dropped refreshes, or a rolling tear line | High (the mechanism is confirmed; the owner's display mode is unknown) | No |
| H2 | **Native shows the *latest* published frame, not the frame that belongs to that swap.** If the command processor falls a frame behind the guest CPU (for example when the GPU is busy with supersampling plus CAS at 1440p), one swap shows frame N+1 and the next shows N+1 again, or skips one. Present timing stays even | Medium (the race is confirmed in code; how often it happens is unmeasured) | No |
| H3 | **A host UI overlay is open** (F3 debug, console, F4 settings, F7 achievements, or an achievement toast). Presents then go through the window thread at the monitor rate, which is a different path | Medium-low (only while an overlay is up) | Only as a changed rate |
| H4 | **A driver feature retimes or generates frames**: AMD Fluid Motion Frames, Radeon Chill, Enhanced Sync, or the RTSS limiter or scanline sync | Unknown; easy to rule out | Partly |
| H5 | **A missed guest vblank or a late frame** (after vblank + 5 ms). The frame waits a whole vblank and the next one replaces it | Low while RTSS is flat (it would show a 33 ms spike) | Yes |
| H6 | **A 1/30 s tick.** The game has a second tick length, entered on a flag (§1) | Low in matches (logs show 60.0 presents/s) | Yes, if the frame rate halves |

The first test is cheap and probably decisive: one PresentMon capture of a
match, reading `PresentMode` and `MsBetweenDisplayChange` (§6).

## 1. How the game paces itself (disassembly)

### Time source (confirmed)

- `QueryPerformanceCounter` is `0x8241A150`. It reads `mftb r11, 0x10c`,
  retries if the low word is zero, then stores the 64-bit value
  (`0x8241A150–0x8241A168`).
- `QueryPerformanceFrequency` is `0x82D992E0`, a wrapper around the kernel
  import `KeQueryPerformanceFrequency` (thunk `0x834FFFA4`).
- UE3's `appInitTiming` is `0x82609340`. It computes
  `GSecondsPerCycle = 1.0 / Frequency` from the constant 1.0 at `0x82003008`,
  stores it at `0x837094A0`, then stores an initial `appSeconds()` at
  `0x83701448`.
- In ReXGlue, `mftb` becomes `REX_QUERY_TIMEBASE()`
  (`src/codegen/builders/system.cpp:266-270`). That value is scaled from the
  host QPC (`src/core/clock_win.cpp:18-30`, `src/core/clock.cpp`). The
  translated code has 526 `mftb` sites.

### Engine loop (confirmed)

`GuardedMain` calls Init (`0x824142D0`) and then `0x82418A20`. It then runs
`while (!GIsRequestingExit[0x837094E0]) Tick(0x82417890)`; see the call at
`0x82419F38` in `generated/default/wweallstars_recomp.185.cpp`.
`0x82417890` is `FEngineLoop::Tick`:

| Address | What it does |
|---|---|
| `0x82417930–0x82417964` | `now = QPC × GSecondsPerCycle`; `measured = now − last` (`last` at `0x835E0090`) |
| `0x82417968–0x824179D8` | **Statistics only.** A 120-frame running maximum of `measured` at `0x8370BD30`, and an exponential average `avg = 0.99·avg + 0.01·measured` (when `measured < 0.5 s`) at `0x8370BD28`. The constants are 0.99 at `0x82002FF8`, 0.01 at `0x82002FF0` and 0.5 at `0x82003000` |
| `0x824179DC–0x82417A3C` | **The tick length.** If flag `0x837094F4` is set and `0x837094F0` and `0x83709510` are clear: `DeltaSeconds = 1/30` (float at `0x82002FE8` = 0.0333333) and a counter at `0x835E0088` is set to 5. Otherwise `DeltaSeconds = 1/60` (float at `0x82001D3C` = 0.0166667). **The measured time is not used** |
| `0x82417A40–0x82417A80` | Frame start = the previous limiter exit time (`0x8370BD20`, stored to `this+0xC`); `last = QPC now` |
| `0x82417B2C–0x82417B68` | A time-dilation ramp (`this+0x2C` steps toward `this+0x30` by `this+0x34`), then `DeltaSeconds` goes to `this+0x28`; scaled `dt = DeltaSeconds × dilation` |
| `0x82417B6C–0x82417BA0` | `GEngine->Tick(dt)` (GEngine at `0x8371D180`, vtable `+0xCC`), plus three calls to `0x829D2108(dt)` |
| `0x82417BD8–0x82417C54` | `GEngine->GetMaxTickRate()` (vtable `+0xE0`). If it returns more than 0, the game calls `appSleep(1/rate − elapsed)` (`appSleep` is `0x82608FD8`) |
| `0x82417E40–0x82417EC0` | **A 60 Hz frame limiter on the QPC.** `while (QPC_seconds − frame_start < dt) appSleep(0.0)`. Each pass stores `now` into `0x8370BD20` |

What follows:

- **The simulation is lock-step, one fixed tick per frame (confirmed).** There
  is no catch-up and no interpolation. A frame that misses its vblank makes the
  game run slower, not skip ahead. That matches SvR 2011, whose game also steps
  per frame (SVR2011-Recomp `docs/performance.md` L607-613).
- **Two clocks limit the frame rate.** One is the game's own QPC limiter
  (≥ `dt` between limiter exits). The other is the XDK D3D present throttle on
  guest vblank interrupts. Both run on the same guest clock in ReXGlue (§2), so
  they cannot drift apart. Their phase, however, is free (hypothesis: whichever
  is later sets each frame's start).
- **Observed rate in matches is 60.0 presents/s (confirmed).** In
  `analysis/play-iq03/runtime.log`, the `native renderer: frame N` lines every
  300 frames give 60.0 fps from frame 4500 to 30000 (match, about 15k draws per
  300 frames). Loading drops to 32–40 fps. `play-39` shows the same. So the
  1/30 branch (H6) is not active in matches.
- **Config (confirmed):** `[Engine.Engine]` in
  `assets/Game/Config/Xenon/Cooked/Coalesced.ini` sets `m_fMinUpdateTime=0.0005`
  and `m_fMaxUpdateTime=0.40` (THQ fields, probably a dt clamp for the
  SD_Animation subsystem), plus `NxTimeStep=0.02` and `NxMaxDeltaTime=0.333333`.
  It has no `bSmoothFrameRate` or `MaxSmoothedFrameRate` override. The engine
  passes the fixed `dt`, so those clamps do not change it.
- **Open (hypothesis):** whether some subsystem (Scaleform UI, particles, the
  physics queue) reads wall time on its own. Six or more other functions read
  `0x8370BD20` (frame start), for example in `wweallstars_recomp.171.cpp`.
  Ticket 30's non-determinism shows that *something* depends on host timing,
  but that is about thread order and RNG, not motion smoothness.
- `GetMaxTickRate()`'s return value is unknown. If it returns a value just
  above 60 it would add a second sleep (instrumentation item I5).

## 2. How ReXGlue makes vblank, the clock and presents

### Guest clock and vblank (confirmed)

- Guest time is the host QPC (`Clock::host_tick_count_platform`,
  `src/core/clock_win.cpp:24-30`). No scaling by default.
- The "GPU VSync" host thread (`src/graphics/graphics_system.cpp:150-190`)
  computes `vsync_interval_ticks = guest_tick_frequency / refresh_rate`, where
  `refresh_rate` comes from `VdQueryVideoMode`. That is the
  `video_mode_refresh_rate` cvar, default 60.0, range 24–240
  (`src/ui/window.cpp:63`, `src/kernel/xboxkrnl/xboxkrnl_video.cpp:76-78, 243`).
  On a 10 MHz QPC this is 166,666 ticks, which gives 60.0002 Hz.
- `rexglue-frame-pacing.patch` (ported from SVR2011-Recomp) makes the thread
  sleep on a high-resolution waitable timer (`PreciseSleep`,
  `src/core/threading_win.cpp:104-125`; `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`,
  [CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw)).
  At each tick, `MarkVblank()` increments the CP counter and dispatches the
  guest graphics interrupt (source 0) (`graphics_system.cpp:338-351`). The
  thread also gives the presenter the *ideal* tick time (`OnGuestVblank`).
- **The guest vblank is not tied to the monitor.** Nothing in this path reads
  the host display's vblank. The same holds in Xenia's design, which this code
  is derived from (not re-checked upstream in this pass). So the guest 60.000 Hz
  and the monitor's real refresh drift by their frequency difference.

### Presentation (confirmed)

- **Present mode is `IMMEDIATE`.** The swapchain prefers `IMMEDIATE`, then
  `MAILBOX`, then `FIFO_RELAXED`, then `FIFO`
  (`src/ui/vulkan/vulkan_presenter.cpp:1380-1405`). Each step is controlled by a
  `vulkan_allow_present_mode_*` cvar (lines 52-59). The owner's sessions log
  `presentation mode 0` (= `VK_PRESENT_MODE_IMMEDIATE_KHR`) at 2560x1440
  (`analysis/play-iq03/runtime.log`, `play-39`,
  `manual-native-20261003-001238.log`). `minImageCount = max(3, min)`
  (line 1324). The window is SDL3 borderless desktop fullscreen
  (`src/ui/window_sdl.cpp:173-175`). The SDK's own comment says that with
  borderless fullscreen on Windows, presentation may be copied by GDI instead
  of independent flip, which rules out VRR (`vulkan_presenter.cpp:1387-1389`).
  This is unverified on AMD; PresentMon's `PresentMode` column settles it.
- Per the Vulkan specification
  ([VkPresentModeKHR](https://registry.khronos.org/vulkan/specs/latest/man/html/VkPresentModeKHR.html)),
  `IMMEDIATE` does not wait for vblank and can tear. `FIFO` queues and shows one
  image per vblank. `MAILBOX` replaces the queued image and shows the newest at
  each vblank.
- **Paced presents** (`rexglue-frame-pacing.patch`; `src/ui/presenter.cpp`):
  - `present_pace_to_guest_vblank` (default true). When the CP issues a guest
    swap, `RefreshGuestOutput` (line 579) only marks a frame as pending.
  - A "Guest Output Pacing" thread (line 696) presents it at
    `vblank_ideal + present_pacing_offset_us` (default 5000). It sleeps until
    1 ms before that time, then spins (`kWakeAhead`, line 703).
  - If no frame is pending at that moment, the vblank counts as *repeated* and
    nothing is presented (line 767). A second frame that arrives before the
    present *replaces* the first (line 656).
  - Present intervals are therefore even to about 0.1–0.15 ms (SvR measurement:
    SVR2011-Recomp `docs/performance.md` L573-577). **That is the flat RTSS
    line.**
- **Overlays switch the path.** Any registered ImGui dialog sends presents
  through the window thread (`GetDesiredPaintModeFromUIThread`,
  `presenter.cpp:1525-1528`). The F3, backtick, F4 and F7 overlays register
  dialogs (`src/ui/rex_app.cpp:398-425`). The patch already unregisters the
  achievement toast when it is idle; SvR found that an always-registered
  overlay caused repainting at the monitor rate.
- **Native hand-off (confirmed race).** The native renderer publishes its frame
  at the guest's `D3DDevice_Present` hook (`0x82DBB558`, `src/d3d_census.cpp:582-593`
  → `native_renderer.cpp:2049-2172`), *before* `VdSwap` writes the swap packet.
  When the command processor later executes that swap, it presents
  `external_frame::Latest()` (`patches/rexglue-native-frame.patch`, the
  `command_processor.cpp` hunk at 2344). It does not present the frame with the
  matching `sequence`; the field exists but is not checked. If the CP is more
  than one guest frame behind, swap N shows frame N+1 and swap N+1 shows N+1
  again, or N+2. The present stays on time, so RTSS stays flat (H2). Classic
  does not have this race, because it resolves the swap's own front buffer.
- **Timer resolution (confirmed absent).** Neither the app nor the SDK calls
  `timeBeginPeriod`. The game's frame limiter uses `appSleep(0)` (a yield), and
  the pacing threads use high-resolution timers, so the frame path does not
  depend on the 15.6 ms default. Guest `KeDelayExecutionThread` with
  millisecond waits does go through `::Sleep` (`threading_win.cpp:96-102`). In
  wwe-svr2011-pc, `timeBeginPeriod(1)` removed a 39 fps cap
  (`docs/research/svr2011-pc-port.md`). We do not see such a cap (matches hold
  60.0).

## 3. Why RTSS is flat while motion judders

- RTSS hooks the present call, so it plots the interval between present calls
  (PresentMon calls the same thing `MsBetweenPresents`: "the time between this
  Present() call and the previous one"). This is **hypothesis** for RTSS
  internals, because Guru3D/Unwinder publish no formal metric definition. It is
  the standard description and matches what SvR saw: "match action stuttered,
  despite a stable Afterburner presentation graph" (SVR2011-Recomp
  `docs/performance.md` L91-94).
- What the eye sees is *when each frame reaches the screen and for how long*.
  PresentMon measures this from ETW
  ([README-ConsoleApplication](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md)):
  - `MsBetweenDisplayChange`: "How long the previous frame was displayed before
    this Present() was displayed".
  - `DisplayedTime`: "How long the frame was displayed on the screen, or 'NA'
    if the frame was not displayed".
  - `MsUntilDisplayed`: "The time between the Present() call and when the frame
    was displayed".
  - `PresentMode`, for example "Hardware: Legacy Flip", "Composed: Flip",
    "Hardware Composed: Independent Flip".
  - `AllowsTearing`: "1 if partial frames might be displayed".
  - `FrameType`: rendered by the application, or generated by a driver/SDK.
- `MsAnimationError` is "the difference between the previous frame's CPU delta
  and display delta". `AnimationTime` is "the time the CPU started animation
  work on this frame" (same README; background:
  [GamersNexus interview with Intel](https://gamersnexus.net/gpus-cpus-deep-dive/fps-benchmarks-are-flawed-introducing-animation-error-engineering-discussion)).
  **For this process, PresentMon's CPU-side times belong to our host pacing
  thread, not the guest's game tick.** So `MsAnimationError` measures display
  irregularity against an even CPU cadence. It is still useful for H1, but it
  cannot see H2 (the wrong game frame inside an on-time present). H2 needs our
  own logging (§7, I1).
- A worked case for H1. At 144 Hz fixed refresh, 60 presents/s map to 2.4
  refreshes per frame. Frames stay on screen for 13.9 ms or 20.8 ms in a
  2-3-2-3-2 pattern while the game advances exactly 16.67 ms each time. Pans
  and fast moves then show a regular beat. The present-call graph is a flat
  16.67 ms line; `MsBetweenDisplayChange` alternates 13.9 and 20.8.
- A worked case at "60 Hz". 2560x1440 at 60 Hz is often 59.95x Hz. Against
  60.000 Hz presents, that is one extra frame every ~20 s:
  - Composed: one frame dropped, plus jitter bursts while the two phases cross
    (MAILBOX-like "latest wins" at the compositor).
  - Independent flip with `IMMEDIATE`: a tear line that rolls through the
    screen at that period.
  
  Either one would read as "sometimes it stutters".

## 4. Other causes considered

- **Uneven ticks per frame (2-1-2-1).** Ruled out by design: one fixed tick per
  engine loop (§1). The game has no fixed-step accumulator that could run
  0, 1 or 2 ticks per frame.
- **Interpolation.** Neither the game nor the runtime interpolates, and with
  lock-step 60 Hz on 60 Hz output none is needed. On 120/144 Hz without VRR,
  only VRR or a 120 Hz mode removes the cadence (§5).
- **Input polling.** `XamInputGetKeystrokeEx` is polled about 2,800 times/s
  (`xenia-baseline.md`). With fixed ticks, input timing changes latency, not
  motion smoothness. Not a judder source (hypothesis).
- **Thread scheduling and power management.** These only matter if they make a
  guest frame miss its vblank + 5 ms slot (H5, visible in RTSS), or delay the
  pacing thread's spin (visible in RTSS). The guest sets `KeSetAffinityThread`
  and `KeSetBasePriorityThread` on 26 threads (`xenia-baseline.md`); the host
  pacing thread runs at normal priority. A low risk while RTSS is flat.
- **GPU-wait spin `0x82DB6710`** (about 885k calls per present,
  `native-renderer.md`). This costs CPU, but the game holds 60.0, so it is not
  a cause here.

## 5. Fix directions (for later tickets, not done)

1. **H1:** Run the display at a true 60 Hz *with* VRR (FreeSync). The swapchain
   must be on a flip path (PresentMon shows "Hardware: Independent Flip" or
   "Legacy Flip"). This is the setup SvR's tester called "extremely smooth"
   (360 Hz FreeSync, `IMMEDIATE`).
   - Without VRR: use a 60 Hz or 120 Hz mode and try `FIFO`/`MAILBOX`
     (`--vulkan_allow_present_mode_immediate=false`).
   - Better long-term: drive the guest vblank from the real display vblank
     (`VK_KHR_present_wait` / `VK_KHR_present_id`, or
     `D3DKMTWaitForVerticalBlankEvent`), so the two clocks cannot drift. This
     is an SDK patch. It would also cover SvR.
2. **H2:** Make swap N present the native frame published for that present
   (check `Frame.sequence`, or queue the published frames and pop one per
   swap) instead of `Latest()`.
3. **H3:** Keep overlays closed during judgement. Consider drawing the debug
   overlay on the pacing thread.

## 6. Measurement plan (owner plays by hand, about 1–2 minutes per run)

Use the same short stretch each time: a match, both wrestlers moving, and a
camera that pans. Note what the owner feels (smooth, judders sometimes, judders
constantly) next to each capture. Close all overlays (F3/F4/F7/backtick). In
Adrenalin, turn off AFMF, Chill and Enhanced Sync for this exe. Turn off the
RTSS frame limiter and scanline sync. RTSS can stay as a display.

### Step 0: facts to write down (one minute)

- Monitor model, the mode Windows reports (Settings → Display → Advanced: e.g.
  "143.995 Hz"), and whether FreeSync is on in both the monitor OSD and
  Adrenalin.
- The number RTSS shows: 60, or the monitor rate? The monitor rate means H3 or
  another UI-thread paint.

### Step 1: PresentMon capture of the default setup

Use PresentMon 2.x console (Intel, GitHub releases), run as administrator.
Start the capture once the match is underway:

```
PresentMon.exe --process_name wweallstars.exe --timed 60 --v2_metrics ^
    --output_file D:\Projects\WWE_ALLSTARS-RECOMP\analysis\judder\pm-default.csv
```

Launch the game with the pacing log for the same window. Give absolute paths,
because the game's working directory is the exe folder:

```
wweallstars.exe ... --present_pacing_stats=true ^
    --present_pacing_log=D:\Projects\WWE_ALLSTARS-RECOMP\analysis\judder\pacing.csv ^
    --frame_event_log=true
```

Columns to read and what they mean:

| Column | Expected if smooth | What confirms a hypothesis |
|---|---|---|
| `PresentMode`, `AllowsTearing` | "Hardware: Independent Flip" (or Legacy Flip) | "Composed: Copy with GPU GDI" or "Composed: Flip" plus a non-60 Hz display → H1 likely; VRR impossible on that path |
| `MsBetweenPresents` | 16.67 ± 0.2 | Matches RTSS. Spikes of 33 → H5 (check `pacing.csv` `repeated_vblanks`) |
| `MsBetweenDisplayChange` / `DisplayedTime` | Every frame 16.67 (VRR or exactly 60 Hz) | Alternating 13.9/20.8 (144 Hz) or 12.1/18.2 (165 Hz) → **H1 confirmed**. A lone 33.3 plus a lone `NA` every ~N s on 60 Hz → **H1 drift confirmed**; the interval should match `1/|f_display − 60.0|` |
| `MsUntilDisplayed` | Flat | A slow saw-tooth that wraps every N s → the clocks are drifting (H1) |
| `FrameType` | Application only | Driver-generated rows → H4 (AFMF) |
| `MsAnimationError` | about 0 | Non-zero pattern that follows `MsBetweenDisplayChange` → H1. **Zero while judder is still felt → look at H2** |

The log line `Paced presentation 5s: … {} vblanks without a new frame, {}
frames replaced …, window paints: …` should show 0 / 0 / 0 in a match.
Non-zero replaced or repeated counts → H5. Non-zero window paints → H3.
`python scripts/analyze_hitches.py <run folder>` lists slow frames with nearby
pipeline and readback events.

### Step 2: display configurations to compare (same scene, one capture each)

| Run | Display | Flags | Prediction if H1 is the cause |
|---|---|---|---|
| A | Native mode as today (e.g. 144/165 Hz), FreeSync on | default | Smooth only if `PresentMode` is a flip mode and `DisplayedTime` is a flat 16.67 |
| B | Same, FreeSync off | default | Constant judder; `DisplayedTime` 2-3 cadence |
| C | Windows set to 120 Hz | default | Mostly smooth; a 1-3 blip every ~N s (drift) |
| D | Windows set to 60 Hz | default | Smooth with a periodic hiccup every ~N s, or a rolling tear |
| E | Windows set to 60 Hz | `--vulkan_allow_present_mode_immediate=false` (MAILBOX) | No tear; still a drop or duplicate every ~N s |
| F | Windows set to 60 Hz | `--video_mode_refresh_rate=<exact Hz from Step 0, e.g. 59.951>` | The period of D/E's hiccups grows a lot (the clocks nearly match). The game runs ≤0.1% slower, which nobody will notice. **Use only values near 60.** At 120/144 the game's own 60 Hz limiter and the guest vblank would disagree |

If A is smooth and B–D are not, H1 is the whole story and the fix is
configuration plus documentation. If every run still judders while
`DisplayedTime` is flat, H1 is refuted; go to Step 3.

### Step 3: renderer A/B for H2

Repeat the best run from Step 2 with Classic (`--wwe_native_renderer=false`),
then with Native with enhancements off (`--wwe_native_supersample=false
--wwe_native_cas=false`, the image-quality cvars).

- Judder only on Native, and less without supersampling → H2 very likely.
  Confirm with I1.
- Judder on both → not H2.

## 7. Instrumentation to add, ranked by value

| # | What | Why | Effort |
|---|---|---|---|
| I1 | At each CP swap with external frames on: log `Latest().sequence` against the swap's own expected sequence (count swaps). Count `duplicate` (same sequence as the last swap) and `skip` (jump > 1) per 5 s, and add a column to `present_pacing_log` | Settles H2 directly, and flat RTSS cannot | Small (SDK patch plus native frame counter) |
| I2 | Add `guest_frame_seq` and the guest vblank count to each `present_pacing_log` row (both already exist: the `presents` counter and the vblank count in `OnGuestVblank`) | Maps presents to game ticks. A step of 2 or 0 between rows = a skipped or repeated tick on screen | Small |
| I3 | Log the host display's real vblank timestamps (`D3DKMTWaitForVerticalBlankEvent` on a side thread, or `VK_KHR_present_wait`) next to the guest vblank, as the phase difference per second | Measures H1 drift (Hz difference, phase wraps) without PresentMon. It is also the input a future vblank-locked clock needs | Medium |
| I4 | Per frame: the engine tick's `DeltaSeconds` (`this+0x28`), dilation (`this+0x2C`), and the 1/30 flag `0x837094F4` read from a `REX_HOOK_RAW(sub_82417890)` wrapper; count frames per 5 s with dt ≠ 1/60 | Proves fixed dt in matches (H6) and catches slow-motion phases | Small |
| I5 | Log `GetMaxTickRate()` (the result of vtable `+0xE0`, same hook) once per 300 frames, and how long the limiter loop at `0x82417E40` spins | Shows whether a second sleep fights the vblank | Small |
| I6 | The frame's arrival time after vblank (already in `present_pacing_log` as `arrival_after_vblank_us`): add a 5 s summary with p99 and max against the 5000 µs offset | Early warning for H5 if heavy scenes approach 5 ms | Trivial |

## Sources

- Disassembly: `0x82417890` (Tick), `0x82419F38` (loop), `0x82609340`
  (`appInitTiming`), `0x8241A150` (QPC), `0x82D992E0` (QPF), `0x82608FD8`
  (`appSleep`), constants at `0x82001D3C`, `0x82002FE8`, `0x82002FF0-0x82003008`.
- ReXGlue: `src/graphics/graphics_system.cpp:150-190, 338-351`;
  `src/ui/presenter.cpp:92-108, 579-660, 696-800, 1509-1533`;
  `src/ui/vulkan/vulkan_presenter.cpp:52-59, 1324-1405`;
  `src/kernel/xboxkrnl/xboxkrnl_video.cpp:76-78, 231-247`;
  `src/core/threading_win.cpp:96-125`; `src/ui/rex_app.cpp:398-425`;
  `src/ui/window_sdl.cpp:173-175`.
- This repo: `patches/rexglue-frame-pacing.patch`, `patches/rexglue-native-frame.patch`,
  `patches/rexglue-guest-tick-source.patch`, `src/d3d_census.cpp:580-593`,
  `src/native/native_renderer.cpp:2049-2172`, `src/frame_clock.*`,
  `.scratch/playable-build/issues/30-lock-guest-time-to-presents.md`,
  `analysis/play-iq03/runtime.log`.
- SVR2011-Recomp `docs/performance.md` L85-97, L548-628 (same pacing patch;
  RTSS limiter stutter; stable graph with stutter; 5 ms offset).
- [Vulkan VkPresentModeKHR](https://registry.khronos.org/vulkan/specs/latest/man/html/VkPresentModeKHR.html);
  [PresentMon console README (metrics)](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md);
  [Microsoft: CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw);
  [Microsoft: DXGI flip model and independent flip](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model).

## Owner answers (2026-10-03)

- Display: 360 Hz, FreeSync on.
- RTSS shows 60 frames/s in matches.

What this changes:

- **H3 (host overlays) does not explain judder in normal play.** RTSS reads
  60, not 360, so presents are not going through the window thread at the
  monitor rate.
- **H1 narrows.** 360 is an exact multiple of 60, so the 2-3-2-3 cadence of a
  144/165 Hz display does not apply. What remains of H1 is drift between the
  host's 60.000 Hz guest vblank and the panel. If FreeSync is active for the
  game (it needs independent flip, which PresentMon's `PresentMode` column
  shows), each frame scans out on arrival and H1 is mostly ruled out. If the
  game is composed by DWM, a frame occasionally holds for 5 or 7 refreshes
  instead of 6: an error of about 2.8 ms, which is small.
- **H2 (Native shows the wrong frame) becomes the lead hypothesis.** I1 is
  the first instrumentation to add.
