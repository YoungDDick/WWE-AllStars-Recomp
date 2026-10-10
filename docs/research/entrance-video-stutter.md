# Stutter during the arena intro video and wrestler entrances

Date 2026-10-03. The owner's report: the arena intro video before a match and
the wrestler entrances stutter, and RTSS shows frametime spikes. Addresses are
base XEX v5. ReXGlue paths are relative to `.tools/rexglue-source` (`f5337cdc`
plus `patches/`). Logs are the owner's hand-played sessions under `analysis/`
(gitignored). Unless a finding is marked **confirmed**, it is a hypothesis
with the stated confidence.

## Answer

Two separate effects add up to the stutter. A third affects only cold caches.

1. **Native reads back resolves the CPU never reads (confirmed by
   correlation, high confidence).** `QueueReadback` copies a resolve target
   to the CPU on the first frame of each "run" of consecutive resolves. A
   target the game resolves at 30 Hz (every second frame) starts a new run on
   every resolve, so it is read back every time. Each readback allocates a
   dedicated host buffer, maps it, swaps red and blue, downsamples it when
   supersampling is on, and writes it into guest memory pixel by pixel. During
   the arena intro and the entrances, renderer time per readback is about
   1.8 ms at 1x and about 7.5 ms with 2x2 supersampling (4x the pixels).
   Frames that do one or more readbacks take 30–110 ms. Ticket 06 found that
   the CPU reads none of these targets in a match, so the readbacks are wasted
   work.
2. **The guest itself runs at 30 Hz during the arena intro (confirmed on
   both renderers; cause is a hypothesis).** For 4–10 s every frame is
   exactly 33.3 ms, with about 1 ms in the Native hooks, and Classic shows the
   same cadence. That pattern means guest work of more than 16.7 ms per frame,
   rounded up to two vblanks. The likely work is 720p Bink decoding plus
   package and level streaming hidden behind the video. The game might also
   simply run at 30 Hz here on purpose. Either way, the renderer is not the
   cause.
3. **Cold caches cost seconds on Classic and only visual pop-in on Native
   (medium confidence).** The first Classic exhibition had single
   350–930 ms frames at the start of the arena intro and every few seconds
   through the entrances. On Native, a cold pipeline cache skips draws
   (`pipeline_pending`, about 2,300 in `play-39`) instead of stalling. With a
   warm cache, Native compiled nothing on the render thread.

The other candidates are ruled out or minor on the evidence so far: texture
streaming and upload, file I/O, XMA audio and the Canary loop patch, sleep
granularity, and pipeline compiles on a warm cache (see C to G).

## Evidence

### A. Native hitch log (already built in)

`LogHitch` (`src/native/native_renderer.cpp:1902-1931`) logs every present
interval over 25 ms (`kHitchMs`, `:1903`). Each line records the total frame
time and the time spent in the Native hooks ("renderer", `HookTimer`
`:396-399`). It splits that time into GPU fence wait, pipelines compiled on
this thread, draws waiting for pipelines, texture uploads, reads, write-watch
and hashing, geometry bytes, and new render targets. **Readbacks are not
broken out**, but `DeliverReadbacks` runs inside `BeginFrameIfNeeded`
(`:599-631`, call at `:610`), so readback time is counted in "renderer" and
in none of the sub-items.

Sessions used: `analysis/play-39` (Native 1x, no enhancements, pipeline cache
partly cold), `analysis/play-iq02` and `analysis/play-iq03` (Native, 2x2
supersampling and CAS, warm cache of 228 pipelines; `report.json`). Hitches
were split by whether renderer time is above or below half the frame time
(throwaway parser in the session scratchpad, not committed):

| Session | Frames | Phase | Hitches / 60 frames | Mean frame | Renderer mean | Pattern |
|---|---|---|---|---|---|---|
| iq03 | 2760–2880 | arena intro video | 60 / 60 | 33.3 ms | 0.8–1.0 ms | every frame, guest-bound |
| iq03 | 2880–3060 | intro → entrances | 17–29 | 51–61 ms | 32–41 ms | 7–12 renderer-bound frames per 60, up to 112 ms |
| iq02 | 2460–2640 | arena intro video | 60 / 60 | 33.3 ms | 0.7–1.0 ms | every frame, guest-bound |
| play-39 | 4080–4980 | entrances | 30–40 | 29–32 ms | 9–16 ms | every second frame renderer-bound (about 22–29 ms) |
| all | end of entrances (iq03 3240, play-39 5040, iq02 2940) | load into the match | 6–8 | 77–93 ms | 0.4 ms | guest-bound load |
| all | in match | | 0–19 per 300 | 30–33 ms | 0.4–2 ms | rare single misses, about 3 Bink textures uploaded |

