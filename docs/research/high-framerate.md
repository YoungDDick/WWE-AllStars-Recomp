# High frame rate in matches (120 fps and up)

Date 2026-10-13. The owner asked whether matches can run at 120 fps or higher, or
unlocked, without breaking game logic (physics, animation timing, AI, input
windows, replays, online sync). Menus may stay at 60. This note is research only:
nothing was built or run, and no source was changed. **Every guest address here is
a TU2 address** (ADR-0022). `docs/research/syncsim-replay-log.md` and
`movement-judder.md` give base v5 addresses; the ones reused here were converted
with `python scripts/tu_addrmap.py` (§9). ReXGlue paths (`SDK/`) are relative to
`.tools/rexglue-source` (`f5337cdc` plus `patches/`).

Tools used:
- Ghidra 12.1.4 through the MCP server, project `ghidra/WWEAllStarsTU2`, program
  `default.xexp` (TU2), opened read-only. The decompilations below come from it.
- `python scripts/disasm.py` on `analysis/xex/tu2.pe`.
- A throwaway scanner in the session scratchpad (not kept) that finds every
  `lis` + `lfs`/`lfd`/`lwz`/`addi` pair in `.text` that loads a float or double of
  1/60, 60.0, 1/30 or 1/120. It found 63 loads of 1/60 and 63 of 60.0. The sites
  read in §3 were picked from that list.
- `python scripts/syncsim_log.py dump` on the recorded `analysis/sync-test-*`
  logs.
- `assets/Game/Config/Xenon/Cooked/Coalesced.ini`, decoded and searched for
  frame-count keys.

**Confirmed** means read in disassembly, decompilation, source, config or logs.
**Inferred** means plausible but not measured. **Unknown** means not looked at
well enough to say.

## Answer

1. **Matches step a fixed 1/60 s per tick, and a QPC limiter holds 60 ticks/s
   (confirmed).** `FEngineLoop::Tick` (`0x82417780`) picks 1/60 (`[0x82001CCC]`) or
   1/30 (`[0x82002F38]`). It passes `dt × dilation` to `GEngine->Tick`, then spins
   until `dt × dilation` of wall time has passed since the last tick. Measured
   time is used only for statistics. Simulation and rendering are one lock-step
   loop: one tick, one rendered frame. There is no accumulator and no
   interpolation (§1).
2. **Raising the tick rate is the same mechanism as 60 fps entrances, one step
   further.** The game's code is mostly written against `dt`. Most of the
   1/60-aware code found scales with `dt` or is fixed-step with its own
   interpolation (§3). The open risks are:
   - frame-counted values in UnrealScript config: `MaxRotationPerFrame=30`,
     `NumFramesToTrackMovement=5`, `ElimDelayTimeTicks=50`, the minigame's
     `NumFrames=120`, and `ReversalSpamTime=60`. How they are used is unverified;
   - fixed 30 Hz physics (Havok `HkSimRateHz=30`, PhysX `NxTimeStep=0.02`), which
     would visibly step at 120 Hz;
   - the per-tick CPU budget, which halves to 8.33 ms. Today's guest work per
     60 Hz tick is 3.3 ms median and 7.5 ms p99.
3. **Online play must stay at 1/60 (confirmed).** The Net-Z session tick runs a
   SyncSim step every `+0x10` = 2 engine ticks (the value is hardcoded). Each
   step checksums the wrestlers' float state. A peer at 1/120 would advance half
   as far per step and diverge at once (§5).
4. **Host ceilings.** ReXGlue does not cap at 60 by design, but it defaults to
   60 in three places (§4):
   - the guest vblank (`video_mode_refresh_rate` = 60, range 24–240);
   - the presenter, which shows at most one guest frame per guest vblank;
   - `ChooseVblankSource`, which assumes a 60 Hz guest.

   A 120 Hz mode needs `video_mode_refresh_rate=120` and a one-line SDK fix that
   passes the guest rate to `ChooseVblankSource`. With that, the owner's 360 Hz
   FreeSync display can show it directly.
5. **Unlocked (variable dt) is not feasible** without rewriting the game's
   timing. The game has never stepped by measured time. **Fixed multiples of 60
   are feasible:** 120 is the target, and 180 and 240 would follow the same path
   if the CPU budget allows.
6. **Recommendation:** prototype approach (a), an offline-only 120 Hz match tick,
   on a new branch. The first experiment needs no codegen (§7 E0): scale the
   engine tick's dilation by 0.5 in the existing `sub_82417780` hook, set
   `--video_mode_refresh_rate=120`, and measure with the tick log and the paced
   presentation log. As a free baseline, the owner can also turn on AMD Fluid
   Motion Frames 2 (driver frame generation, supports Vulkan) on the RX 7900 XT.
   It touches nothing in the game (§6 c).

## 1. How a match frame is driven

### 1.1 The tick (confirmed, TU2, Ghidra decompilation of `0x82417780`)

```c
dt_meas = QPC*GSecondsPerCycle - last;             // statistics only (0x8370BD48/50)
if (flag_83709514 == 0 || BENCHMARK_83709510 || DUMPMOVIE_83709530) {
  dt = [0x82001CCC];                               // 1/60
  if (countdown_835E0088) countdown_835E0088--;
} else { dt = [0x82002F38]; countdown_835E0088 = 5; }   // 1/30
this->FrameStart = GCurrentTime_8370BD40;          // this+0xC
if (this->RampCount_38 > 0) { this->Dilation_2C += this->RampStep_34; ... }   // dilation ramp
this->DeltaSeconds_28 = dt;
step = this->Dilation_2C * dt;
GEngine_8371D1C0->vtbl[0xCC](step);                // GEngine->Tick
...  GEngine->vtbl[0xE0]() (GetMaxTickRate) -> optional appSleep, skipped under -BENCHMARK
if (online session object 0x82410498() && [0x83740590]) 0x82A62908(session);   // Net-Z tick
do { GCurrentTime = QPC*GSecondsPerCycle; } while (GCurrentTime - FrameStart < step && appSleep(0));
```

- The step choice is at `0x824178CC–0x8241792C`, and both paths join at
  `0x82417930` (`b 0x82417930` at `0x8241790C`; fall-through from
  `0x8241792C`). Disassembly: `python scripts/disasm.py 82417908 20`.
- `0x82BDF098`, which the tick calls three times with `step`, is an empty
  function (`blr`).
- The limiter compares against `step`, so it follows any change to `dt` or to
  dilation. A 1/120 step therefore lets the game run 120 ticks/s on its own.
- Lock-step: a tick that takes longer than `step` makes game time run slower
  than wall time. Nothing catches up (`movement-judder.md` §1). At 120 Hz the
  budget per tick is 8.33 ms.

### 1.2 Measured rates (confirmed, earlier notes)

- Matches: 60.0 presents/s and 0 ticks at 1/30 s (`movement-judder.md` §1;
  `entrance-video-stutter.md`, tick-log table; `60fps-entrances.md` Answer 1).
- Guest work per 60 Hz tick on the owner's PC: median 3.3 ms, p99 7.5 ms
  (`performance-and-low-end-hardware.md`, Answer). The p99 is already 90 % of a
  120 Hz budget.

### 1.3 Other frame-rate switches in the image (confirmed)

- `LOCKSTEP` / `LOCKED` / `UNLOCKED` (strings `0x82158620`, `0x82158610`,
  `0x821585FC`) are console commands. They set the D3D `PresentationInterval`
  through `0x82768088`, and normal play never runs them (`60fps-entrances.md`
  §1.3).
- `-LOCKSTEPOFF` (L"LOCKSTEPOFF" `0x82001B98`, parsed at `0x82414530`) clears
  `0x83530754`, which defaults to 1 (`0x82414524`). Its only readers are the
  console handler (`0x82871A28`) and a stats draw (`0x82907BCC`, near
  L"LockStep" `0x82171290`). It is not a gameplay switch.
- `FPS_LOCKED=False` (`[D3DDrv.D3DRenderDevice]`) and `MinDesiredFrameRate=35`
  (`[Engine.Client]`, `[XeDrv.XenonClient]`) are UE3 stock settings. No reader of
  them was traced.

## 2. Is simulation coupled to rendering?

**Yes, one tick per rendered frame (confirmed, §1.1).** UE3's game thread ticks
the world and enqueues the frame. The render thread then draws it, and the game
thread waits on a fence for it. The hook on `sub_8271CB70` is commented "r4 = how
many frames the render thread may lag" (`src/game_tick_log.cpp:173-179`). No
render-only path draws between ticks.

