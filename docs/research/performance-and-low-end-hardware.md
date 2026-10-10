# Performance measurement and low-end hardware (Steam Deck)

Date 2026-10-04. Owner's question: how fast will the port be, and how much
can we optimize the Native renderer for older hardware such as the Steam
Deck? This note covers three things: how to measure credibly, what the target
hardware is, and where the cost and the levers are. Paths in ReXGlue are
relative to `.tools/rexglue-source` (`f5337cdc` plus `patches/`). Addresses
are base XEX v5. Numbers marked **measured** come from captures already on
disk (`analysis/`, not committed), read for this note with a throwaway
script in the session scratchpad. Numbers marked **estimate** are
arithmetic on specs and are not measurements. Nothing was built or run for
this note.

The project has no Steam Deck (`.scratch/image-quality/map.md:36`,
`.scratch/image-quality/spec.md:181`). Every Deck number below is therefore
an estimate.

## Answer

- **"Average FPS" cannot measure this port.** The game paces itself: a fixed
  1/60 s tick with its own QPC limiter (`movement-judder.md` §1) behind a
  guest vblank (ADR-0016). It shows 60 on any machine that keeps up, and
  then slows down rather than dropping frames. Headroom has to be measured as
  **work per frame against the 16.67 ms budget**, separately for the guest
  CPU, the renderer CPU and the GPU. The frame-time distribution (1 % / 0.1 %
  lows, hitches) shows only whether a machine keeps up.
- **Most of the CPU-side measurement already exists:** the tick log (guest
  work per tick), the Native hitch line (renderer and fence time), PresentMon
  captures with marks, `analyze_hitches.py` and `compare_builds.py`. The
  GPU side is the gap:
  - Native has no GPU timer;
  - no Tracy zones in `src/`, and no Tracy GPU zones anywhere;
  - no debug labels for RenderDoc, RGP or Nsight;
  - no report reads the `GPUBusy` column that every PresentMon capture
    already records.
- **Measured on the owner's RX 7900 XT** (PresentMon `GPUBusy`, whole
  sessions):
  - Native at 2x2 supersampling (the default): median 1.4–2.3 ms per frame.
  - Native at 3x scale: median 3.0–3.4 ms per frame, p99 4.7–5.6 ms.
  - Guest work per 60 Hz tick: median 3.3 ms, p99 7.5 ms. Ticks at the
    game's own 1/30 s rate (intro, entrances) take 13–14 ms.