In the renderer-bound frames, the logged sub-items are zero or close to it:
for example, iq03 frame 2744 took 111.7 ms, 107.9 ms of it in the renderer,
with fence 0.0 ms, no pipelines, no textures and no geometry. Native's
statistics line every 300 frames (`frame N: … readbacks=R`) gives the
readback count, which matches the renderer time:

| Session (scale) | Frames | New readbacks | Renderer time in hitch frames | Per readback |
|---|---|---|---|---|
| play-39 (1x) | 3900–4200 | 402 | 780 ms | 1.9 ms |
| play-39 (1x) | 4200–4500 | 928 | 1,703 ms | 1.8 ms |
| play-39 (1x) | 4500–4800 | 994 | 1,708 ms | 1.7 ms |
| play-39 (1x) | 4800–5100 | 768 | 1,369 ms | 1.8 ms |
| play-39 (1x) | 5100–18000 | 24 | < 25 ms per window | — |
| iq03 (2x2) | 2700–3000 | 284 | 2,192 ms | 7.7 ms |
| iq03 (2x2) | 3000–3300 | 95 | 666 ms | 7.0 ms |
| iq03 (2x2) | 3600–30000 | 1 | < 25 ms per window | — |

"Renderer time in hitch frames" is a lower bound, because frames under 25 ms
are not logged. The roughly 4x ratio between 2x2 and 1x matches the 4x pixel
count. Frame rate over those windows: play-39 fell to 39.6–47.2 fps for about
20 s (frames 3900–5100), and iq03 to 31.9–40.4 fps (2700–3300). It was
60.0 fps everywhere else in the match.

### B. Why Native reads back so much

- `QueueReadback` (`native_renderer.cpp:555-592`) is called for every colour
  resolve with a fetch target (`:1316`), with no cvar. It starts a new run
  when `run.last_frame + 1 < g.frame` (`:559`) and copies the first frame of
  each run. A target resolved every second frame never forms a run.
- Each copy creates a new dedicated host buffer (`:576-580`). That buffer is
  freed after delivery (`DeliverReadbacks` `:504-550`). On the CPU, delivery
  then does a red/blue swap over the scaled image (`:526`), a `Downsample`
  when scale > 1 (`:534`), and `textures::WriteToGuest`
  (`src/native/textures.cpp:918-949`). For tiled targets, that last step does
  one `GuestOffset` calculation and one 4-byte `memcpy` per pixel, plus
  `write_watch::MarkWritten`.
- The 30 Hz targets appear right when the arena intro starts: new
  320x256 and 560x512 render targets and resolves are logged at
  `play-iq03/runtime.log` 16:53:16.860 and 16:53:17.035 (`play-39`:
  16:35:12.591 and .655). The 512x512 and 256x256 resolve destinations from
  ticket 06 and ticket 31 (`0x188F0000`, `0x189F0000`, `0x15010000`) are
  resolved thousands of times per match.
- Ticket 06 (`.scratch/playable-build/issues/06-cpu-read-resolves.md`) watched
  the guest CPU's page accesses: 11 resolve destinations from boot to the
  result screen, 0 CPU reads. The comment on `DeliverReadbacks` names created
  attire bakes as the reason for the readback (a Create-a-Superstar case,
  outside the playable-build map).
- Precedent: SVR2011 hit the same class of bug. Its finisher and signature
  hitches came from resolve readback and were fixed by never waiting and by
  burst detection with a streak limit
  (`D:\Projects\SVR2011-RECOMP\docs\performance.md:639-689`). Classic here has
  the same logic, with readback off by default
  (`src/graphics/command_processor.cpp:81`, `vulkan/command_processor.cpp:4560-4566`).

### C. Video decoding (Bink 1, guest-side)