Interpolation exists only locally. **`0x825F2160` is a 30 Hz fixed-step
integrator with interpolation (confirmed code, identity unknown).** It steps a
value in increments of `[0x82098798]` (1/30 s) and lerps between the last two
steps by the remainder times `[0x8209879C]` (30). Its only caller, `0x825DFD30`,
writes the result to `WorldInfo+0x73C` and sets bit `0x40000000` of
`WorldInfo+0x734`. So THQ already wrote at least one subsystem to be
rate-independent this way. Nothing like it exists for bone or camera transforms.

## 3. What a 1/120 s tick would change, area by area

| Area | Evidence | At 1/120 s | Confidence |
|---|---|---|---|
| Engine step and limiter | §1.1 | 120 ticks/s, half the game time per tick | Confirmed |
| Crowd scheduler `0x82B55A38` | Under 1/30 s it ticks one of two groups per tick with `2·dt` (`60fps-entrances.md` §8.2) | Crowd animates at 60 Hz instead of 30 Hz; the cost per tick stays the same | Confirmed code |
| Skeletal pose `0x82798160` (UpdateSkelPose) | `0x82798ED8`: `lfs f27,[0x8206B200]` (60.0), then `x -= dt·60` on `+0x248`/`+0x24C` | Timers count "60 Hz frames" scaled by `dt`, so they are correct at any rate | Confirmed code |
| THQ animation player `0x82540E30` (caller `0x82B11E50`) | `0x825410BC–0x825410F4`: when bit `0x40000000` of `+0x12C` is set (one-shot, cleared after), it snaps the position to the 1/60 s grid: `round((pos+delta)·60+0.5)·(1/60)`. `0x82541228`: an end-of-anim look-ahead of `(delta/dt)·[0x8206CEF8]`, where that constant is 1/120, i.e. half a 60 Hz frame | The snap still lands on 1/60 multiples (harmless, but quantised). The look-ahead becomes a full tick, so end-of-anim may trigger one 8.3 ms tick earlier | Confirmed code; effect inferred |
| UWorld::Tick `0x828114C0` | `0x8281161C–0x82811628`: under a WorldInfo debug flag (`+0x2A0`, bits 9/10) the world step is forced to 1/60 | Debug single-step only | Confirmed code |
| Matinee, interp, particles | Take `dt` (UE3 design; `60fps-entrances.md` §2) | Same speed | High |
| Havok | `HkSimRateHz=30` in `[Engine.WorldInfo]` (`Xe-Game.ini` in `Coalesced.ini`) | If it steps fixed at 30 Hz without interpolation, Havok bodies move in 30 Hz steps under a 120 Hz camera (this already happens at 60) | Config confirmed; reader not found |
| PhysX | `NxTimeStep=0.02`, `NxMaxDeltaTime=0.333333` (`Coalesced.ini`, `movement-judder.md` §1) | 50 Hz substeps against a 120 Hz tick: 0, 1 or 2 substeps per tick | Config confirmed; engine use inferred |
| Values in "frames" or "ticks" (UnrealScript config) | `[SD_Actors.SD_Pawn] MaxRotationPerFrame=30`; `[Game.GamePawn] MaxRotationPerFrameRun=25`, `PuppetMaxRotationPerFrame=25`, `ReversalSpamTime=60`; `[Game.GamePawnMovementTracker] NumFramesToTrackMovement=5`; `[GameHUD] ElimDelayTimeTicks=50`, `ElimScaleTimeTicks=10`, `ElimFadeTimeTicks=7` (commented "# ticks"); `[GameSubmissionMiniGame] NumFrames=120`, `NumIntroFrames=60` | **If** these are counted per tick, turning speed doubles, the movement tracker's window halves (5 ticks = 42 ms), HUD animations play 2x fast, and the submission minigame meter runs 2x fast. The game's own tooltip says "MaxRotationPerFrame: The Max number of rotations per frame." (`0x82084584`) | Config confirmed; how the script uses them is **unknown**. **Main gameplay risk** |
| Reversal and input windows | Reversals go through the anim FSM: `UGameAnimFSMReversalButtonTrigger` (`0x821D3CA0`), `UGameAITransitionLogic_IsReversalWindowOpen` (`0x821C3980`). Input is polled about 2,800 times/s (`xenia-baseline.md`) and read per tick | Windows tied to animation time keep their length in ms; a window counted in ticks would halve. Input latency drops by about 8 ms | Inferred |
| AI | UE3 AI uses `dt`, timers and latent actions. THQ's AI transition logic was not read | Unknown | Unknown |
| Replays (SD_ReplaySystem) | `execTrackReplayData` (native `0x82A928B8`, table `0x8352E238`) reads one float argument (probably DeltaTime) and calls vtable `+0xD4` | Sampling may follow time; the buffer may fill twice as fast if it stores one sample per tick | Inferred |
| Music, crowd and commentary audio (FMOD, real time) | `60fps-entrances.md` §2 | In sync as long as every tick fits 8.33 ms | Confirmed mechanism |
| Scene runs | `wwe_lock_guest_time` assumes 1/60 per present (`src/frame_clock.cpp:66`) | Must force the mode off, as every enhancement does (`scripts/scene_runner.py` `ENHANCEMENTS`) | Confirmed |
| SyncSim, online | §5 | Desync | Confirmed |

Offline determinism is not a requirement: offline matches are never compared
across runs except in scene runs, which turn enhancements off.

## 4. Host side: what caps presentation at 60

- **Guest vblank.** The SDK's vsync thread runs at `VdQueryVideoMode`'s refresh
  rate, which comes from `video_mode_refresh_rate`: default 60.0, range 24–240,
  restart required (`SDK/src/ui/window.cpp:63-66`,
  `SDK/src/kernel/xboxkrnl/xboxkrnl_video.cpp:76-78`,
  `SDK/src/graphics/graphics_system.cpp:169-175`).
- **Presenter.** With `present_pace_to_guest_vblank` on (default,
  `SDK/src/ui/presenter.cpp:94`), the pacing thread presents at most one guest
  frame per guest vblank, at vblank + `present_pacing_offset_us` (5000 µs,
  `:106-110`). A second frame inside one vblank *replaces* the first (counted as
  "frames replaced" in the `Paced presentation 5s` line, `:930`). So a game
  ticking at 120 behind a 60 Hz guest vblank shows only 60 frames/s.
- **Follow the display (ADR-0016).** `ChooseVblankSource` has `guest_hz = 60.0`
  as a default argument (`SDK/include/rex/graphics/vblank_source.h:32-42`), and
  the caller does not pass the configured rate (`graphics_system.cpp:404`).
  At a 120 Hz guest on a 360 Hz panel, it would lock the guest vblank to every
  6th refresh, i.e. 60 Hz. That is a one-line SDK patch: pass the configured
  rate. With VRR active, the decision already keeps the host clock
  (`:34-35`), so the owner's FreeSync setup does not hit this.
- **Offset.** 5 ms after a vblank leaves 3.3 ms before the next vblank at
  120 Hz, and `ChoosePresentOffsetUs` already guards the margin on
  fixed-refresh displays (`presenter.cpp:111-116`). Inferred: fine. If frames
  arrive late, lower the offset for 120 Hz.
- **Guest D3D throttle.** The present (`0x82DBDFF8`, the only `VdSwap` caller)
  waits only while more than 14 frames are in flight (`0xe < [+0x4188] -
  [+0x4190]`, Ghidra). No vblank wait was found on the CPU side. Whether the
  swap's PM4 stream waits on the vblank counter (`counter_`,
  `SDK/src/graphics/command_processor.cpp:1353`) is **unknown**. The E0
  experiment answers it: a 120 Hz tick with a 60 Hz guest vblank would then hold
  at 60 ticks/s.
- **The guest vblank interrupt.** The XDK registers it with
  `VdSetGraphicsInterruptCallback` (`xenia-baseline.md`). If any game code counts
  vblanks rather than ticks, a 120 Hz guest vblank speeds it up. **Unknown.**
  Menus would also see a 120 Hz vblank while ticking at 60; the presenter then
  holds each frame for two vblanks.