- **Steam Deck (estimate):** the GPU is about 9–13x smaller than the 7900 XT
  by bandwidth and by CU × clock. Supersampling on the Deck very likely
  misses 16.7 ms. 1x (1280x720, which is the Deck's native width) is near the
  budget, and only a Deck run can settle it. The CPU side cannot be estimated
  yet: the owner's CPU model is not recorded anywhere in the repo.
- **Levers, by expected payoff:**
  1. a low-end default profile: supersampling off, CAS off or cheap,
     anisotropy lower;
  2. stop the two busy-wait loops that burn host cores (the game's limiter
     and the D3D GPU-wait spin). On a 15 W APU the CPU and GPU share one
     power budget;
  3. the `x86-64-v3` build (ticket 11; the Deck's Zen 2 supports it);
  4. barrier and memory hygiene in the renderer.

  Async compute, FSR and descriptor work have little to gain here (see C).
- **Linux/Proton:** the repo has never run on Linux or Proton. ADR-0006 puts
  Linux and the Deck at M6. The Native renderer's device needs are all core
  Vulkan 1.2/1.3 features that RADV exposes. Proton is therefore a plausible
  early way to measure on a Deck before the native Linux build, but this is
  unverified.

## A. Measuring credibly

### A.1 What is already in the repo

| Tool | Turn on | What it gives | Limits |
|---|---|---|---|
| Tick log | `--wwe_tick_log=true` (`src/game_tick_log.cpp:19`) | Per second: ticks, ticks at 1/30 s, mean and max game work per tick. Work is the tick minus the time in the limiter's `appSleep` (`game_tick_log.cpp:1-5, 57-60`) | Work includes waits on the GPU and on D3D's present throttle, so it is not pure CPU |
| Native hitch line | always on (Native) | Frames over 25 ms with renderer ms, GPU fence-wait ms, pipeline and texture counts (`src/native/native_renderer.cpp:2109-2145`) | Only frames over 25 ms. The renderer timer is collected every frame (`native_renderer.cpp:394-428`) but not logged |
| Render-thread totals | always on (Native) | Pipelines compiled, modules created and waits on the render thread (`native_renderer.cpp:1082-1087`) | Counts only |
| `--perf_log_csv` | cvar (`src/core/perf/counter.cpp:22`; opened by `src/wweallstars_app.h:105-109`) | One row per guest swap: `frame_time_us`, draw calls, cache hits and more (`counter.cpp:34-52`) | Compiled only when the config is not Release (`CMakeLists.txt:27-32`). Not every counter is set on Vulkan (`entrance-video-stutter.md`, instrumentation item 6) |
| PresentMon 2.6.0 | `scripts/capture.ps1` (marks with Ctrl+Shift+M) | Per present: `FrameTime`, `DisplayedTime`, `PresentMode`, `CPUBusy`, `GPUBusy`, `GPUTime` (header of every `analysis/capture-*/presentmon.csv`) | Windows only. `CPUBusy` belongs to the presenting thread (the SDK pacing thread), not the guest |
| Smoothness report | `scripts/analyze_hitches.py` | Hitch, judder and zero-stutter bars per marked window, 1 % low, worst frame, a `game:` line (`analyze_hitches.py:1-27`) | Ignores `GPUBusy` (no match for `GPUBusy` in `scripts/`) |
| Build comparison | `scripts/compare_builds.py` | Game work per tick across builds over a fixed span (`compare_builds.py:1-8`) | Run-to-run noise of 20–30 % on idle menus (ticket 11) |
| Scenes | `scripts/scene_runner.py`, `tests/scenes/exhibition-1v1.toml` | Scripted input from boot to the result screen of a 1v1 match | Headless, fixed 60.000 Hz vblank (ADR-0016), judged on image, not time |
| Tracy 0.13.1 | `--wwe_profile=true` (`src/main.cpp:7-8`, `src/wweallstars_app.h:100-103`) | Frame marks at each guest swap (`include/rex/perf/counter.h:105-111`); coarse SDK zones in the command processor and the function dispatcher (`src/graphics/command_processor.cpp`, `src/system/function_dispatcher.cpp:61, 93, 144`) | **No zone in `src/`. No `TracyVkZone` in the SDK or in `src/`** (the SDK's GPU macros are empty, `include/rex/dbg.h:56-58`). Compiled out in Release (`CMakeLists.txt:27-29`). The shipped build is RelWithDebInfo (`scripts/package.ps1:34` ships `TracyClientrd.dll`), so the profiled build is the shipped one |
| ImGui debug overlay | F3 | Guest FPS, frame-time graph, counters (`src/ui/overlay/debug_overlay.cpp:36-80`) | **Do not measure with it on.** A registered dialog moves presents to the window thread (`movement-judder.md` §2, "Overlays switch the path") |

### A.2 What is missing, and what to use

**GPU time per pass (Vulkan timestamps).** This is the most important gap.
- `vkCmdWriteTimestamp2` writes a device timestamp into a
  `VK_QUERY_TYPE_TIMESTAMP` pool. The spec says that "implementations may
  write the timestamp at any stage that is logically later than `stage`".
- One tick is `VkPhysicalDeviceLimits::timestampPeriod` nanoseconds. Only
  `timestampValidBits` of the value are valid.
- Source: Vulkan-Docs `chapters/queries.adoc`, Timestamp Queries
  (https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp2.html).

How it fits here:
- Native records one `main_cb` per frame slot. There are three slots
  (`kFramesInFlight = 3`, `native_renderer.cpp:122`), each with its own fence
  (`native_renderer.cpp:300-307, 526-528`).
- Give each slot a query pool. Write a timestamp at the start and end of the
  frame and at each render-target switch (`BeginRendering`/`EndRendering`,
  `native_renderer.cpp:681, 839`). Write another around the resolve blits
  (`native_renderer.cpp:1513`) and the present blit (`native_renderer.cpp:2381`).
- Read the results when the slot's fence has signalled. The renderer already
  waits on that fence before reuse (`native_renderer.cpp:649-654`), so the
  readback adds no stall.
- Log GPU ms per frame, and per pass, in a per-frame CSV. That turns
  "supersampling costs X" into a real number. Ticket 02 of image-quality
  left that number open ("A per-frame GPU timer would give a real number",
  `.scratch/image-quality/issues/02-supersampling-2x2.md:54`).

**Tracy (CPU and GPU in one timeline).**
- Tracy profiles CPU zones, locks and context switches, and GPU zones for
  "OpenGL, Vulkan, Direct3D 11/12, Metal, OpenCL, CUDA, WebGPU"
  (https://github.com/wolfpld/tracy).
- The client is built in and starts on demand. What it lacks is zones:
  - `ZoneScoped` in the D3D hooks, `pipelines::Get`, `textures::Bind`,
    `UploadConstants` and the present;
  - a `TracyVkContext` on the Native queue, with `TracyVkZone` per pass.
- This is the tool for the guest-thread question: which host threads are
  busy, and whether they are busy working or spinning. Context-switch capture
  needs the profiled process to run elevated on Windows (Tracy manual,
  "Context switches"; **unverified** for this build).

**Debug labels.**
- Neither `src/` nor the SDK calls `vkCmdBeginDebugUtilsLabelEXT` or names
  objects (no match for `DebugUtilsLabel` / `SetDebugUtilsObjectName`).
- One label per render-target pass, plus names for the main targets, make
  RenderDoc, RGP and Nsight captures readable. It costs nothing when no tool
  is attached.

**Vendor GPU profilers** (for "why is this pass slow", after the timestamps
show which pass):
- **Radeon GPU Profiler** works with Vulkan on Windows 11 and on Linux, on
  RDNA and newer, including "Ryzen processors with Radeon graphics". It shows
  wavefront occupancy, barriers that "flushed caches, caused a
  synchronization point", and per-instruction timing
  (https://gpuopen.com/rgp/). That covers the owner's 7900 XT and the Deck's
  RDNA 2.
  - On RADV (Linux and the Deck), `MESA_VK_TRACE=rgp` writes RGP captures to
    `/tmp`. Trigger them with F1 (X11 only) or with `MESA_VK_TRACE_FRAME` /
    `MESA_VK_TRACE_TRIGGER` (https://docs.mesa3d.org/envvars.html,
    `MESA_VK_TRACE`). `rmv` gives Radeon Memory Visualizer captures.
  - Valve's Deck FAQ says RGP "works on Linux natively today", with native
    trace capture "something that we're looking into"
    (https://partner.steamgames.com/doc/steamdeck/faq). The Mesa variable is
    the route that exists today.
- **NVIDIA Nsight Graphics** profiles Vulkan. GPU Trace shows "hardware unit
  throughputs, cache hit rates, memory throughput"
  (https://developer.nvidia.com/nsight-graphics). Use it for the NVIDIA
  testers of M6.
- **RenderDoc** (Vulkan 1.4) is a debugger. Its author contrasts it with
  tools "primarily designed around the profiling of applications"
  (https://renderdoc.org/docs/getting_started/faq.html). Use it for pass
  structure, overdraw and target sizes, not for timings. Setup notes:
  `docs/research/tooling.md` §3.

**Frame-time logging on Linux and the Deck.**
- PresentMon is Windows only (https://github.com/GameTechDev/PresentMon).
- On Linux and the Deck use MangoHud:
  - `output_folder` plus `autostart_log` / `log_duration` (or `Shift_L+F2`)
    write a per-frame CSV;
  - `benchmark_percentiles` defaults to "97,AVG,1,0.1", i.e. 1 % and
    0.1 % lows;
  - leave `fps_limit` at 0, because the game paces itself
    (https://github.com/flightlessmango/MangoHud).
- The Deck's Quick Access Menu has a performance overlay (4 levels), a frame
  limit, a TDP limit and a manual GPU clock. No Valve primary page was found
  that documents these. The description comes from a third-party guide
  (https://tomsguide.com/how-to/how-to-enable-the-performance-overlay-on-the-steam-deck).
  For repeatable Deck runs, record the TDP and GPU-clock settings with each
  capture, or pin them.

### A.3 Metrics

Report these per marked window (intro, entrance 1, entrance 2, match), as
`analyze_hitches.py` already splits them:

| Metric | Source | Why |
|---|---|---|
| Displayed frame time: median, p99 (1 % low), p99.9 (0.1 % low), max; hitch count | PresentMon `DisplayedTime` / MangoHud | Whether the machine keeps up. The report already defines the 1 % low as the nearest-rank p99 interval (ticket 01) |
| Guest work per tick: mean, p99, max | tick log | CPU-side headroom: 16.67 ms minus this (33.3 ms at the 1/30 s ticks) |
| Renderer CPU ms per frame: median, p99 | the hitch timer, logged every frame (new) | Native's own CPU cost on the guest's render thread |
| GPU ms per frame, and per pass | timestamps (new); PresentMon `GPUBusy` today | GPU-side headroom; which pass to optimize |
| CPU busy per host thread | Tracy context switches / WPR on Windows, `perf` on Linux | Spinning threads, core oversubscription on 4c/8t |
| Draws, pipeline binds, uploads per frame | the 300-frame statistics line (`native_renderer.cpp` "frame N:" line) | Normalizes GPU and CPU time by load |

PresentMon defines `GPUBusy` as "the time during which at least one GPU
engine is executing work from the target process"
(https://github.com/GameTechDev/PresentMon, `README-ConsoleApplication.md`,
L98-103). It therefore includes the presenter's CAS and blits. It is a
whole-frame upper bound, not a per-pass number.

### A.4 A repeatable benchmark

- **Segment.** Use the smooth-presentation route the owner already plays
  (Hogan vs Cena, SmackDown, entrances not skipped, marks at intro, entrance
  1, entrance 2 and match; ticket 11's owner steps). For a hands-off variant,
  run `tests/scenes/exhibition-1v1.toml`'s input script (`--wwe_input_script`)
  in a window instead of headless. Owner rules: the owner plays matches by
  hand and long scripted runs are avoided, so the scripted variant is for the
  Deck, where an agent cannot play.
- **Clock.** Do not use `--wwe_lock_guest_time`. It caps time per frame and
  hides slowness (`src/frame_clock.h:1-9`). On desktop, keep
  `vblank_follows_display` at its default. On the Deck LCD (60 Hz) that is the
  panel. On the OLED (up to 90 Hz, https://www.steamdeck.com/en/tech/deck-oled),
  set the refresh to 60, or ADR-0016 falls back to the host clock with a 2-3
  pattern (inferred from ADR-0016's 0.5 % rule).
- **Warm-up.** Do one run to the match first: the pipeline store and the
  driver cache. Fresh-install runs are a separate bar (ticket 01).
- **Noise.** Make at least 3 runs per variant, with the baseline first and
  last. Ticket 11 measured the same exe at 4.16 and 3.37 ms per tick. Compare
  per-window medians. Count a change only when it exceeds the
  baseline-to-baseline spread (ticket 11's ship rule).
- **Settings.** Record every enhancement in each capture's `display.json`
  `notes`. The captures on disk mix 2x supersampling and 3x scale, which can
  only be told apart from `runtime.log` (the "render target 2560x1440" or
  "output 3840x2160" lines).
- **Pass bar for a minimum-spec machine.** The ROADMAP asks for "Steam Deck
  at native resolution and the original frame rate" (`docs/ROADMAP.md:35`).
  That is the zero-stutter match bar (1 % low within 16.7 ms, no hitch
  outside accepted loads) with:
  - guest work p99 under about 14 ms;
  - GPU p99 under about 14 ms.

  The margin of about 2.7 ms covers the 5 ms present offset and scheduling
  jitter. It is a proposal, not a measured requirement. Valve's own
  "Verified" bar is only "30fps at 800p"
  (https://partner.steamgames.com/doc/steamdeck/compat); the ROADMAP's bar is
  stricter.

## B. Target hardware

### B.1 Steam Deck

| | Steam Deck (LCD) | Owner's desktop GPU |
|---|---|---|
| CPU | "Zen 2 4c/8t, 2.4-3.5GHz (up to 448 GFlops FP32)" | not recorded in the repo |
| GPU | "8 RDNA 2 CUs, 1.6GHz (1.6 TFlops FP32)" | RX 7900 XT: 84 CUs, 2000 MHz game / 2400 MHz boost, 51.6 TFLOPs |
| Memory | "16 GB LPDDR5 on-board RAM (5500 MT/s quad 32-bit channels)", shared by CPU and GPU | 20 GB GDDR6, up to 800 GB/s |
| Power | "APU Power: 4-15W", shared by CPU and GPU | 315 W board power |
| Display | "1280 x 800", 60 Hz | 2560x1440 at 360 Hz (`display.json`) |

- Sources: https://www.steamdeck.com/en/tech/deck and the AMD RX 7900 XT
  product page
  (https://www.amd.com/en/products/graphics/desktops/radeon/7000-series/amd-radeon-rx-7900xt.html).
- The OLED model has a "6 nm APU" and a "1280 x 800 HDR OLED display" at "Up
  to 90Hz" (https://www.steamdeck.com/en/tech/deck-oled). Valve's page lists
  no different CPU or GPU figures. Assume the same APU at a better
  efficiency (**unverified**).
- **Bandwidth (estimate):** 4 × 32 bit × 5500 MT/s = 88 GB/s, shared with
  the CPU. That is about 9x less than the 7900 XT, and the Deck has no
  Infinity Cache.
- **CU × clock (estimate):** 8 × 1.6 = 12.8 against 84 × 2.0 = 168, about
  13x.
- Valve: "We recommend that all games have an FPS limit", and "Up to 8GB can
  be shared" with the GPU (https://partner.steamgames.com/doc/steamdeck/faq).
  The game's own limiter already caps at 60.
- **Driver:** RADV (Mesa). Vulkan 1.3 since Mesa 22.0
  (https://docs.mesa3d.org/relnotes/22.0.0.html).
  - Graphics pipeline libraries have been on by default since Mesa 23.1.
  - `dynamicPrimitiveTopologyUnrestricted` is false on the Deck.
  - present_wait v1 is available; wait2 since Mesa 25.2.
  - Sources: `zero-stutter.md` support table.
- **Pipeline caching on the Deck.** Steam's shader pre-caching is built on
  Fossilize, "a library and Vulkan layer" that serializes shader modules and
  pipelines (https://github.com/ValveSoftware/Fossilize). Whether it covers a
  non-Steam shortcut is **unverified**. ADR-0019 already prebuilds every
  shipped key at startup. On the Deck that prepare step runs on 8 logical
  cores (`src/native/pipelines.cpp:907-909`) and its duration should be
  measured. Mesa's own disk cache keeps the results between runs.

### B.2 Does the port run on Linux or Proton?

- **Not yet, and never tried.** ADR-0006: "Linux and Steam Deck arrive at
  beta (M6)". The ROADMAP lists M6 as "Linux build, Steam Deck at minimum
  spec" (`docs/ROADMAP.md:52`). The tooling study planned a Flatpak in the
  ReXGlue 22.04 container (`tooling.md` §7). The repo has Linux presets from
  ReXGlue (`CMakePresets.json:22-33, 93-101`), but no Linux build has been
  done.
- Windows-only code in `src/`: `pipelines.cpp`, `topup_converter.cpp`,
  `resolve_watch.cpp`, `settings/schema_export.cpp`,
  `settings/settings_page.h` and `startup_paths.cpp` include Windows headers
  or `_WIN32` paths. The scripts are pwsh plus PresentMon (Windows).
- **Native's device requirements** are buffer device address, runtime
  descriptor arrays with partially-bound and update-after-bind, shaderInt64
  and dynamic rendering (`src/native/vk_context.cpp:153-160`). All are core
  Vulkan 1.2/1.3 (dynamic rendering through `KHR_dynamic_rendering` before
  1.3, `src/ui/vulkan/vulkan_device.cpp:203, 235`). RADV advertises 1.3 (B.1).
- **Classic** prefers the FSI path. It falls back to host render targets
  when fragment shader interlock is missing
  (`src/graphics/vulkan/render_target_cache.cpp:203-236`). Whether RADV on
  the Deck exposes interlock is **unverified** (`tooling.md` §7).
- **Proton as an early probe (inferred).** The Windows exe talks Vulkan
  directly. Under Proton, its Vulkan calls reach RADV through Wine's
  Vulkan layer, with no DXVK in between. Valve recommends Vulkan "for best
  performance and battery life"
  (https://partner.steamgames.com/doc/steamdeck/recommendations). A
  Proton run before M6 would give real Deck GPU numbers. Its risks are not
  known: the Avalonia launcher, file paths, the waitable timers of the
  pacing patches and the guest's write-watch faults under Wine.

### B.3 A reasonable "older hardware" floor

- Native refuses a device without the features in B.2
  (`vk_context.cpp:153-160`). That is the hard floor today.
- **NVIDIA:** the current Vulkan driver page lists Blackwell, Ada, Ampere and
  Turing as supported (https://developer.nvidia.com/vulkan-driver). GTX
  900/1000 (Maxwell/Pascal) need an older branch. Their Vulkan version and
  `shaderInt64` / descriptor-indexing support on that branch should be read
  from gpuinfo before they are promised (**unverified**).
- **Intel:** Arc lacks present_id2/wait2 (`zero-stutter.md` support table).
  Older iGPUs (Gen9/Gen11): **unverified**. Check `shaderInt64` and
  `descriptorBindingSampledImageUpdateAfterBind` on gpuinfo.
- **CPU:** the default build is `x86-64-v2` (`scripts/build.ps1:73`). A v3
  build needs AVX2/FMA/MOVBE/BMI2: 97.7 % of Windows Steam users by AVX2
  (`zero-stutter.md` §E). The Deck's Zen 2 has these (inferred from the Zen 2
  ISA). A v3 exe would therefore run on the Deck.
- **Proposal:** treat the Deck as the minimum (the ROADMAP's own words). It
  doubles as the reference for "a 4c/8t CPU with a small iGPU-class GPU and
  shared memory". Below that, promise nothing that has not been tested.

## C. Where the cost is, and the levers

### C.1 Measured today (RX 7900 XT, owner's sessions)

PresentMon `GPUBusy` and `FrameTime` per present, whole sessions (menus,
loads and matches mixed):

| Capture | Native setting | Frames | GPUBusy median / p95 / p99 (ms) |
|---|---|---|---|
| `capture-game-20261003-191337` | 2x2 supersample (2560x1440 targets) | 9,728 | 1.91 / 3.43 / 7.28 |
| `capture-game-20261003-220943` | 2x2 supersample | 33,207 | 1.58 / 2.40 / 2.78 |
| `capture-game-20261004-192110` | 2x2 supersample, fresh install | 26,743 | 2.31 / 2.70 / 3.38 |
| `capture-game-20261003-230821` | scale 3 (3840x2160) | 17,690 | 3.13 / 4.04 / 4.67 |
| `capture-game-20261004-102736` | scale 3 | 140,129 | 3.12 / 3.85 / 5.36 |
| `capture-game-20261004-090711` | scale 3 | 38,365 | 3.43 / 4.15 / 5.64 |
| `capture-canary-20261003-174412` | Xenia Canary (reference) | 35,842 | 2.20 / 2.50 / 3.11 |

- The setting is read from each `runtime.log`: "colour render target
  2560x1440", or "output 3840x2160" with "--wwe_native_supersample is
  ignored with --wwe_native_resolution_scale=3". All captures were at
  2560x1440 on a 360 Hz panel, with CAS 0.5 or 1.
- **Guest work per tick** (tick log, seconds with at least 58 ticks and none
  at 1/30 s):
  - `192110`, 376 s: median 3.32 ms, p90 6.06, p99 7.45, max 7.71.
  - `102736`, 2,265 s: median 3.25, p99 6.52.
  - Seconds at the 1/30 s step (intro, entrances): median 13.95 ms per tick
    (55 s) and 12.71 ms (16 s), against a 33.3 ms budget. Ticket 10 found the
    same 13 ms.
- **Renderer CPU on hitch frames** (`192110`, the only frames it logs):
  0.0–0.4 ms. Ticket 01 summarizes the same capture as "renderer under 3 ms
  and GPU fence 0" on all 50 hitches.
- What these say:
  - On the desktop every side has large headroom.
  - GPU time grows less than the pixel count: 2.25x the pixels for about
    1.5x the time. A fit of t = a + b·Mpx through the two medians gives
    about 1.0 ms fixed plus 0.26 ms per million pixels.
  - **Estimate, and weak:** the captures are different scenes, `GPUBusy`
    includes the presenter, and a lightly loaded 7900 XT runs well below its
    boost clock.

### C.2 Steam Deck projection (estimate, not measured)

- Scaling the fit by the 9–13x ratio of B.1:
  - 1x (0.92 Mpx) is about 1.25 ms on the 7900 XT, so about 11–16 ms on the
    Deck.
  - 2x2 supersampling (3.7 Mpx) is about 2.0 ms, so about 18–26 ms.
- Errors run both ways:
  - The desktop downclocks at this load, which inflates the ratio.
  - The Deck's 88 GB/s is shared with the CPU, and the power budget is
    shared with spinning CPU threads, which deflates it.
- Conclusion: **supersampling must be off in a Deck profile. 1x is plausible
  but unproven.** The Deck's 1280-pixel width equals the game's 1280x720, so
  1x is already "native resolution". An upscaler (FSR) has nothing to
  recover there.
- **CPU:** the owner's CPU model is unknown, so the 3.3 / 7.5 / 14 ms figures
  cannot be scaled. A Zen 2 core at 2.4–3.5 GHz inside a 15 W shared budget
  is likely slower per thread than a current desktop core (**inferred**). The
  half-rate intro ticks (about 14 ms, budget 33.3 ms) have room. The 60 Hz
  p99 of 7.5 ms has room only if the Deck is under about 2x slower per thread.
  This is the first number to measure on a Deck.

### C.3 Where the cost sits in code

**GPU (Native):**
- **Target resolution.** `wwe_native_supersample` is **on by default**
  (`src/settings_cvars.cpp:11-14`). It renders screen-sized targets at
  2560x1440 (`guest::TargetScale`, `native_renderer.cpp:689-691`) and
  filters them down at present. That is 4x the pixel and bandwidth work for
  the main scene. Scale 1–3 multiplies the same
  (`settings_cvars.cpp:20-24`).
- **Depth format.** `VK_FORMAT_D32_SFLOAT_S8_UINT` for every depth target
  (`native_renderer.cpp:296`). That is 5 or more bytes per pixel of depth
  traffic. A packed D24S8 is not available on AMD (inferred, from AMD's
  lack of `D24_UNORM_S8_UINT` optimal-tiling support; check on gpuinfo). So
  this is probably fixed on RDNA.
- **Barriers.** Every layout change is a full
  `ALL_COMMANDS → ALL_COMMANDS` barrier with `MEMORY_READ|WRITE` on both
  sides (`native_renderer.cpp:449-464`; the comment says "the frame has few
  layout changes"). On a big GPU that is cheap. On an 8-CU part each one
  drains the whole pipe. RGP's barrier view
  (https://gpuopen.com/rgp/) will show whether they matter. Narrow stage and
  access masks per transition are a contained change.
- **Resolves** are `vkCmdBlitImage` copies (`native_renderer.cpp:1513`). They
  are bandwidth, and they are 4x larger under supersampling.
- **Present path.** The front buffer is blitted into the frame slot
  (`native_renderer.cpp:2381`). The presenter then applies the gamma ramp,
  CAS or FSR (`ui/presenter.cpp:47-77`;
  `src/graphics/vulkan/command_processor.cpp:2364-2373`). That is two
  full-screen passes at output size.
- **Descriptors are already cheap.** Native uses one bindless
  update-after-bind heap per type (`src/native/vk_context.cpp:42-92`),
  constants by buffer device address in push constants, and skips repeated
  pipeline binds and pushes (`native_renderer.cpp:1795-1811`). There is
  little to win here.
- **EDRAM emulation is already gone in Native.** With an external frame, the
  SDK's `IssueDraw` returns at once. The PM4 stream is still parsed "for
  fences and interrupts" (`src/graphics/vulkan/command_processor.cpp:3657-3661`).
  The cost Classic pays for EDRAM is not in Native's frame. The PM4 parse is
  CPU work on the GPU command-processor thread. Measure it in Tracy (the
  SDK's coarse "gpu" zones, `src/graphics/command_processor.cpp:610-1690`).
- **Async compute.** Native has no compute work at all: one graphics queue,
  one upload command buffer and one main command buffer per slot
  (`native_renderer.cpp:300-307`, submit at `2427`). Async compute needs
  overlapping compute work, so this is not a lever for this game.

**CPU:**
- **Two busy-wait loops.**
  - The game's frame limiter spins on `appSleep(0.0)` until 1/60 s has
    passed (`movement-judder.md` §1, `0x82417E40–0x82417EC0`).
  - D3D's GPU-wait loop `0x82DB6710` runs about 885k calls per present
    (`native-renderer.md`, attribution sweep). It spins with PPC priority
    hints that do nothing on the host (`entrance-video-stutter.md` §F). No
    hook exists in `src/` outside the sweep build (`src/d3d_sweep.gen.cpp:1221`).
  - wwe-svr2011-pc yielded its counterpart and the render thread dropped
    "from 98% to 51% of a core" (`svr2011-pc-port.md`, Performance).
  - On a 4c/8t, 15 W APU, two spinning threads take cores from the guest's
    real threads and power from the GPU (**inferred**, from the shared
    "APU Power: 4-15W").
- **Host thread count.** Next to the guest's threads the process runs:
  - 2 texture workers (`src/native/textures.cpp:658-660`);
  - pipeline workers, `clamp(cores/4, 2, 4)` during play
    (`pipelines.cpp:907-909`), which is 2 on the Deck;
  - (since low-end-preset ticket 12, `src/native/thread_budget.h`: 1 + 1 on 8 or
    fewer logical CPUs, else `clamp(cores/4, 2, 4)` of each;)
  - the SDK's command processor, vsync, presenter pacing, audio and XMA
    threads.

  The guest's affinities and priorities are ignored by default
  (`src/system/xthread.cpp:40-44`). On 8 logical CPUs the thread count, not
  the code speed, may decide the frame (**inferred**). Tracy context switches
  will show it.
- **Recompiled code.**
  - Every guest load and store is a `volatile` access with a byte swap. On
    Windows each address also gets a compare for the physical-range offset
    (`generated/default/wweallstars_pch.h:119-139`).
  - `volatile` stops the compiler from merging or dropping repeated guest
    loads. The cost of that is **unmeasured**, and it is an SDK codegen
    decision.
  - MMIO is a range check at only 36 sites in the generated code
    (`REX_MM_*`, `wweallstars_pch.h:146-200`). It is negligible.
  - The byte swap itself is one instruction. Under `x86-64-v3` (which
    includes MOVBE per the x86-64 psABI levels,
    https://gitlab.com/x86-psABIs/x86-64-ABI) it can fold into the load.
  - The `fma` CRT calls are removed by v3 (ticket 11: 381 of 383 sites in 10
    files).
- **Renderer CPU per draw.** Two 4 KB constant blocks are compared and, when
  changed, byte-swapped into a 64 MB host-coherent ring
  (`native_renderer.cpp:993-1012`, `kUploadRingSize` at line 123). About 600
  draws at peak (`native-renderer.md`, gameplay measurement) bound this to a
  few MB a frame. The hitch line shows under 0.5 ms on desktop. Textures are read,
  hashed and converted on 2 worker threads (`ReadTexture`,
  `textures.cpp:440-470`) and cached behind write watches. That is mainly a loading cost.

### C.4 Levers, ranked by expected payoff

Payoff is an estimate. None of these has been measured on low-end hardware.

| # | Lever | Exists today? | Expected payoff | Risk / cost |
|---|---|---|---|---|
| 1 | **Low-end profile:** supersampling off, scale 1, CAS off, anisotropy 2x or lower | All four are settings (`settings_cvars.cpp:7-29`) on the PC SETTINGS page (`src/settings/registry.h`) | High on GPU: about 4x fewer main-scene pixels (C.2) | None in code. Needs a default chosen by GPU (the launcher already lists devices, `src/settings/gpu_list.cpp`) |
| 2 | **Yield in the two spin loops** (limiter `appSleep(0)`, GPU wait `0x82DB6710`) | No (sweep hook only) | High on 4c/8t and 15 W; low on desktop | Hooks need instruction-level evidence (manifest rules). Pacing must not regress (ticket 13/23 bars). SVR2011 shows the pattern |
| 3 | **`x86-64-v3` build** (ticket 11) | Built as a variant, not shipped | Medium on guest CPU (FMA, MOVBE, AVX2). The route capture decides | A second exe and a CPU check at startup (ticket 11 criterion 3) |
| 4 | **Narrow barriers** | No | Medium on small GPUs, about 0 on desktop (inferred) | Low; validate with sync validation |
| 5 | **Forced 4x MSAA instead of SSAA** as the Deck's AA | No (`native-image-quality.md` B; map: "only for a low-end or Deck profile") | Medium: edge AA at a fraction of 2x2 fill | Pipeline key, colour and depth resolves (`native-image-quality.md` B) |
| 6 | **Sub-allocated memory** (VMA instead of a dedicated `vkAllocateMemory` per texture and target) | No (`zero-stutter.md` §F.1) | Stutter more than average cost | Medium |
| 7 | **ThinLTO / PGO** (ticket 12) | Variants exist (`scripts/pgo_profile.py`) | Unknown | Build time, link RAM |
| 8 | **Thread budget:** fewer pipeline and texture workers during play on ≤ 8 threads | Partly (`pipelines.cpp:907-909` scales; textures fixed at 2) | Low to medium, Deck only | Low |
| 9 | **Lower-than-720p internal resolution plus FSR1** | FSR1 is in the presenter (`present_effect=fsr`, `ui/presenter.cpp:47-51`). Native's scale floor is 1 (`settings_cvars.cpp:20-24`) | Only if 1x still misses on the Deck | FSR1 wants "well anti-aliased" input before the UI (`native-image-quality.md` B). Native composites the HUD into the same target, so it would also upscale the HUD |
| 10 | Async compute, descriptor rework, FSR2/3 | n/a | ≈ 0 for this workload | FSR2/3 needs motion vectors the game does not produce (`native-image-quality.md` B) |

Shadow and post-process quality are not a lever on Native. Native draws the
game's own passes at the game's sizes. Scale and supersampling apply only to
screen-sized targets; the bloom target stays 400x182
(`native-image-quality.md` A, render-target table). A "lower shadows" option
would mean changing the game's draws, which is out of scope.

## Proposed next steps

1. **GPU timer in Native.** Timestamp queries per frame slot, one at frame
   start and end and one per render-target pass and resolve. Log GPU ms per
   frame and per pass to a per-frame CSV, beside the renderer CPU ms the
   hitch timer already collects. Acceptance: supersampling on versus off in
   a match, as a number in image-quality ticket 02.
2. **`GPUBusy` in the smoothness report.** `analyze_hitches.py` prints the
   median and p99 `GPUBusy` per window from data every capture already has.
3. **Tracy zones.** CPU zones in the D3D hooks, pipelines, textures, constants
   and present, plus `TracyVkZone` per pass. Then one Tracy capture of the
   match with context switches, to name the busy host threads and the
   spinners.
4. **Debug labels** per pass and object names for the main targets, for
   RenderDoc, RGP and Nsight.
5. **Benchmark wrapper.** `capture.ps1 -Benchmark` writes the settings into
   `notes` and runs N captures. A summary script gives the A.3 metrics per
   window, with baseline-to-baseline spread.
6. **Research ticket: the two spin loops.** Disassemble `0x82608FD8`
   (`appSleep`) and `0x82DB6710`. Propose yield or sleep hooks with
   instruction-level evidence. Measure CPU busy per thread before and after,
   and hold the pacing bars.
7. **Low-end profile.** A "Steam Deck / low" preset: supersampling off, CAS
   off, scale 1. The launcher picks it by GPU. Measure on the owner's machine
   with the GPU clock capped, as a stand-in until a Deck is available.
8. **Deck probe.** The Windows build under Proton on a Deck, with MangoHud
   logging and `MESA_VK_TRACE=rgp`, on the scripted 1v1 scene. Yield: real
   GPU ms, guest work per tick and the prepare-step duration. If Proton
   fails, record why for M6.
9. **Finish ticket 11's route capture.** The v3/O3 decision helps every
   CPU-bound machine.
10. **Narrow barriers** once RGP or timestamps show barrier stalls.

## Open questions

- What CPU does the owner's desktop have? It is needed to scale the measured
  guest work to the Deck.
- Can a Steam Deck be borrowed, or a tester found, before M6? Every Deck
  number here is an estimate.
- Does the Windows exe run under Proton on a Deck, launcher included? Does
  Steam's Fossilize pre-caching apply to a non-Steam shortcut?
- Which GTX 900/1000-class and Intel iGPU drivers expose every feature Native
  requires (`vk_context.cpp:153-160`)? Should those GPUs be in scope at all?
- Should the Deck profile use forced 4x MSAA (map: "only for a low-end or
  Deck profile"), or no AA plus CAS?
- How much of PresentMon's `GPUBusy` is the presenter (gamma, CAS) rather
  than Native's passes? Timestamps (step 1) settle it.
- Does the 60 Hz pacing (ADR-0016, present wait) behave under gamescope on
  the Deck, and on the 90 Hz OLED set to 60?

## Sources

- Repo:
  - `src/native/native_renderer.cpp` (122-123, 296-307, 394-428, 449-464,
    649-654, 681-691, 839, 993-1060, 1082-1087, 1513, 1795-1811, 2109-2145,
    2381, 2427)
  - `src/native/vk_context.cpp` (42-92, 153-160), `src/native/pipelines.cpp`
    (907-909), `src/native/textures.cpp` (440-470, 658-660)
  - `src/settings_cvars.cpp`, `src/game_tick_log.cpp`, `src/frame_clock.h`,
    `src/main.cpp`, `src/wweallstars_app.h`
  - `generated/default/wweallstars_pch.h` (119-200)
  - `scripts/capture.ps1`, `analyze_hitches.py`, `compare_builds.py`,
    `analyze_perf.py`, `scene_runner.py`, `package.ps1`, `build.ps1`
  - `tests/scenes/exhibition-1v1.toml`
- ReXGlue:
  - `CMakeLists.txt` (24-32), `include/rex/dbg.h` (48-103),
    `include/rex/perf/counter.h`, `src/core/perf/counter.cpp`
  - `src/graphics/command_processor.cpp` (1020-1035),
    `src/graphics/vulkan/command_processor.cpp` (2364-2373, 3650-3661),
    `src/graphics/vulkan/render_target_cache.cpp` (203-236)
  - `src/ui/presenter.cpp` (40-80), `src/ui/overlay/debug_overlay.cpp`,
    `src/ui/vulkan/vulkan_device.cpp` (203-259), `src/system/xthread.cpp`
    (40-44), `src/system/mmio_handler.cpp`
- Captures: `analysis/capture-game-2026100{3,4}-*` (`presentmon.csv`,
  `runtime.log`, `display.json`), `analysis/capture-canary-20261003-174412`.
- Docs: ADR-0002, 0003, 0006, 0015, 0016, 0019; `docs/ROADMAP.md`;
  `docs/research/zero-stutter.md`, `native-image-quality.md`,
  `native-renderer.md`, `movement-judder.md`, `entrance-video-stutter.md`,
  `svr2011-pc-port.md`, `tooling.md`, `xenia-baseline.md`.
- Tickets: `.scratch/zero-stutter/issues/01, 10, 11`;
  `.scratch/image-quality/map.md`, `issues/02`.
- External:
  - Steam Deck: https://www.steamdeck.com/en/tech/deck,
    https://www.steamdeck.com/en/tech/deck-oled,
    https://partner.steamgames.com/doc/steamdeck/faq,
    https://partner.steamgames.com/doc/steamdeck/compat,
    https://partner.steamgames.com/doc/steamdeck/recommendations
  - AMD and Mesa: AMD RX 7900 XT,
    https://www.amd.com/en/products/graphics/desktops/radeon/7000-series/amd-radeon-rx-7900xt.html;
    Mesa, https://docs.mesa3d.org/envvars.html and
    https://docs.mesa3d.org/relnotes/22.0.0.html
  - Vulkan timestamps:
    https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp2.html
    (spec source `KhronosGroup/Vulkan-Docs` `chapters/queries.adoc`)
  - Profilers: Tracy, https://github.com/wolfpld/tracy; RGP,
    https://gpuopen.com/rgp/; Nsight Graphics,
    https://developer.nvidia.com/nsight-graphics; RenderDoc,
    https://renderdoc.org/docs/getting_started/faq.html
  - Frame-time logging: PresentMon, https://github.com/GameTechDev/PresentMon
    (`README-ConsoleApplication.md`); MangoHud,
    https://github.com/flightlessmango/MangoHud
  - Drivers, caching and the ISA: Fossilize,
    https://github.com/ValveSoftware/Fossilize; NVIDIA Vulkan driver support,
    https://developer.nvidia.com/vulkan-driver; x86-64 psABI levels,
    https://gitlab.com/x86-psABIs/x86-64-ABI
  - Secondary, Deck overlay controls only:
    https://tomsguide.com/how-to/how-to-enable-the-performance-overlay-on-the-steam-deck