- **Confirmed: the format is Bink 1.** `assets/Game/Movies/` holds 164
  `.bik` files, all `BIKi`, and no XMV or WMV. Header data: `ARENA_001.bik` to
  `ARENA_006.bik` are 1280x720, 300 frames, 30 fps, about 10 Mbit/s, with no
  audio track. Per-wrestler entrance videos are `NNN_JumboTron.bik` at
  512x512 and 30 fps (36 files). There are also 59 files at 320x240 and a few
  at 256x256.
- **Confirmed: decoding runs in recompiled guest code.** The image has
  `BINKCONS`, `BINK` (code, `0x83500E00`–`0x83512AD4`, outside `.text`),
  `BINKBSS` and `BINKDATA` sections. It also has the UE3 classes
  `UCodecMovieBink`, `UTextureMovie` and `USD_SeqAct_PlayFullScreenMovie`,
  and the shaders `FBinkYCrCbToRGB…PixelShader` (UTF-16 strings at
  `0x820BB610`, `0x820EB4D0`, `0x82007840`, `0x82176F20`). ReXGlue has no Bink,
  XMV or XMedia code (`grep -i "bink\|xmv\|xmedia"` over `src` and `include`
  finds nothing). The recompiler generated 147 functions in
  `0x8350xxxx`–`0x8351xxxx`.
- **Confirmed: the upload cost of movie textures is small.** In-match and
  intro hitch lines show 3 textures per frame at 0.38 MB (Y 512x512 plus two
  256x256 chroma planes) or 0.09 MB (one 256x256 movie), with 0.1 ms on the
  render thread. The full-screen 720p arena video does not show up as
  uploads of 1.4 MB per frame. Which path it is drawn through is not
  established.
- **Hypothesis (medium): the 33.3 ms cadence during the arena video is guest
  CPU time.** Candidates are the 720p Bink decode and UE3 async loading
  (`AsyncLoading`, `PrecachedInGamePackages`, `USD_StreamingGroup` strings;
  zlib 1.2.3 in the image at `0x822735C9`) behind the video. The THQ boot
  video is also 1280x720 at 30 fps, yet it plays with the game at 59.9 fps
  (iq03 frames 300–600). Bink decoding alone may therefore not be enough to
  push frames over 16.7 ms. Classic shows the same cadence: 33 ms frames,
  sometimes 48 ms, in rows 3745–3860 of
  `analysis/runtime-20261002-220750/perf.csv`. So it does not depend on the
  renderer. SVR2011 runs its entrances at 30 fps on
  purpose (`sub_826E1C88` halves the frame step; `performance.md:607-612`), but
  All Stars is a different engine (UE3), so that does not carry over.

### D. Shader and pipeline compilation

- **Native:** pipelines are cached on disk with keys plus driver cache data,
  and are prebuilt on 2–4 worker threads at startup
  (`src/native/pipelines.cpp:395-447`; `play-iq03` prebuilt 228 in 7 ms).
  New pipelines compile asynchronously. Draws into the main scene are skipped
  while they compile, but off-screen targets (shadow maps, the new
  320x256/560x512 targets) compile synchronously on the render thread
  (`native_renderer.cpp:1567-1577`; `pipelines.cpp:449-475`). Measured: warm
  cache (iq02/iq03) gives 0 synchronous compiles and 0 pending draws in the
  intro and entrances. Cold cache (play-39) gives 2,365 `pipeline_pending`
  skips from frame 3840 (arena intro), 1–2 synchronous compiles, and no
  measurable stall. Ruled out as the cause of the stutter on a warm cache.
  Pop-in on a cold cache remains. Ticket 32 (pipelines still pending on the
  second run) is a separate gap in the cache.
- **Converted shaders:** XenosRecomp turns guest shaders into SPIR-V ahead of
  time (`docs/research/native-renderer.md`, ADR-0012). A handful logged as
  "not in the converted cache" around boot are unrelated to entrances.