- **Displays.**
  - 120/240/360 Hz fixed refresh: 120 divides evenly.
  - 144/165 Hz: only with VRR (ADR-0016, consequences).
  - The owner's panel is 360 Hz FreeSync (`movement-judder.md`, owner answers).
  - Vulkan `FIFO` shows one image per vblank, so presentation at 120 needs a
    display at 120 Hz or more, or VRR
    ([VkPresentModeKHR](https://registry.khronos.org/vulkan/specs/latest/man/html/VkPresentModeKHR.html)).
- **Unlocked.** Showing frames as fast as the GPU allows would mean a variable
  `dt`. Approach (a) cannot give that: the game has only ever stepped a fixed
  `dt` (§1.1). A fixed rate also keeps the existing pacing design
  (`smooth-presentation`, `.scratch/smooth-presentation/spec.md:111`).

## 5. Online play (confirmed)

The engine tick calls the Net-Z session tick `0x82A62908` (its only caller is
`0x82417780`; see §1.1). In match state 3, Ghidra shows:

```c
if (sim->step == 0x1e) { this->ticksPerStep_10 = 2; ... if (0x82B70C08()) this->ticksPerStep_10 = 4; }
...
if (this->flags_38 & 0x4000000) { 0x82A5F870(this); 0x82A61D80(this); }   // sync step
this->tick_14 = (this->tick_14 + 1) % this->ticksPerStep_10;
this->flags_38 = (tick_14 == 0) << 26 | ...;                               // step on every Nth tick
```

- `0x82A61D80` hashes each wrestler's float state into the station checksum: per
  pawn, `+0xE8..0xF0`, `+0x468..0x470`, `+0x410`, `+0xF4..0xFC`, and per-bone
  floats `+0x50`/`+0x5C`. Then, in a loop, it calls the SyncSim update
  `0x830D9F00` (base v5 `0x830D5E40`) and sleeps until the step is allowed.
- `0x830D9F00` advances `sim+0x78` (the step) and schedules the next one
  `1000 / sim+0x98` ms later. It returns 3 on a checksum mismatch, which raises
  "SyncSim Diverge" (`syncsim-replay-log.md`).
- **So one SyncSim step is exactly 2 engine ticks (4 in some mode, probably
  4-player), with a hardcoded count.** At 1/60 that is 33.3 ms of game time per
  step. At 1/120 it would be 16.7 ms, so the two peers would compute different
  states for the same step, and the checksums would differ on the first step.
  Online therefore has to run the base rate on every peer.
- The recorded logs agree: every `analysis/sync-test-*/Station*.log` header has
  `last_ssf = 29`, and `last_sim_clock_ms / last_step` = 34.07 ms (match1: 243004
  / 7133). That is 1000/29 rounded down to 34 ms per step.
  - Inferred: a 1v1 online match therefore runs at about 2 × 1000/34 = 58.8
    ticks/s, about 2 % slower than offline.
  - Inferred: online can never benefit from a higher tick rate. The step cadence
    belongs to Net-Z, not to the display.
- **Rule for the feature:** off whenever online play is enabled (`--wwe_online`,
  ADR-0021). The hook also checks at run time that no Net-Z session is active
  (`0x82410498()` non-zero and `[0x83740590]`, the tick's own test). A higher
  rate in the online *menus* would be harmless, but there is no reason to allow
  it.

## 6. Approaches, ranked

### (a) Raise the guest tick to 1/120 s in matches (recommended first)

- **What:** the same seam as `Wwe60fpsEntrances`. A midasm hook at the join
  `0x82417930`, with `registers = ["f31"]`, sets `f31 = 1/120` when the
  conditions hold. FPR registers are supported in midasm hooks
  (`SDK/src/codegen/builders/context.cpp:383-387`). Then `DeltaSeconds`,
  `GEngine->Tick` and the limiter all follow. Conditions:
  - the setting is on;
  - not online;
  - not a 1/30 or 60-fps-entrance cinematic (or allow those too, later);
  - `-BENCHMARK` and `-DUMPMOVIE` are clear;
  - "in a match".

  The countdown at `0x835E0088` is untouched, because the hook runs after it.
  Host side: `video_mode_refresh_rate=120` and the `ChooseVblankSource` patch
  (§4).
- **Menus at 60:** the "in a match" predicate keeps them at 1/60. The tick is
  chosen per tick, so the switch is seamless, as with entrances.

  RE needed: a cheap and reliable match-active signal. Candidates:
  - the crowd scheduler `0x82B55A38` ran in the previous tick (arena loaded;
    this includes entrances);
  - THQ's match manager state;
  - the online session object's match state, as a model.

  For the first experiment, matches and menus alike can run at 120.
- **What to scale or fix (§3):**
  - the frame-counted config values. Either double them in a 120 Hz session
    (they are config properties, so a hook on their load or a write into the
    class defaults could work), or prove that the script scales them by `dt`;
  - optionally, interpolate Havok and PhysX bodies;
  - the end-of-anim look-ahead (`0x82541228`) only if testing shows an effect.
- **RE still needed:**
  - where `MaxRotationPerFrame`, `NumFramesToTrackMovement`, `ReversalSpamTime`
    and the HUD and minigame tick counts are consumed (UnrealScript bytecode in
    the cooked packages, or native `exec` functions);
  - the `HkSimRateHz` reader;
  - the replay buffer size;
  - whether anything counts vblanks.
- **Effort:** E0 is about 10 lines and no codegen. A production hook with the
  match predicate and the online guard is about 1–2 days. Scaling the frame
  counts is open-ended until the RE above is done.
- **Risk to game logic:** medium.
  - Everything that uses `dt` is safe.
  - Anything counted in ticks plays faster. Which is the case can only be seen
    in a run.
  - CPU budget: p99 7.5 ms against 8.33 ms means some ticks will run long.
    Each one slows game time (and music sync) slightly. 4-player matches and
    the low-end preset will miss more.

### (b) Keep the 60 Hz sim and interpolate on the render side

- **What:** draw an extra frame between ticks N and N+1, with bone matrices,
  object transforms and the camera interpolated.
- **Where hooks would go:** the guest cannot render without ticking (§2), so the
  interpolated frame has to be made on the host. The Native renderer
  intercepts D3D (`src/d3d_census.cpp`, `src/native/native_renderer.cpp`) and
  already publishes one frame per guest present. It would have to keep two
  frames' draw lists, match draws across frames, find which vertex-shader
  constants are transforms (bone palettes, world and view-projection), lerp or
  slerp them, and re-issue the draws. Particles, UI, render targets, resolves
  and the Scaleform HUD would need their own rules.
- **Effort:** very high (weeks), with a long tail of artifacts.
- **Risk to game logic:** none, because the game is unchanged and online works
  too.
- **Menus:** unaffected. They are presented as they are now.
- **Verdict:** only if (a) fails on gameplay grounds.

### (c) Host frame generation, no game change

- **AMD Fluid Motion Frames 2 (driver).** AMD's Adrenalin 24.9.1 notes list
  AFMF 2 for "OpenGL, Vulkan, DirectX 11, and DirectX 12 titles", on RX 7000
  and newer
  ([AMD 24.9.1 release notes, community copy](https://community.amd.com/t5/pc-drivers-software/amd-software-adrenalin-edition-24-9-1-release-notes/m-p/714647/highlight/true);
  [PCWorld](https://www.pcworld.com/article/2411947/amd-releases-ai-powered-fluid-motion-frames-2.html)).
  The owner's RX 7900 XT qualifies.
  - Effort: zero code. Risk to logic: none. Online-safe.
  - Costs: added latency, artifacts on fast motion and on the HUD, and frames
    the game never computed.
  - It interacts with this repo's pacing: `movement-judder.md` H4 already lists
    AFMF as something that retimes frames. So it is a player-side option, not a
    port feature.
- **FSR 3 frame generation.** The SDK is already in `.tools/fidelityfx-sdk`:
  FidelityFX SDK 1.1.4, `ffx-api/include/ffx_api/ffx_framegeneration.h`, and a
  Vulkan frame-generation swapchain in `ffx_api/vk/ffx_api_vk.h:50-81`. The
  presenter already links ffx-api for FSR upscaling
  (`SDK/src/ui/vulkan/vulkan_presenter.cpp:31-33`).
  - Its prepare dispatch needs `depth` and `motionVectors`
    (`ffx_framegeneration.h:121-138`), plus camera near, far and FOV
    ([GPUOpen frame interpolation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation/)).
  - The Native renderer produces no motion vectors (no velocity or motion
    pass in `src/native/`). Camera-only motion vectors could be rebuilt from
    depth and two view-projection matrices. That is wrong for the wrestlers,
    the part that moves most in this game. Skinned motion vectors would need
    approach (b)'s draw matching.
  - The HUD needs a HUD-less color input (`ffx_framegeneration.h:113`).
  - Effort: high (about 2–4 weeks). No risk to game logic.
- **Menus:** frame generation can be gated to matches, or left on everywhere.
  It does not change the game's 60.

### (d) Other options

- **VRR at 60** is already the smooth baseline (ADR-0016; owner's 360 Hz
  FreeSync). It gives no more motion samples, but no judder.
- **180 or 240 Hz ticks.** The same path as (a), with 1/180 or 1/240. The CPU
  budget falls to 5.6 ms or 4.2 ms, under today's p99 (7.5 ms), so this is not
  realistic without large CPU wins. The crowd round robin would then run at 90
  or 120 Hz.
- **Speeding up only the camera.** Not separable: the camera ticks inside
  `GEngine->Tick` with everything else.

## 7. First experiments (rapid, owner-driven)

**E0: does the engine run correctly at 120 Hz? (about 10 lines, no codegen)**

1. In `src/game_tick_log.cpp`'s `REX_HOOK_RAW(sub_82417780)` (`:159`), behind a
   temporary cvar `wwe_tick_120`:
   - before `__imp__sub_82417780`, read the dilation float at `self+0x2C`, store
     half of it, and remember the old value;
   - after the call, restore it, but only if the ramp counter at `self+0x38` is
     0. If the ramp moved the value, leave the ramp's result.

   The tick then passes `dilation × 1/60 / 2` to `GEngine->Tick` and to the
   limiter (§1.1). `DeltaSeconds` at `+0x28` stays 1/60; it only feeds
   statistics. Skip the change while `[0x83709514]` is set, and when online is
   on.
2. Launch with `--wwe_tick_120=true --video_mode_refresh_rate=120
   --wwe_tick_log=true --present_pacing_stats=true`. Play an offline 1v1 with
   the owner at the controls.
3. Read:
   - `game tick 1s:` about 120 ticks/s; `work mean` / `max` against 8.33 ms;
   - `Paced presentation 5s:` about 600 frames per 5 s, with 0 replaced and 0
     repeated vblanks. If ticks are 120 and presents 60, a vblank cap is in the
     way; if ticks themselves stay at 60, something in the swap waits on the
     vblank counter (§4);
   - the in-game match clock and a stopwatch over 60 s: game-time drift shows
     slow ticks;
   - by eye and feel:
     - walk and run speed (time to cross the ring);
     - turning speed (`MaxRotationPerFrame`);
     - reversal timing;
     - the submission/shake-stick minigame meter speed (`NumFrames=120`);
     - the elimination HUD graphic (`Elim*TimeTicks`);
     - ragdoll, rope and prop motion (Havok/PhysX stepping);
     - whether the crowd looks smoother.
4. A/B: flip `wwe_tick_120` live (F4 or the console) in the middle of the match.

Outcomes:
- Everything at normal speed and ticks within budget: go to E1.
- Something runs 2x fast: that value is counted per tick. Find its consumer
  (§6 a, RE list) and scale it.
- 120 ticks but 60 presents: fix the host side (§4) before judging smoothness.

**E1: production form.**
- The midasm hook at `0x82417930`, with the match predicate and the online
  guard, replaces the dilation trick, because dilation is the game's own
  slow-motion lever.
- Add the `ChooseVblankSource` guest-rate patch.
- A live cvar; it must not be a startup row, because `video_mode_refresh_rate`
  needs a restart, so both may need a launcher row (owner decision).
- Add it to `ENHANCEMENTS` / `kEnhancements`, as `60fps-entrances.md` §4
  describes.

**E2 (parallel, zero code):** the owner turns on AFMF 2 for `wweallstars.exe` at
60 and compares it with E0 by eye. If AFMF looks good enough, (a)'s gameplay
risk may not be worth taking.

## 8. Open questions

1. Are the "per frame" and "ticks" config values counted per tick or scaled by
   `dt`? Only E0 by eye, or reading the UnrealScript bytecode, answers this.
2. Does any guest code wait on the vblank counter or count vblank interrupts
   (§4)?
3. Is the CPU budget at 120 Hz met on the owner's PC in 1v1, in 4-player
   matches, and with the Native renderer's supersampling? On the Steam Deck it
   almost certainly is not (ROADMAP's minimum spec is the original frame rate).
4. Which object is `0x825DFD30`'s 30 Hz interpolated value (`WorldInfo+0x73C`)?
   Possibly a slow-motion or game-speed curve.
5. Should 120 Hz also cover entrances and cinematics (`Wwe60fpsEntrances` plus
   1/120)? The crowd would then need `Wwe60fpsCrowd`'s logic generalised.
6. Does the replay system's buffer hold a fixed number of samples?
7. Is the online slowdown (58.8 ticks/s from 34 ms steps) real? A wall-clock
   timestamp in the sync-test logs would settle it. It is off-topic, but it
   came out of this study.

## 9. Base v5 to TU2 addresses used here

From `python scripts/tu_addrmap.py docs/research/syncsim-replay-log.md`
(report only):

| Base v5 | TU2 | What |
|---|---|---|
| `0x830D5E40` | `0x830D9F00` | SyncSim update |
| `0x82A5E7A8` | `0x82A60F78` | divergence handler (code changed) |
| `0x82417890` | `0x82417780` | `FEngineLoop::Tick` (`60fps-entrances.md` §7) |

## 10. Flicker at 1/120 s (ticket 02)

Date 2026-10-13. In ticket 02 run 2 (Native renderer) and run 3 (Emulated
renderer), the owner saw textures flicker on the wrestler during the top-rope
climb (Cena climbing the turnbuckle), and only at 120. Because both renderers
show it, the cause is in the guest. This section is static analysis only.
Nothing was built or run.

**Result: no site was confirmed as the cause.** No `.text` site that loads
1/60 or 60.0 toggles render, material, LOD or texture state from one tick to
the next. The site classes and the three suspects are below. The first
experiment, X, splits the search in two. It shows whether the flicker comes
from the size of `dt` or from the number of ticks per second.

### 10.1 The 1/60 and 60.0 scan, redone (confirmed)

The scratchpad scanner `constscan.py` reads `analysis/xex/tu2.pe` and finds
`lis` + `lfs/lfd/lwz/addi` pairs in `.text`. It finds:
- 63 loads of f32 1/60 (`[0x82001CCC]`; `[0x8352F77C]` and `[0x8352F784]` are
  data copies);
- 63 loads of 60.0 (`[0x8206B200]`; `[0x8352F758]`);
- 8 loads of 1/30;
- 1 load of 1/120 (`[0x8206CEF8]` at `0x82541228`);
- 1 load of f64 1/60 (`0x8243B71C`).

All 115 containing functions were decompiled in Ghidra (TU2) and read where
they use the constant. The sites fall into these classes:

| Class | Sites (function entry) | At 1/120 s |
|---|---|---|
| `x += dt·60`: a timer kept in 60 Hz frame units, scaled by `dt` | `0x82531E50`, `0x82532AA8`, `0x825347F0`, `0x82534A18` (the fields are floats; read in disassembly at `0x82531E78`, `0x82532AFC`, `0x825348A8`); `0x82798160` `+0x248/+0x24C` | Correct |
| Authored frame count × 1/60 (frames to seconds) | `0x8254B8C0`, `0x82551F60`, `0x825521F8`, `0x825532F8`, `0x8259CFF8`, `0x8279BD68`, `0x827A18B0`, `0x827A1A70`, `0x827A3238`, `0x82ADB238`, `0x82ADD260`, `0x82BAA1F0`, `0x82BFCF50`, `0x82BFD400`, `0x82BFDBA8` | Correct |
| Anim time rounded to a 60 Hz frame (`fctiwz` of time·60, not of `dt`) | `0x8279D040`, `0x827A3440`, `0x827A2568`, `0x82BB3B28`, `0x8259B9E8` (angle wrap) | Quantised, as at 60 |
| `dt` clamped to at most 1/60 | `0x8271C578` (render-thread tickables, measured time) | Correct |
| Fixed 1/60 added or subtracted per call (not `dt`) | `0x828DF7D0`, `0x8257EA70`, `0x8292A020` `+0x6D0`, `0x82B2C650`, `0x82B42430` (debug camera-target text) | **Runs 2x fast** |
| Integer counter per call | `0x82798160` `+0x3FC += +0x3F4`, clamped to `+0x3F8`; blends the root bone's scale | **Runs 2x fast** |
| Frame-unit compare against a per-tick delta | `0x82ADB130` | **Flips** (below) |
| Step choice, debug step, 1/30 paths | `0x82417780`, `0x828114C0`, `0x825AC200`, `0x825AC660`, `0x82C942F0`, `0x82B260F8`, `0x82B55A38` (crowd: `dt < [0x82217768]` holds at both 1/60 and 1/120) | Unchanged |

Findings (confirmed code, effects inferred):
- **No integer truncation of `dt·60` was found.** Every `fctiwz` near these
  constants truncates an animation time or an angle, never `dt`
  (`0x8259BA98`, `0x8279D1A8`, `0x827A0914`, `0x827A3544`, `0x82BB4240`).
- **No compare of the tick's `dt` against 1/60 was found, and no
  frame-parity test.** The two-group crowd scheduler takes the same path at
  1/60 and at 1/120.
- **`0x82ADB130` flips at 1/120.** It sets the reversal-spam flag (bit
  `0x20000000` of pawn `+0x1A04`; name "ClearReversalSpamFlag", `0x821FB71C`)
  when the current anim's position (`[+0x478]+0xC4`) moved less than
  `[+0x1C50]`/60 s since the last call. Per call, the position moves half as
  far at 1/120, so the flag is set more often. This is gameplay
  (`ReversalSpamTime`, ticket 04), not rendering.
- **The fixed per-call steps are all gameplay or skeletal.** At 120 they run
  twice as fast:
  - `0x828DF7D0` is a skel control that adds `amp·sin(ω·phase)` to a bone's
    translation (`+0x18` of a 0x28-byte bone atom) and advances the phase by
    1/60 per call. Its setup, `0x828DF898`, is reached from the anim-FSM
    statements "Animate abdominal/chest muscle" (`0x8259C800`, `0x8259C898`;
    descriptors near `0x8207B468` and `0x8207B510`). Its pulse plays at
    twice the rate. The 60 Hz samples are the same, only played faster.
  - `0x8292A020` is a look-at skel control (an atan2 of bone transforms with
    hysteresis at `+0x5C`). Its out-of-range timer `+0x6D0` gives up after
    half the time.
  - `0x8257EA70` holds three countdowns at −1/60 per call.

None of these toggles a texture or a material. At most they move bones.

### 10.2 Lead 2: the THQ animation player (confirmed code, effect inferred)

- **The 1/60 snap at `0x825410BC–0x825410F4` is one-shot.** Only one site in
  `.text` sets bit `0x40000000` of `+0x12C` (`oris` at `0x8259D344`). It
  belongs to the anim-FSM statement described as "rounds out the fractional
  part of the current frame and sets the playrate to 1" (descriptor near
  `0x8207B8B4`). The snap clears the bit after use. So the snap cannot
  alternate from tick to tick on its own.
- **The notify/time-warp walker `0x82544430` (called from `0x82551E08`
  before the snap) works in 60 Hz frames with a half-frame look-ahead.**
  `dVar10 = rate·0.5` at `0x8254449C` (`fmuls f27,f31,[0x82004938]`). An
  event at frame F fires once `time·60 + 0.5·rate ≥ F`:
  - types 1 and 3 jump the time to exactly F/60;
  - type 2 warps the time;
  - type 5 holds for `[ev+0xC]` frames.

  At 1/120 a tick can land on a half frame. The event then fires one tick
  early and pulls the time forward by up to 1/120. On a chain of one-shots,
  such as a climb, this can repeat or skip a pose for one tick. That would
  look like a pose pop, not a change of texture.
- The end-of-anim look-ahead at `0x82541228` uses `delta/dt · 1/120`. It
  becomes a full tick (§3), so it can end a one-shot one tick early. The
  effect is the same: a pose, not a material.

### 10.3 Lead 3: per-tick toggles (searched, none found)

The string table was searched for these and nothing was found that switches
per tick in a match:
- LOD (`SetSwitchedLOD` and `UCinemaTrackForcedLodOverride` only apply to
  breakables and cinematics);
- fades (`AGamePawn.execStartFade` `0x82A91BE0` → `0x82ADACF8`, ticked by
  `0x82ADAD08`, uses `dt`; `execUpdateCharacterFade` `0x8244DFE8` calls the
  camera mode's vtable `+0x138`, which was not traced);
- trails (`GameTrails.MIC_List`, manager `[0x83734860]` set up at
  `0x8275ED20`, not traced further);
- muscle (above);
- cloth (`USD_SkelControlCloth`, below);
- motion blur (`UMotionBlurEffect` is present; whether matches use it is
  unknown).

`USD_SkelControlCloth` (vtable entries at `0x8203D08C–0x8203D094`):
- the entry at `0x8203D08C` → `0x82558260` stores the tick's `dt` at `+0x98`;
- `0x82558340` → `0x82558110` steps the cloth once per tick with that `dt`;
- `0x82555C28` integrates with `dt` and `dt²`.

It scales with `dt` (stiffer and more damped per second at 120, inferred). It
is not a toggle.

### 10.4 Suspects, ranked

1. **Texture streaming runs once per tick, and part of its pacing counts
   ticks (inferred).**
   - Where: `FStreamingManagerTexture::UpdateResourceStreaming` = `0x827386F8`
     (vtable `0x821356E4` slot 1, constructed at `0x82737398`, object
     `[0x83721BA0]`).
   - What counts ticks:
     - each call re-evaluates `max(1, N/10)` textures, so a full sweep takes
       10 ticks: 167 ms at 60, 83 ms at 120;
     - the countdowns at `+0x10` and `+0x14` drop by 1 per call;
     - `+0x14` is set to 3 when free pool memory falls below `+0x38`.
   - What is time-correct: the fudge factor (`+0x18 += min(dt, cap)·rate`)
     is scaled by `dt`.
   - Why it fits the climb: the camera closes in, so character textures want
     their top mips (`TEXTUREGROUP_Character` MaxLODSize 4096, `PoolSize=62`,
     `HysteresisLimit=30`, `Coalesced.ini [TextureStreaming]`). At twice the
     re-evaluation rate and half the hold time, mip drops and raises on the
     wrestler can alternate. That shows as texture flicker, game-side, in
     both renderers. **This is the best visual match.**
2. **One-shot anim chain at half-frame ticks (`0x82544430`, `0x82541228`;
   §10.2; inferred).** Notifies fire one tick early and jump the time to the
   60 Hz frame. If a notify on the climb switches a material, an attachment
   or a mesh (unknown), it could flip back and forth. Without such a
   notify, the effect is a pose pop.
3. **Fixed 1/60 per-call steps on the pawn's skeleton (`0x828DF7D0`,
   `0x8292A020`, `0x82798160` `+0x3FC`; confirmed code).** They run twice as
   fast. They only move bones, so they can explain shimmer or popping, not a
   texture change.

**Unknown:** what the owner calls "flicker". It could be blur against sharp
(points to 1), surfaces popping or z-fighting (2 or 3), or brightness. The
loading icon that showed when the cvar was toggled is also unexplained.

### 10.5 Experiments (cheap, owner-driven)

**X: split dt size from tick rate (two midasm hooks, one codegen).**
- Hook point: `GEngine->Tick` is called at `0x82417A78` with `f1 = f30`, where
  `f30 = dilation × f31` (`0x82417A58`). After it returns, f30 feeds only the
  empty `0x82BDF098` (×3) and the limiter's compare at `0x82417D64`
  (`fcmpu f10,f30`). A void midasm hook at `0x82417A7C` that scales f30
  therefore changes only the wall time per tick.
- X1 (dt 1/120 at 60 ticks/s): `Wwe120Tick` halves f31 as today, and the new
  hook doubles f30. The game plays at half speed, 60 frames/s.
- X2 (dt 1/60 at 120 ticks/s): no halving, and the new hook halves f30. The
  game plays at double speed, 120 frames/s.
- Read the result:
  - flicker in X1 only: the cause is the size of `dt` (suspect 2);
  - flicker in X2 only: the cause is the tick count (suspects 1 and 3, or
    pacing);
  - flicker in both: a mix of the two;
  - flicker in neither: the cause needs both together.

**S1 (suspect 1):** in the existing `sub_82417780` hook, read the manager at
`[0x83721BA0]` once a second and log:
- the fudge factor `+0x18` and its rate `+0x1C`;
- the per-second deltas of its counters `+0x54`, `+0x58`, `+0x5C`, `+0x60`,
  `+0x64` and `+0x70`.

Compare a climb at 60 with one at 120. Thrash shows as large deltas and a
rate that keeps flipping sign. Confirming test: a `REX_HOOK_RAW` on
`sub_827386F8` that, at 120, runs it only on every other tick with `dt × 2`.
If the flicker stops, streaming cadence is the cause.

**S2 (suspect 2):** a midasm hook at `0x825444A0` that scales f27 (the
look-ahead) to 0 at 120. Also log the climber's anim time (`[obj+0x180]`
plus `+0x158`) and the notify index `+0x10C` per tick across the climb. If
the time steps backwards or repeats, or the flicker stops with no
look-ahead, this is the cause.

**S3 (suspect 3):** a `REX_HOOK_RAW` on `sub_828DF7D0` that skips every
other call at 120 restores the 60 Hz rate for the muscle pulse. The same works for `sub_8292A020`. Only
worth doing if X points at the tick count and S1 is clean.

Tools: Ghidra 12.1.4 MCP, `ghidra/WWEAllStarsTU2` `default.xexp`, read-only
(decompiled: the 115 functions above, plus `0x82551E08`, `0x82544430`,
`0x8259D120`, `0x8259C788`, `0x828DF5B8–0x828DF8C8`, `0x82558110`,
`0x82558278`, `0x82558340`, `0x82555C28`, `0x82737398`, `0x827386F8`,
`0x8244DFE8`, `0x82A91BE0`, `0x82ADAD08`, `0x8275ED20`, `0x82ADB130`);
`python scripts/disasm.py` at `0x82417930`, `0x82417A54`, `0x82417D30`,
`0x82558258`, `0x82544480`; the scratchpad scanners `constscan.py` and
`constfuncs.txt`; `Coalesced.ini [TextureStreaming]`, `[TextureLODSettings]`;
`.scratch/high-framerate/issues/02-owner-experiment-run.md`. The logs
`analysis/high-framerate/t02b.log` and `t02c-emulated.log` were grepped. They
have no marker for the climb and no line from the guest's streaming, so they
add nothing here.

## 11. The vanishing wrestler at 1/120 s (ticket 02)

Date 2026-10-13. Run 4 (`.scratch/high-framerate/issues/02-owner-experiment-run.md`)
showed that the "flicker" is the climbing wrestler (Cena on the turnbuckle)
**vanishing for a moment**. It happens on both renderers. Experiment X:
- 1/120 s steps at 120 ticks/s: he vanishes;
- X1, 1/120 s steps at 60 ticks/s: he vanishes;
- X2, 1/60 s steps at 120 ticks/s: he does not.

This section is static analysis only. Nothing was built or run.

**Result: no code was confirmed as the cause.** Every path that can hide or
fade a wrestler was found and read (§11.2), and none of them depends on the
tick's step. So the vanish is most likely the camera, or the pawn's position,
reacting to per-tick logic (inferred). §11.3 ranks three such mechanisms on
the climb path; their code is confirmed, their link to the vanish is
inferred. One instrumented run (§11.4) decides between them.

### 11.1 What X really separates (correction to §10.5)

X1 runs 120 ticks per second of *game* time (at half speed), and X2 runs 60.
So X does not only test "maths on `dt`". It tests **game time per tick**
against **wall time per tick**:
- ruled out: everything paced by wall clock or by presents. That covers
  texture streaming (§10.4 suspect 1: X1 streams 60 times per wall second,
  as at 60 fps) and presenter pacing;
- still possible: `x/dt` terms, *and* any fixed per-tick step, counter or
  threshold. At 1/120 those run twice per 60 Hz frame of game time, in both
  the base run and X1.

§10.5's reading "X1 only means suspect 2" was too narrow. §10.4 suspect 3
(fixed per-call steps) fits X1 just as well.

### 11.2 Every way the game hides or fades a wrestler (confirmed); none depends on `dt`

| Lever | Code (TU2) | Uses `dt`? | Who triggers it |
|---|---|---|---|
| Actor `bHidden` = bit `0x40000000` of `+0x6C` | `AActor::SetHidden` `0x827E37A8` (exec `0x826392E8`) | No | Natively, only for spawned helper/clone pawns (`0x82AE6B40`, `0x82B36FC8`, `0x82B37270`, `0x82B37768`, `0x82BBB2F8`), never the controlled wrestler. In script (all of `Engine.xxx`), only stock UE3 callers: `Pawn.FellOutOfWorld`, `Pawn.OutsideWorldBounds`, `PlayerController.SetCinematicMode`, pickups, weapons |
| Actor opacity: `+0x154` hold timer, `+0x158` current, `+0x15C` goal (`Actor.OpacityTimer/CurrentOpacity/GoalOpacity`) | `ReduceOpacityForTime` → `0x827E5AB0`; `FadeToOpacity` → `0x827E38F8`; `TickOpacity` → `0x827E3900`: the hold counts down by `dt`, then the opacity moves toward the goal at `dt × 5` (`[0x8206D980]` = 5.0) and calls vtable `+0xD8` | Yes, correctly | Script natives only |
| GamePawn fade: `+0x730` length, `+0x734` time left | `execStartFade` `0x82A91BE0` → `0x82ADACF8`; ticked by `0x82ADAD08` in `0x82AE7320` and `0x82B37768`; it sets the mesh opacity to `+0x734/+0x730` and subtracts `dt` | Yes, correctly | `0x82ADACF8` has no caller other than the exec thunk |
| Mesh opacity, `mesh+0x278` (material parameter `FadeOpaque`, `0x82150E60`) | `UMeshComponent.SetOpacity` → vtable `+0x188` = `0x828211D0` (skeletal-mesh vtable `0x82018B58`) | n/a | The two fades above |
| Camera "inside the character" fade | `USD_CameraMode_ThirdPerson` vtable `0x82011A38` `+0x138` = `0x82502ED0`. If the camera's distances to the `Head` and `Hips` bones add up to less than `|Head−Hips| + [mode+0x130]`, it calls `ReduceOpacityForTime(clamp((sum − 0.8·L)/(0.2·L)), 0.15)` | No (pure geometry) | Only `execUpdateCharacterFade` `0x8244DFE8`, and no script calls it |

How the script side was checked: `Engine`, `Game`, `SD_Camera`, `SD_Actors`,
`SD_Animation`, `SD_Engine`, `SD_Proximity`, `SD_Graphics` and
`SD_ActorUtils` (`.xxx`) were decompressed. These are UE3 v398 packages,
licensee 0x25, made of LZO1X chunks (as in `base-roster.md`; a throwaway
decompressor in the scratchpad, not kept). Their `Function`/`State`
bytecode was then searched:
- for `EX_VirtualFunction` (`0x1B`) followed by the name;
- for `EX_FinalFunction` (`0x1C`) followed by the export/import index.

Nothing calls `UpdateCharacterFade`, `ReduceOpacityForTime`, `FadeToOpacity`,
`TickOpacity`, `SetOpacity` or `GamePawn.StartFade`.

The matching properties exist but are not used by the wrestler, as far as
this check reaches:
- `GamePawn.FadeDistance/FadeLength/FadeTimeRemaining/PawnProximityQuery/PawnProximityPresence`;
- `GameRailCameraActor.CameraProximityQuery/ProximityRadius`;
- `Actor.FadeCondition` (`FC_TriggerWithDistance`, `FC_DistanceAttenuated`, …);
- `FadeDistanceIn/Out`.

Not searched: Kismet in the map packages.

**Inferred:** no fade or hide fires at 1/120 that does not fire at 1/60. A
wrestler who vanishes while not hidden and at full opacity has to be one of
these:
- off screen;
- behind or inside geometry (the turnbuckle pad, the apron);
- seen from inside his own mesh (camera clipping, so only back faces);
- culled because of bad bounds or a NaN bone.

`SkeletalMesh.bLockBoundsToBonePositions/m_BoundsLinkBoneName/LockBoundsToBonePositionDelta`
(Engine) show that THQ customised skeletal bounds. Their native reader was
**not found**.

### 11.3 Per-tick mechanisms on the climb path (code confirmed, link to the vanish inferred)

1. **The match camera averages its focus over the last 30 *ticks*.**
   - `AGameProgrammaticCamera.UpdateProgrammaticCamera` → vtable
     `0x821D8558` `+0x2DC` = `0x82B60FD8` → `0x82B60A18(dt, cam, &out)`.
   - Each call pushes one sample of the wrestlers' bounding-box centre into a
     ring at `cam+0x3C8`:
     - count `+0x3CC`, filled up to `subfic r30,r10,30` at `0x82B60B00`;
     - index `+0x3C4 = (+0x3C4 + 1) % 30` at `0x82B60DA4–0x82B60DC8`.
   - It returns the mean.
   - At 1/60 the window is 0.5 s; at 1/120 (base run and X1) it is 0.25 s;
     in X2 it is back to 0.5 s of game time. **This matches X.**
   - When `f1 == 0` the function averages without pushing (the `dVar18 == 0`
     branch). That gives a clean fix seam.
   - Other camera code is time-correct:
     - the per-wrestler tracker `0x82B5DCB8` blends toward the turnbuckle
       post while a wrestler is on it (`0x82B5CC28` / `0x82B5DA78`) at
       `rate·dt`;
     - the speed and acceleration limiter `0x82B5CE18` (`Δ/dt`, clamped,
       `·dt`) and the focal-point `MoveTowardTarget` `0x82500390`
       (`1 − e^(−k·dt)`) are framerate-independent.

   So the averaging window is the only part that changes the framing with
   tick size.
   - Effect (inferred): during the fast vertical climb the camera follows
     about twice as tightly and swings with it. That can briefly push the
     climber out of frame, behind the post, or against the near plane.
2. **Movement curves counted in ticks, `0x82AE2138` (the wrestler's movement
   tick; it runs only in game states 4, 0x15, 0x21 and 0x25).**
   - "Move or jump to a point" anim-FSM statements `0x82593D50` and
     `0x825945C8` set the launch velocity (`+0x468..+0x470`, in units per
     60 Hz frame) from a duration in frames. They can start an ease-in ramp:
     `+0x1CDC..+0x1CE4` holds the velocity, `+0x1CE8` the length in frames,
     `+0x1CEC` the frames left.
   - In `0x82AE2138` the ramp advances one step per *tick*:
     - `+0x1CEC −= 1.0` at `0x82AE233C` (f21 = 1.0);
     - the increment is `sin(n·90°/N) − sin((n−1)·90°/N)`.
   - Displacement per tick is `velocity·dt·60`, so at 1/120 the ramp lasts
     half as long and covers half the distance.
   - In the same block, `vel.z += g − g·dt·60`
     (`fnmsubs f10,f0,f23,f31` at `0x82AE2344`; f24 = `dt`, f23 = 60,
     f31 = `g` from pawn vtable `+0xE8`). That is **exactly 0 at 1/60 and
     +g/2 per tick at 1/120**: extra gravity on every ramped jump.
   - Effect (inferred): a jump onto the turnbuckle ends short or low. The
     spline attach then pulls him back at fixed per-tick fractions
     (`0x82ADC2F8`):
     - 0.1 of the error per tick (`[0x82062764]`);
     - in movement state 7, a blend `+0x19C4 += 0.016` per tick
       (`[0x8207BD18]`).

     For a few ticks his actor can sit inside the post or apron.
   - Close by: anim-state entry `0x824E3318` sets `+0x1D44 = 2`
     (`[0x8352F648]`), and `0x82AE7320` counts it down once per tick before
     it restores the collision flags `0xC0800000` on `+0x21C`. Collision
     comes back after 16.7 ms instead of 33 ms (the `CharacterCollisionType`
     statement `0x82597848` writes the same bits).
3. **The anim-time frame counter `pawn+0x1D00` counts ticks.**
   - `0x82540E30` adds 1.0 per tick for the pawn's current animation
     (`lfs f0,0x1D00(r24)` / `fadds f13,f0,f26` / `stfs` at
     `0x82541064–0x8254106C`; f26 = 1.0, f27 = `dt`).
   - The anim-FSM triggers compare it against authored 60 Hz frame numbers
     (`0x8254B630`, `0x8254B8C0`, `0x82BAB548`).
   - At 1/120 every "frames N..M of this animation" window opens and closes
     at half the animation time. A climb is a chain of FSM states, so a
     window can let a transition fire early or let it lapse. This
     corrects §10.1: `0x8254B8C0` is not only "frames × 1/60".
   - It also belongs to ticket 04: reversal and combo windows.

Re-checked and time-correct: §10.2's notify walker `0x82544430`, which is
anim-time based; `UpdateSkelPose` `0x82798160`, where root velocity
`+0x3F0 = Δroot/dt` and root motion is applied through `0x827E0198`; the
camera distance step `0x82502B50`; `MakeLerpRateFramerateIndependent`
`0x824FFF10` (`clamp(rate·dt·30)`). The root-motion thresholds in
`0x82798160` (`[0x82006074]` = 0.00137, `[0x82062760]` = 1e−8) are far
below the per-tick motion of a climb.

### 11.4 Next run: one probe, three toggles (one codegen)

**Probe (log only).** In the `sub_82417780` hook, after `GEngine->Tick`,
with `wwe_tick_120` on, log one line per wrestler per tick for about 10 s
after a hotkey. The owner presses it at the corner. Fields:
- pawn manager `0x837450D0` (valid once bit 0 of `[0x8374523C]` is set):
  count `[+0x40]`, pawn *i* = `[0x837450D0 + 0x44 + 4i]`;
- per pawn:
  - `Location` `+0xE8/+0xEC/+0xF0`;
  - `bHidden` = (`[+0x6C]` >> 30) & 1;
  - opacity `+0x158` / goal `+0x15C`;
  - mesh `[+0x3AC]` and its opacity `+0x278`;
  - velocity `+0x468..+0x470`;
  - ramp frames left `+0x1CEC`;
  - frame counter `+0x1D00`;
  - movement state byte `+0x76E`;
  - spline `+0x19D4`;
  - flags `+0x21C`, `+0x6F8`, `+0x6FC`;
- the camera: a `REX_HOOK_RAW` on `sub_82B60FD8` stores `r3`. Log its
  `+0xE8..+0xF0` and the ring count `+0x3CC`.

Reading:
- a hide or opacity field changes at the vanish: §11.2 is wrong; follow
  that field;
- the camera ends up within about 30 UU of the climber or swings past him:
  suspect 1;
- the pawn's Z drops below the turnbuckle top, or `Location` jumps: suspect 2;
- the FSM state changes early compared with a 1/60 run: suspect 3.

**Toggles (each restores 60 Hz-equivalent behaviour at 1/120; cvars, one
codegen):**
- T1 (suspect 1): `REX_HOOK_RAW(sub_82B60A18)`. On every other tick, while
  the step is 1/120, call the original with `ctx.f1 = 0.0`. That takes the
  average-only branch, so 30 samples span 0.5 s again. Ticks in between push
  normally. **This is the cheapest and cleanest test, and the recommended
  first fix shape.**
- T2 (suspect 2), two midasm hooks:
  - at `0x82AE2340` (before `stfs f11,0x1CEC(r30)`), with
    `registers = ["f11","f13","f24"]`: set `f11 = f13 − f24·60`, so the
    ramp counts down in 60 Hz frames;
  - at `0x82AE2348` (before `fadds f9,f10,f12`), with `["f10"]`: set
    `f10 = 0` while the step is 1/120. `vel.z` then gets nothing added, as at
    1/60.
- T3 (suspect 3): a midasm hook at `0x8254106C` (before `stfs f13,0x1D00(r24)`),
  with `["f0","f13","f27"]`: set `f13 = f0 + f27·60`.

If T1 alone removes the vanish, keep it as the production fix. It is
display-only and does not touch gameplay. T2 and T3 fix gameplay timing,
which ticket 04 needs anyway. Each midasm hook needs an
`[[entrypoint.midasm_hook]]` entry in `wweallstars_manifest.toml` with an
instruction-level comment, like `Wwe120Tick`. T1 is a `REX_HOOK_RAW` on an
existing function and needs none. All the addresses are inside functions that
are already generated, so no new seed is needed.

Tools for §11: Ghidra 12.1.4 MCP on `ghidra/WWEAllStarsTU2` `default.xexp`,
read-only. Decompiled:
- the camera: `0x8244DFE8`, `0x82502ED0`, `0x827E5AB0`, `0x827E3900`,
  `0x8244C9C0`, `0x824FFF10`, `0x82502A50`, `0x825001F0`, `0x82500390`,
  `0x82502CD0`, `0x82B60FD8`, `0x82B60A18`, `0x82B5DCB8`, `0x82B5CE18`,
  `0x82B5FBC0`;
- fades and hiding: `0x82ADAD08`, `0x82638A18`, `0x82639A78`, `0x82637A20`,
  `0x8244F670`, `0x82822688`, `0x82821C30`, `0x828211D0`, `0x826392E8`,
  `0x827E37A8`, `0x82AE6B40`, `0x82B37768`, `0x82BBB2F8`, `0x82A912D0`,
  `0x82ADAF20`;
- skeleton and animation: `0x82798160`, `0x82540E30`, `0x82544430`,
  `0x82551E08`, `0x8254B630`, `0x8254B8C0`, `0x82BAB548`;
- pawn movement and state: `0x82AE2138`, `0x82ADC2F8`, `0x82593D50`,
  `0x82AE7320`, `0x82597848`, `0x824E3318`.

Disassembly with `python scripts/disasm.py`: `0x82540E30`, `0x82541050`,
`0x82AE2250`, `0x82B60AF0`, `0x82502B50`, `0x825029D0`.

Scans run in Ghidra over `.text`:
- every function that divides by its incoming f1 (64 found; the game-side
  ones were read);
- all native readers and writers of `+0x1D00`, `+0x1D44`, `+0x19B8..+0x19D4`
  and `+0x1CDC..+0x1CEC`;
- the vtable-`+0x15C` call sites.

Decompressed packages: those listed in §11.2.

## 12. Fixes built (ticket 04, 2026-10-14)

Built behind `wwe_120fps_matches`, only while a tick steps 1/120 s, each
switchable through the bitmask `wwe_120fps_fixes` (`src/game_tick_log.cpp`):

| Bit | Site | Fix |
|---|---|---|
| 1 | `0x82B60A18`, match camera focus average | its output becomes the mean of its own 60-sample ring (0.5 s), fed every tick. The first version passed dt = 0.0 on odd ticks (the mean-only branch), which froze the camera every other tick: a 60 Hz judder (ticket 06) |
| 2 | `0x82AE2290`, `0x82AE2340`, jump ease-in ramp | previous point and countdown step 0.5 frame instead of `f21` = 1.0 |
| 4 | `0x82AE2348`, ramp gravity term | `f10 − g/2`: half of the 60 fps term `g·(1 − d)` at dilation `d` |
| 8 | `0x8254106C`, anim frame counter pawn `+0x1D00` | adds 0.5 instead of `f26` = 1.0 |

These count ticks, not game time: `dt` carries the time dilation, and at
1/60 s each tick adds a whole frame even in slow motion. So a halved tick adds
half a frame, not `dt·60` (which would slow slow-motion down 2.5× more at
dilation 0.4; review, 2026-10-14). Scripted match `analysis/session-hf6`:
anim frames fire 242 times a second (2 wrestlers × 121 ticks), the camera
fix 120 (`0x82B60A18` runs twice per tick; every other tick keeps the 60 fps
samples per second either way), the ramp during jumps.

Correction to §11.4 T2: the ramp adds `sin(n·a) − sin((n−1)·a)` to the
velocity each tick (`0x82AE2278–0x82AE22D4`; `n` = `+0x1CEC`, `a` = 90°/N via
`f25`, `f31`). Counting `n` down by 0.5 while keeping `n−1` adds a full
frame's increment every half frame, so the launch velocity would reach about
twice its target. Both the countdown and the previous point (`fsubs
f9,f10,f21` at `0x82AE228C`) take `dt·60`; the sum still telescopes to
`sin(N·a)` = 1.

Registers confirmed by disassembly: in `0x82AE2138`, `f24` = dt (`fmr f24,f1`
at `0x82AE2158`), `f21` = `[0x82001CC8]` = 1.0 (`0x82AE21F8`), `f23` =
`[0x8206B200]` = 60.0 (`0x82AE2254`); in `0x82540E30`, `f27` = dt (`fmr
f27,f1` at `0x82540E4C`), `f26` = 1.0 (`0x82541004`).

**Match predicate (ticket 03).** The crowd scheduler is not one: the main
menu's 3D arena backdrop runs it too (scripted runs `analysis/session-hf1`,
`hf2`). The pawn manager count `[0x837450D0 + 0x40]` (ready bit 0 of
`[0x8374523C]`) is 0 in the title, menus and entrances, and 4 in a 1v1 from
about 3 s after the entrance.

**Stutter at 120 in the owner's run (ticket 02, `tp-swaps.csv`).** The game
ticks on time and, in a scripted match at resolution scale 3, the GPU needs
about 3 ms per frame (`hf5-gpu.csv`), so neither is the cause. In the owner's
fullscreen 1440p run on the 360 Hz FreeSync panel, present to shown drifted
from about 1 ms to 8–15 ms and wrapped at about one 120 Hz period; the
presenter's present-wait then held presents up to 9.5 ms, which RTSS shows as
spikes. A windowed 720p scripted run does not show it (interval spread
0.05 ms). Inferred: the display was refreshing on its own ~120 Hz clock,
beating against the guest vblank's host clock. Owner A/B for the next
session (game settings only, nothing in the driver): `--present_wait=false`,
and `--present_pacing_offset_us=2000`.

## Sources

- TU2 Ghidra (`ghidra/WWEAllStarsTU2`, `default.xexp`), decompiled: `0x82417780`,
  `0x82A62908`, `0x82A61D80`, `0x830D9F00`, `0x830D6CC0`, `0x82BBBAE0`,
  `0x82540E30`, `0x825F2160`, `0x825DFD30`, `0x82DBDFF8`, `0x82A928B8`,
  `0x82BDF098`; strings L"LOCKSTEPOFF" `0x82001B98`, "MaxRotationPerFrame…"
  `0x82084584`, `UGameAnimFSMReversalButtonTrigger` `0x821D3CA0`,
  `UGameAITransitionLogic_IsReversalWindowOpen` `0x821C3980`,
  `USD_ReplaySystem` natives `0x82009848–0x82009D48`.
- TU2 disassembly (`scripts/disasm.py`): `0x82417908–0x82417954`,
  `0x824144C0–0x824145AC`, `0x82907BC0–0x82907C34`, `0x825410A8–0x825410F4`,
  `0x82541210–0x8254124C`, `0x82798EC0–0x82798EF4`, `0x82811560–0x8281169C`,
  `0x825F21C8–0x825F2204`; constants `0x82001CCC`, `0x82002F38`, `0x8206B200`
  (60.0), `0x82004938` (0.5), `0x8206CEF8` (1/120), `0x82098798`.
- Config: `assets/Game/Config/Xenon/Cooked/Coalesced.ini` (`[Engine.WorldInfo]
  HkSimRateHz`, `[Game.GamePawn]`, `[SD_Actors.SD_Pawn]`,
  `[Game.GamePawnMovementTracker]`, `[GameHUD]`, `[GameSubmissionMiniGame]`,
  `[D3DDrv.D3DRenderDevice]`).
- Logs: `analysis/sync-test-*/Station{1,2}.log` via `scripts/syncsim_log.py dump`.
- This repo: `src/game_tick_log.cpp:159-179, 225-253`,
  `wweallstars_manifest.toml:451-467`, `src/frame_clock.cpp:66`,
  `docs/adr/0016-guest-vblank-locked-to-display.md`,
  `docs/adr/0021-online-play-revived.md`, `docs/research/60fps-entrances.md`,
  `movement-judder.md`, `performance-and-low-end-hardware.md`,
  `syncsim-replay-log.md`, `online-mode-feasibility.md` §6.7, `spin-loops.md`,
  `docs/ROADMAP.md:23,35`.
- ReXGlue: `SDK/src/ui/window.cpp:63-66`,
  `SDK/src/kernel/xboxkrnl/xboxkrnl_video.cpp:76-78`,
  `SDK/src/graphics/graphics_system.cpp:41-47, 169-175, 402-415`,
  `SDK/include/rex/graphics/vblank_source.h:32-42`,
  `SDK/src/ui/presenter.cpp:94-130, 930`,
  `SDK/src/graphics/command_processor.cpp:1342-1363`,
  `SDK/src/codegen/builders/context.cpp:383-387`,
  `SDK/src/ui/vulkan/vulkan_presenter.cpp:31-33`.
- FidelityFX SDK 1.1.4 (`.tools/fidelityfx-sdk`): `readme.md`,
  `ffx-api/include/ffx_api/ffx_framegeneration.h:113-138`,
  `ffx-api/include/ffx_api/vk/ffx_api_vk.h:50-81`;
  https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation/
- Vulkan: https://registry.khronos.org/vulkan/specs/latest/man/html/VkPresentModeKHR.html
- AMD AFMF 2 Vulkan support:
  https://community.amd.com/t5/pc-drivers-software/amd-software-adrenalin-edition-24-9-1-release-notes/m-p/714647/highlight/true ,
  https://www.pcworld.com/article/2411947/amd-releases-ai-powered-fluid-motion-frames-2.html