- **Classic (hypothesis, medium):** `async_shader_compilation` is on by
  default (`src/graphics/command_processor.cpp:112`). Without
  `--vulkan_async_skip_placeholder_pipelines` (default false,
  `patches/rexglue-async-pipelines.patch`), the GPU thread compiles a
  placeholder pipeline per new pipeline, and
  `vulkan_async_skip_incomplete_frames` (default true,
  `vulkan/command_processor.cpp:68, 2321-2330`) drops the present of any frame
  that used a placeholder. SVR2011 measured 40–70 ms per pipeline this way
  (`performance.md:596-604`). The first Classic exhibition
  (`analysis/runtime-20261002-220750/perf.csv`) had single frames of 867 and
  933 ms at the start of the arena intro (rows 3743–3744), then 683, 448, 400,
  350 and 365 ms. Through the entrances there followed 416, 432, 332, 335,
  385, 433 and 384 ms (rows 3877–5253). Its `pipeline_cache_misses` column is
  always 0 because the Vulkan backend never sets it (only D3D12 does), so this
  log cannot tell pipelines from textures.

### E. Textures, streaming and file I/O (minor)

- Native texture uploads in the slow windows are at most 0.5 MB per frame,
  with at most 12 ms of render-thread time and 34 ms of guest-thread reads per
  300 frames (A). Write-watch and hash times are about 0.
- `NtReadFile` reads synchronously on the calling guest thread, with no
  emulated disc latency (`src/kernel/xboxkrnl/xboxkrnl_io.cpp:187-240`). UE3
  does its loading on its own `AsyncIOSystem` thread. A slow read would show
  up as a guest-bound frame, as in the 77–93 ms frames when the match loads.
  There is no direct evidence yet.

### F. Synchronisation, timers and audio (minor)

- Guest threads keep host affinity and priority unless told otherwise:
  `ignore_thread_affinities` and `ignore_thread_priorities` are both true by
  default (`src/system/xthread.cpp:40-43, 880-901`).
- `KeDelayExecutionThread` truncates to whole milliseconds and calls
  `::Sleep` (`src/system/xthread.cpp:1000-1030`;
  `src/core/threading_win.cpp:96-102`). Nothing in ReXGlue or `src/` calls
  `timeBeginPeriod`. On Windows 11 the timer resolution is per process
  ([timeBeginPeriod](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod)).
  On the same PC, SVR2011 measured guest `Sleep(1)` at 1.08 ms on average, and
  a `timeBeginPeriod` request changed nothing (`performance.md:531`). Low
  likelihood.
- The XMP sleeps (10 ms and 1 ms) are already replaced by yields
  (`patches/rexglue-xmp-no-delay.patch`). The vblank clock and the presenter
  use high-resolution waitable timers (`patches/rexglue-frame-pacing.patch`).
- The D3D GPU-wait loop `0x82DB6710` spins with only PPC priority hints
  (`mr r1,r1`, `mr r31,r31`, `mr r2,r2` at `0x82DB6724`–`0x82DB6754`), which
  do nothing on the host. It costs one host core while the guest waits for
  the GPU, but it does not lengthen frames on this 7900 XT machine. It may
  matter on the Steam Deck.
- XMA: the "There are no bits to copy!" errors arrive in groups of three
  contexts (8–10, 11–13) during the intro and entrances
  (`play-iq03/runtime.log` 16:53:18–16:53:26). The busy-wait at `0x82FD1E88`
  was queried 6–9 times and never saw 0 (`xma-stats periodic`). The decoder
  runs on its own host thread (`t16780`). With the output-valid and
  frame-drain patches, the stuck-loop code cannot hang
  (`docs/research/stuck-audio-loop.md`). The Canary patch at `0x833A6C98` is
  not applied and is not needed. Ruled out for frame time. Audio glitches are
  a separate question.

### G. Presentation and overlays

- The presenter shows each guest frame 5 ms after its vblank
  (`present_pacing_offset_us`, `patches/rexglue-frame-pacing.patch`). A frame
  that arrives later waits a whole vblank. The per-present CSV
  (`--present_pacing_log`) has an `arrival_after_vblank_us` column that shows
  this directly. Not yet measured on Native.
- The owner's sessions load the RTSS, OBS and two Overwolf Vulkan layers
  (`play-iq03/runtime.log` 16:52:29.240). RTSS measures at its own layer.
  PresentMon measures at the OS present and needs no injection.

## What exists to measure with today

| Tool | How to turn it on | What it shows |
|---|---|---|
| Native hitch log | always on (Native) | frames over 25 ms, with the renderer breakdown (A) |
| Native statistics | always on (Native) | every 300 frames: draws, resolves, pipelines, **readbacks** (cumulative) |
| `--wwe_native_report=<file>` | cvar | skip counts including `pipeline_pending` |
| `--perf_log_csv=<file>` (`smoke.ps1 -PerfCapture`) | cvar (`src/wweallstars_app.h` opens it) | one row per guest swap: `frame_time_us`, … (`src/core/perf/counter.cpp`). Shown to work on Classic; not checked on Native |
| `--present_pacing_log=<file>` and `--present_pacing_stats=true` | cvars (frame-pacing patch) | per host present: interval, repeated vblanks, arrival after vblank, latency |
| `--frame_event_log=true` | cvar (Classic) | presents skipped while pipelines build, blocking or deferred readbacks |
| `scripts/analyze_perf.py`, `scripts/analyze_hitches.py` | copied from SVR2011 | summary statistics per range; slow frames with nearby events |
| Tracy 0.13.1 | `.tools/tracy-0.13.1/` (matches the client) | **not usable yet**: the client is built in (`TracyClientrd.dll`, `TRACY_ON_DEMAND`, `TRACY_MANUAL_LIFETIME`), but nothing calls `rex::perf::Profiler::Startup()` |
| Windows Performance Recorder / PresentMon | external | CPU sampling with stacks through the RelWithDebInfo PDB (`sub_XXXXXXXX` names); present times seen by the OS |

## Measurement plan (owner, by hand)

One session per row. Same route each time: Exhibition, 1v1, Hogan vs Cena,
arena `Arena_001` (SmackDown), **entrances on, not skipped**, then about 30 s
of the match, then quit. Note the wall-clock time when the arena video starts
and when the first entrance begins (or press a mark key once one exists).
Launch through `scripts/smoke.ps1 -Seconds 900 -PerfCapture` with the
`-ExtraArguments` shown, plus `--wwe_native_renderer=true` for Native runs.
Before each run, disable Overwolf (`DISABLE_VULKAN_OW_OVERLAY_LAYER=1`,
`DISABLE_VULKAN_OW_OBS_CAPTURE=1`) and run PresentMon instead of RTSS (or
alongside it, with RTSS only as an overlay).

| Run | Settings | Confirms | Refutes |
|---|---|---|---|
| 1. Baseline | Native, enhancements at their defaults, warm caches; `--present_pacing_log=…csv` | Hitch lines in the intro and entrances with renderer ≈ frame time, and readbacks rising in the 300-frame statistics only there (reproduces A) | A new picture: then stop and profile (run 5) |
| 2. Supersampling off | run 1 plus `--wwe_native_supersample=false` | Renderer-bound spikes shrink to about 1/4 (about 25 ms instead of about 100 ms), with the same readback count | Spike size independent of scale: the cost is not per pixel |
| 3. Readback off | needs instrumentation item 1 (`--wwe_native_resolve_readback=false`) | Entrances at 60 fps apart from the guest-bound frames, renderer under 2 ms; correct image (ticket 06: no CPU readers) | Spikes stay: something else in the renderer; look at the new readback and timing fields |
| 4. Classic comparison | Classic, warm caches, `--frame_event_log=true`, `--present_pacing_log=…` | The intro's 33.3 ms cadence also on Classic, so it is guest-bound and not a Native problem | The cadence missing on Classic: a Native cost that is not logged |
| 5. Guest profile of the arena video | Native, readback off (once item 1 exists), WPR CPU profile (`wpr -start CPU -filemode`, stop after the video) or Tracy (item 2) | Hot `sub_` functions in the Bink section (`0x83500E00`–`0x83512AD4`) and Bink callers, or zlib/package loading, on the game or render thread | Hot spots in host code (kernel waits, `MaybeYield`, the `0x82DB6710` spin) instead: a synchronisation problem, not CPU work |
| 6. Console or emulator reference | Xenia Canary (`xenia-baseline.md`) with PresentMon over the same route | Canary also at 30 fps in the arena video: the game's own behaviour or the same CPU cost; accept or optimise | Canary at 60 fps: recomp-specific guest cost (codegen, kernel HLE) |
| 7. Cold cache | Native and Classic with `cache/native-pipelines` and the Classic shader storage moved away | Native: `pipeline_pending` in the report, no stall. Classic: 350–930 ms frames tied to `frame_event_log` "present skipped" lines | Classic spikes without pipeline events: texture or first-use asset loading |

A good result for the playable build: after run 3 (or its fix), entrances hold
60 fps on Native apart from the game's own load frames, and the arena video
is either 60 fps or shown to match Canary.

## Instrumentation to add, by value

1. **A Native readback switch plus readback fields in the hitch line.** Add
   `--wwe_native_resolve_readback` (default to be decided; ticket 06 supports
   off for this map), and per frame: number of readbacks queued and
   delivered, bytes, and delivery time (allocation, map, swap, downsample,
   `WriteToGuest`). This confirms or rules out H1 directly and is also the fix
   path. Longer term, read a target back only when the CPU touches it
   (`src/resolve_watch.cpp` already faults on guest CPU access), or tolerate
   1-frame gaps in the run test, and reuse readback buffers instead of one
   dedicated allocation per copy.
2. **Tracy startup**, ported from SVR2011 (`src/svr2011_app.h:21-23`, cvar
   `svr_profile`): a `wwe_profile` cvar that calls
   `rex::perf::Profiler::Startup()` in `OnPostSetup`. Add zones for the
   Native hooks, `DeliverReadbacks`, `pipelines::Get`, `textures::Bind` and
   the present. Capture with SVR2011's `capture-profile.ps1` pattern and the
   local `.tools/tracy-0.13.1`.
3. **Guest busy time per frame**: in the hitch line, time from the end of
   the last present hook to the first D3D call of the next frame, and time
   spent in `0x82DB6710`. This separates "the game took 33 ms" from "the game
   waited for the GPU or a vblank" without a profiler.
4. **Movie events in the log**: log which `.bik` opens (`NtCreateFile` on
   `Movies\*.bik` at info level) and the Bink frame rate. The JumboTron
   videos currently leave no trace, so the hitch windows cannot be lined up
   with the entrance videos.
5. **A mark key** (the SVR2011 `mark.ps1` pattern) and a hitch-session
   wrapper, so `analyze_hitches.py` reports only the entrance windows the
   owner marks.
6. **Classic perf counters**: set `pipeline_cache_misses` and
   `texture_cache_misses` in the Vulkan backend (only D3D12 sets them), so
   Classic `perf.csv` can tell pipeline spikes from texture spikes.

## Open

- What renders at 30 Hz into the 320x256 / 560x512 targets during the intro
  and entrances (titantron composite, crowd, reflections)? One
  `native_trace_request` trace at an entrance frame would show it.
- Whether the arena video's 30 Hz is the game's own behaviour or guest CPU
  cost (runs 5 and 6).
- What causes the 77–93 ms guest frames when the match loads, and whether
  they are acceptable as a load boundary.
- The [game-compatibility report #1536](https://github.com/xenia-project/game-compatibility/issues/1536)
  lists only a 2020 startup crash and apu labels; no upstream source covers
  entrance performance for this title.

## Owner answers (2026-10-03)

- The entrance stutter is the same with `--wwe_native_supersample` on or off.

What this changes:

- The quick test for H1 (spikes shrink to about a quarter without
  supersampling) did not show the predicted drop. H1 is no longer high
  confidence. The readback still runs at 1x, so it is not ruled out, but its
  cost does not scale with pixels the way the per-copy estimate predicted.
- The deciding test is now instrumentation item 1: a cvar that turns the
  readback off, plus readback counts and timings in the hitch line. If the
  spikes stay with the readback off, the cost is elsewhere (H2, the game's own
  work during the intro and entrances), and item 3 (the guest's own work per
  frame) becomes the next step.

## The 30 Hz intro is the game's own half-rate (2026-10-04, zero-stutter ticket 10)

**Answer: confirmed, high confidence.** The arena intro and the entrances run at
30 Hz because the game asks for it, not because the guest's work is too much for
the recomp. Item 2 of the Answer above ("guest work of more than 16.7 ms per
frame") is wrong. Canary's 60 presents/s do not show that the console runs the
intro at 60 Hz.

### How the game picks 1/30 s (static, base XEX v5)

- `FEngineLoop::Tick` (`0x82417890`, `0x824179DC–0x82417A3C`) steps 1/30 s when
  `0x837094F4` is set and `0x837094F0` and `0x83709510` are clear. Otherwise it
  steps 1/60 s. Its limiter then waits out the step
  (`movement-judder.md` §1).
- The only store of 1 to `0x837094F4` is in `sub_82861648`. It runs when bit
  `0x00040000` of the object's flag word at `+248` is set
  (`rlwinm r10,r11,0,13,13`). The rest of `sub_82861648` is UE3's
  `USeqAct_Interp::Play`:
  - With `bForceStartPos` (bit 9) set and `bIsPlaying` (bit 0) clear, it jumps
    to `ForceStartPosition` (`+244`).
  - Otherwise, with `bRewindOnPlay` (bit 4) set and either `bIsPlaying` clear or
    `bRewindIfAlreadyPlaying` (bit 6) set, it rewinds. It first calls
    `ResetMovementInitialTransforms` (`sub_82860490`) when `bNoResetOnRewind`
    (bit 5) is set.
  - Both jumps go through `UpdateInterp` (vtable `+320`, `bJump` = 1).
  - At the end it sets `bIsPlaying`, then clears `bReversePlayback` and
    `bIsPaused`.
  - Bit 13 is a THQ addition, so a **Matinee flagged in the level data drops
    the engine to 1/30 s while it plays**.
- `0x837094F4` is cleared under the same bit in `sub_824D6110` and
  `sub_824D6A38`, the Matinee's stop paths.
- The two flags that must be clear come from the command line at init
  (`sub_824142D0`): `-BENCHMARK` sets `0x837094F0` and `-DUMPMOVIE` sets
  `0x83709510`, through the UTF-16 strings at `0x82001C30` and `0x82001C44`. The
  `STARTMOVIEDUMPFRAMES`/`ENDMOVIE` console commands (`sub_8286FCF0`) set and
  clear `0x83709510`. Neither is set in play. `0x837094F0` is also set from
  object flags in `sub_827AEAE8` and `sub_824D6110`. That path is game data too.
- Nothing in the choice depends on host timing, so any faithful emulator runs
  the same 1/30 s steps. Canary presented 85 times a second in the menus, where
  the game ticks at 60 (smooth-presentation ticket 08). Its present count is
  therefore not a guest frame count, and its 60 presents/s in the intro are
  most likely each 30 Hz frame shown twice. (Inference: Canary's guest swap
  count was never measured.)

### Measured (tick log, `--wwe_tick_log`)

The smoothness report now prints a `game:` line per window (ticks, ticks at
1/30 s, mean work per tick; `scripts/analyze_hitches.py`, `game_step_summary`).
On `analysis/capture-game-20261003-194453` (Native, 60 Hz, marks on the route):

| Window | Ticks | At 1/30 s | Work mean per tick |
|---|---|---|---|
| intro | 625 | 602 (96 %) | 12.9 ms |
| entrance 1 | 486 | 441 (91 %) | 11.9 ms |
| entrance 2 | 1156 | 491 (43 %) | 8.3 ms |
| match | 3546 | 0 | 3.3 ms |

During the half-rate stretches each 33.3 ms tick is about 13 ms of game work
and about 20 ms in the game's limiter (hitch notes:
`game work 13.0 ms, limiter 20.3 ms`). The game is idle for most of each step.
"Work" includes the game's waits on the GPU, so the CPU cost is at most that
figure.

### What follows

- No recomp cost needs fixing for the intro to match the console. The build
  flags, a host Bink decoder or streaming would not change the step, so a
  Tracy profile of the intro cannot find the cause. Tracy is still the right
  tool for the per-tick budget of the enhancement below.
- Reaching 60 Hz is an **enhancement** that goes beyond the Xbox 360: ignore the
  half-rate flag, as SVR2011's port does for its entrances
  (`svr2011-pc-port.md`, "Frame pacing and 60 fps"). The step is fixed per
  tick, so a Matinee still plays at the right speed: it moves half as far per
  tick, twice as often. Bink keeps its own 30 fps. The risk is the lock-step
  simulation: a tick whose work exceeds 16.7 ms slows the game instead of
  skipping, and about 13 ms of work per tick leaves about 3.7 ms of margin. The
  work at 1/60 s may be lower, since part of it is paid per frame rather than
  per second of game time. This is ticket
  `.scratch/zero-stutter/issues/15-sixty-hz-cinematics.md`.
- The report's intro bar (60 Hz) fails on a faithful run by design. Whether the
  bar stays at 60 (with the enhancement on) or allows the game's half-rate
  windows is the owner's call in ticket 15.
