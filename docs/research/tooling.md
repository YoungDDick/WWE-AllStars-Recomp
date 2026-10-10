# Tooling for M2–M6

Date: 2026-10-03. Which tools and workflows would most help take the recomp
from M2 (native renders menus) to v1.0, and how each would be used here.
Sources are primary (repos, source, official docs) unless marked
**unverified**. Upstream state was read through `gh api` on 2026-10-02/03.

## Summary (highest leverage first)

1. **Scripted input driver keyed on frame count.** Replaces focus-dependent
   `keybd_event` injection, so every scene in the test matrix can be reached
   unattended. Every other loop below depends on it.
2. **Golden-scene render tests** (pinyon-shift model): a script gives the
   input, the frame to dump and the checks. One command per scene, run on
   Classic and Native. This turns `native_frame_check.py` into the M3/M4
   regression suite.
3. **RenderDoc in-application capture plus debug-utils labels per draw.** The
   SDK already loads the RenderDoc API. A capture flag plus a
   `qrenderdoc --python` export gives agents per-draw inputs and outputs.
4. **NVIDIA FLIP (`flip-evaluator`) instead of mean absolute difference.** It
   gives a perceptual mean plus a heatmap, so the agent sees where frames
   differ.
5. **Vulkan validation (sync, GPU-AV) as a standing switch.** The SDK's
   `--vulkan_validation_enabled` covers Native too, because Native shares the
   SDK device.
6. **Xenia GPU trace (`.xtr`) for per-draw Xenos ground truth.** It needs
   self-built Canary tools, or a port of `TraceWriter` into rexglue-source
   (skate3-recomp has one).
7. **Cherry-pick 4 ReXGlue codegen fixes rather than upgrade.** There has been
   no release since v0.10.0, and nothing new helps the native renderer.
8. **Matt Pocock skill chain on a local-markdown tracker** (`.scratch/`, no
   git remote): wayfinder for foggy milestones (M3, M6), to-spec → to-tickets
   → implement for scoped work, diagnosing-bugs for regressions.
9. **Ghidra BSim from a symbolled UE3 360 build** (Fable Anniversary prototype
   PDB) to name UE3, Scaleform and FMOD code for the M4 hooks.
10. **Linux: follow rexglue's Ubuntu 22.04 CI recipe and ship a Flatpak**
    (UnleashedRecomp model). Proton only as a stopgap.

## 1. ReXGlue after v0.10.0

Findings:

- Upstream is `github.com/rexglue/rexglue-sdk`. Work happens on `development`;
  `main` carries only releases. There is no stable release after v0.10.0. The
  newest nightly is `nightly-20261002-bd833a2a`
  (https://github.com/rexglue/rexglue-sdk/releases).
- `compare/f5337cdc...development` is 24 commits ahead, 0 behind.
  - Codegen correctness: `6319e236` (vpkuwus/vpkuhus wrong when the
    destination is also a source, #449), `7f7c92ef` (`stwcx.` via
    `std::atomic` CAS), `bd833a2a` (real fences for sync/lwsync/eieio),
    `ea222e98` (unsigned register types, #450; large generated-code diff).
  - Also: input (vibration, deadzones, hot-plug: `3cd72439`, `3f34ffc3`,
    `78985dda`, `c6958520`), window/monitor changes without restart
    (`289f5180`), log path and budget (`b971840d`), leak fixes (`01ac2a3b`,
    `ee20900e`, `0c7b01a0`).
  - Nothing for tracing, debugging, function discovery or native rendering.
- The hook and discovery features already exist at our pin:
  `.tools/rexglue-source/src/codegen/config.cpp` parses `midasm_hook`
  (L311–362), `switch_tables` (L257), `functions` (L178), `indirect_calls`
  (L376) and `setjmp`/`longjmp` (L124/128); `tests/ppc/asm` holds 166 asm
  tests.

How we'd use it here:

- Keep the v0.10.0 pin (ADR-0005). Add `6319e236`, `7f7c92ef` and `bd833a2a`
  as patches in `patches/`, regenerate and smoke-test; the atomics and fences
  matter for Havok and FMOD threads. Take `ea222e98` on its own later.
- Re-evaluate the input commits at M4 (hot-plug, vibration), together with
  SVR2011 as ADR-0005 requires.

Effort/payoff: half a day; moderate payoff (a silent miscompile is expensive to
find later). A full upgrade is not worthwhile now.

## 2. Other Xbox 360 recomps

Findings:

- **XenonRecomp** (https://github.com/hedge-dev/XenonRecomp): XenonAnalyse
  finds jump tables by pattern and writes TOML ("no fully generic solution",
  README "Jump Tables"); boundaries come from `.pdata` plus `bl` targets;
  codegen is validated against Xenia's PPC tests
  (`XenonRecomp/test_recompiler.cpp`).
- **XenosRecomp** (README "Shader Cache"): shaders compile to a DXIL/SPIR-V
  cache embedded as `.cpp`, keyed by XXH3-64, with SPIR-V compressed by smol-v
  and zstd. This is consistent with ADR-0012, except that our launcher
  converts at setup instead of embedding.
- **xenon-jumptables** (https://github.com/xdzleo/xenon-jumptables): an IDA
  tool that recovers switch tables the built-in detection misses and emits
  ReXGlue `[[switch_tables]]` TOML.
- **pinyon-shift** (Forza Horizon 1, https://github.com/arcanite24/pinyon-shift)
  has the most complete verification tooling found:
  `config/render-tests/*.fh1test` scripts feed synthetic controller input per
  frame, dump PPMs and assert `expect-image`, `expect-performance`,
  `expect-simulation-time` (`tools/run-fh1-render-test.py`;
  `tools/create-render-seed.py` for repeatable start states).
  `docs/native-renderer/NATIVE_FRAME_CONTRACT.md` defines a JSON-lines frame
  census. Its native renderer lives in a forked command processor, not in D3D
  hooks.
- **Xerenge** (Burnout Revenge, https://github.com/shipa-2/Xerenge) is closest
  to our design: a D3D-hook renderer (`rexglue/src/plume_d3d_hooks.cpp`)
  chosen at launch, with Xenos as the reference; also a forked XenosRecomp,
  `plume_selftest.cpp`, `movie_trace.cpp`, an aarch64 Linux toolchain.
- **skate3-recomp** (https://github.com/portingpete/skate3-recomp) has a
  rexglue build with `include/rex/graphics/trace_writer.h`, `trace_player.h`,
  `trace_viewer.h`, `trace_dump.h`, `debug_overlay.h` and
  `cmake/ppc_test_pipeline.cmake`. **Unverified** that this is a straight port
  of Xenia's GPU trace.
- Smaller: `reNut` (debug menu, `shader_dump.cpp`), `hells-gate-recomp`
  (`docs/linux-cross-compile.md`), `AC6_recomp` (`d3d_hooks.cpp`, capture
  bridge). The hedge-dev repos have no frame comparison or golden traces
  (README and tree searched; not exhaustive).

How we'd use it here:

- Copy pinyon-shift's render-test idea (§4) and its frame-contract idea. Our
  `d3d_census.cpp` and `check_census.py` already do half of it.
- Read skate3-recomp's trace writer before porting Xenia's (§3).
- Keep xenon-jumptables in reserve. So far `find_table_targets.py` has covered
  pointers in data (`docs/boot-investigation.md` L26–36), but later modes may
  hit switch tables it misses.

Effort/payoff: hours of reading; high payoff for the harness design.

## 3. Graphics debugging

### RenderDoc

Findings:

- The SDK already loads the RenderDoc API when RenderDoc is attached
  (`.tools/rexglue-source/src/ui/renderdoc_api.cpp` L18–45, created at
  `src/ui/vulkan/vulkan_instance.cpp` L40, exposed at
  `include/rex/ui/vulkan/instance.h` L37), and turns on `gpu_debug_markers`
  when it does (`src/graphics/flags.cpp` L31–50). The vendored header only
  defines API 1.0.0/1.0.1 (`thirdparty/renderdoc/renderdoc_app.h` L448–449):
  `TriggerCapture`, `Start`/`EndFrameCapture`, `SetLogFilePathTemplate`, but
  not `SetCaptureTitle` or multi-frame capture.
- Upstream RenderDoc is v1.46 (2026-08-31). The current header has
  `StartFrameCapture` (L536), `EndFrameCapture` (L547),
  `SetCaptureFilePathTemplate` (L409), `SetCaptureTitle` (L565) and
  `RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE` (L505)
  (https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/api/app/renderdoc_app.h,
  https://renderdoc.org/docs/in_application_api.html).
- Headless analysis: `qrenderdoc --python script.py` uses the bundled Python
  (`qrenderdoc/Code/qrenderdoc.cpp` ~L346); the module chain is
  `OpenCaptureFile` → `OpenCapture` → `GetRootActions`
  (https://renderdoc.org/docs/python_api/examples/renderdoc_intro.html);
  `renderdoccmd capture -w` launches and waits (`renderdoccmd.cpp`
  L1619–1636). **Unverified**: that the Python path runs without a window,
  and that `convert` emits `zip.xml`.

How we'd use it here:

- Add `--wwe_native_capture_frame=N`: call `StartFrameCapture` on frame N and
  `EndFrameCapture` at the next present, and launch through
  `renderdoccmd capture -w`. This works for both renderers.
- Label each Native draw with `vkCmdBeginDebugUtilsLabelEXT`: guest draw index,
  ring or IB source (ADR-0015), caller LR, and VS/PS hash. RenderDoc's event
  tree then lines up with `--wwe_native_trace_frame` and the census.
- Add `scripts/rdc_export.py` (via `qrenderdoc --python`) to write per-draw
  JSON (state, textures, RT thumbnails), so agents diff two captures without
  the GUI.

Effort/payoff: about 2 days. Very high payoff for M2/M3: it answers what the
GPU actually received, which `native-renderer.md` (2026-10-02, late) already
asks for.

### Vulkan validation

Findings:

- `--vulkan_validation_enabled` (`src/ui/vulkan/vulkan_provider.cpp` L21)
  requests `VK_LAYER_KHRONOS_validation` (`vulkan_instance.cpp` L224). Native
  uses the SDK device (`src/native/vk_context.cpp` L166,
  `native_renderer.cpp` L920), so one flag covers both renderers.
- These settings are off by default (`VkLayer_khronos_validation.json.in`,
  SDK 1.4.363): `validate_sync` (L444), `validate_best_practices` (L648),
  `gpuav_enable` (L727), `printf_enable` (L1411), `log_filename` (L1541).
- They are set as `VK_KHRONOS_VALIDATION_<KEY>` environment variables
  (Vulkan-Utility-Libraries `src/layer/layer_settings_util.cpp` L58–92) or
  through `VK_LAYER_SETTINGS_PATH`
  (https://github.com/KhronosGroup/Vulkan-ValidationLayers).

How we'd use it here:

- Add a `-Validate` switch to `smoke.ps1` and `native_frame_check.py`. It sets
  the flag plus `VALIDATE_SYNC=1` and `LOG_FILENAME` in the run directory, and
  any new VUID counts as RED.
- Turn on GPU-AV only when chasing out-of-bounds descriptor or buffer reads.

Effort/payoff: hours; high payoff.

### Xenia GPU trace

Findings:

- `.tools/xenia-canary` holds only the release binary (`xenia_canary.exe` +
  `LICENSE`). The trace tools must be built from source. Upstream Canary
  (`b083312b8b`) still has the `xenia-gpu-vulkan-trace-viewer` and
  `-trace-dump` targets (`src/xenia/gpu/vulkan/CMakeLists.txt` L14–67).
- Recording: F4 / "Trace Frame" (`src/xenia/app/emulator_window.cc` L853,
  L1625) or `--trace_gpu_stream` writes `<trace_gpu_prefix>/<TITLEID>_<n>.xtr`
  (`command_processor.cc` L253–288). The local config sets the prefix at
  `.tools/xenia-canary/xenia-canary.config.toml` L294–296.
- A trace holds the ring and indirect buffers, registers, memory
  reads/writes, EDRAM snapshots and the gamma ramp (`trace_protocol.h`
  L45–58).
- The viewer shows registers, shaders, vertex fetch and textures per draw
  (`trace_viewer.cc` L211–217, L590–1099). The dump tool only writes the
  final front buffer as PNG (`trace_dump.cc` L50–160).
- rexglue-source has no `TraceWriter` or `trace_gpu` (grep), so Classic cannot
  write `.xtr`.

How we'd use it here:

- Build Canary's viewer and dump tool once, pinned under `.tools/`. Trace the
  same menu and match frames in Canary. Use the trace as per-draw ground truth
  (registers, fetch constants, texture formats) when Native disagrees with
  Classic, or when Classic itself looks wrong.
- Later, port the trace writer into rexglue-source as a patch, with
  skate3-recomp as the reference, so traces of our own Classic build can be
  compared with Canary's.

Effort/payoff: about a day to build the tools, 2–4 days for the port. High
payoff for M3 textures, render targets and resolves; low for M2.

### Perceptual image diff

Findings:

- NVIDIA FLIP: `pip install flip-evaluator`. Command line:
  `flip -r ref.png -t test.png`. Python: `flip.evaluate(ref, test, "LDR")`
  returns the error map, the mean and the parameters
  (https://github.com/NVlabs/flip README L50–72, `src/python/api_example.py`).
- Alternatives: `skimage.metrics.structural_similarity(..., full=True)` (SSIM
  map), ImageMagick `compare`, and odiff v4.5.0.

How we'd use it here:

- Replace the mean-absolute check in `native_frame_check.py --mode diff` with
  the FLIP mean, and write the heatmap PNG next to the dumps for the agent to
  read.
- Normalise gamma first. Classic dumps the guest front buffer as ARGB, while
  Native dumps its own surface.

Effort/payoff: about 2 hours; high payoff.

## 4. Headless, agent-friendly verification

Findings:

- Input today is `keybd_event` with window-focus bouncing
  (two keyboard scripts, replaced by `scripts/session.ps1` in ticket 11); screenshots are window captures
  (`smoke.ps1 -ScreenshotEvery`). Guest input enters at
  `XamInputGetState_entry` (`.tools/rexglue-source/src/kernel/xam/xam_input.cpp`
  L95), drivers in `src/input/{xinput,sdl,mnk,nop}`: the seam for a replay
  driver.
- SVR2011 already has a perf gate (`scripts/benchmark_gate.py`),
  `check-charselect-flicker.ps1` and Tracy capture (`capture-profile.ps1`;
  Tracy 0.13.1 hash-pinned by `prepare-profiler.ps1`, already in `.tools/`).
- Hosted GPU runners (T4) bill per minute with no free minutes for private
  repos (https://docs.github.com/en/actions/reference/runners/larger-runners).
  Lavapipe (Vulkan 1.3, Windows port) and SwiftShader (`VK_ICD_FILENAMES`)
  exist; **unverified** whether Classic's FSI/EDRAM path runs on them. The
  repo is private and local-only, so hosted CI is out anyway.

How we'd use it here:

- **Input replay driver**: `--wwe_input_script=path` applies
  `frame: buttons/sticks` lines per guest swap, not wall time;
  `--wwe_input_record` writes the same format from a real pad, so a human
  plays a route once and the agent replays it. Thread timing limits
  determinism, so a step can also wait on state ("until census shows scene
  X").
- **Scene tests**: `tests/scenes/*.scene` = input script + dump frames +
  expectations (FLIP vs golden, FLIP Classic-vs-Native, census invariants,
  perf gate), run by `scripts/scene_test.py <scene> --renderer
  classic|native|both`. Goldens live in `analysis/golden/` (gitignored, game
  data). The M4 test matrix becomes a list of scenes.
- **Local nightly**: a scheduled pwsh task (or `/loop`) runs the scene suite
  on both renderers into `analysis/nightly-<date>/`.

Effort/payoff: 2–3 days. Highest payoff: the M3 to M5 exit criteria are all
"plays or matches in Native".

## 5. Middleware

Findings:

- **XMA**: ReXGlue ports Canary's "new" XMA context
  (`.tools/rexglue-source/src/audio/xma_context.cpp`; `XMAFRAMES` L69,
  `ResetDecoderState` L192–200) over the `wmarti/FFmpeg` fork, which registers
  only wmapro, wmav2, xmaframes and mp3
  (`thirdparty/ffmpeg-overlay/codec_list.c` L2–15). Canary can switch with
  `xma_decoder = fake|master|old|new` (`src/xenia/apu/xma_decoder.cc`
  L64–75). Upstream FFmpeg has `xma1`/`xma2` (`libavcodec/wmaprodec.c`
  L2103/L2117) but no `xmaframes`.
- **FMOD Ex** is guest code. The image has `.\src\fmod_codec_xma.cpp`
  (0x822F4C28), `.\src\fmod_output_xenon.cpp` (0x822F58E8) and about 60 more
  source paths, useful for naming. Xenia's compat entry reports "Audio loops
  during Entrances" and match crashes on an old build
  (https://github.com/xenia-project/game-compatibility/issues/1536). SVR2011's
  only audio change is `patches/rexglue-xmp-no-delay.patch`
  (`docs/performance.md` L464–467).
- **Bink 1** is guest code (sections `BINKCONS`/`BINKBSS`/`BINKDATA`; string
  "Error reading Bink header." at 0x8234EAAC). rexglue's FFmpeg tree contains
  `bink.c`/`binkaudio.c` but does not register them. No 360 recomp that
  replaces Bink with native playback was found.
- **Scaleform GFx 2.x** (string "incompatible GFX file, version 2.x
  expected" at 0x82307F48). Public Autodesk docs only cover 4.x.
  **Speculative**: expect many small draws, user-memory or dynamic vertex
  data, partial updates of glyph-cache textures, and stencil masks. This is
  the same class of path as the user-memory draw fix of 2026-10-03.

How we'd use it here:

- Leave XMA, FMOD and Bink as guest code.
- Add audio checks to scene tests: tap the XAudio output PCM, check it is not
  silent, and hash repeated buffers to catch a stuck loop. Give the entrance
  loop its own scene.
- If a decoder bug appears, bisect with Canary's `xma_decoder` setting before
  touching rexglue.
- Attribute Scaleform draws in the census by caller range once Ghidra names
  the GFx renderer, since menus are mostly GFx.

Effort/payoff: about a day for the audio checks. Bink and Scaleform need no
new tools.

## 6. Reverse-engineering aids

Findings:

- XEXLoaderWV is maintained (latest release targets Ghidra 12.1.3) and loads
  PDB/XDB (https://github.com/zeroKilo/XEXLoaderWV). idaxex 0.44 (IDA 9) names
  imports; its SDK-function naming is original-Xbox XBE only (README L25–33).
- FunctionID needs identical bytes (same compiler). BSim matches fuzzily and
  works with a file-based H2 database
  (https://ghidra.re/ghidra_docs/GhidraClass/BSim/BSimTutorial_Intro.html). No
  public X360 XDK FunctionID database was found.
- The Fable Anniversary prototype (Apr 27, 2013) ships
  `WellingtonGame-Xbox360-{Debug,Release}.xex` with PDBs
  (https://hiddenpalace.org/Fable_Anniversary_(Apr_27,_2013_prototype)). A
  2013 UE3, so structs differ, but XDK, CRT and core UE3 code should match.
  **Unverified** whether it links Havok, FMOD or Scaleform.
- `scripts/match_functions.py` already matched 349 of 563 D3D functions
  against SVR2011 (`native-renderer.md` "D3D hook map"). SVR2011 has no other
  symbol tooling.

How we'd use it here:

- Load the Fable debug XEX + PDB with XEXLoaderWV and build a BSim H2 database
  from it plus SVR2011's named D3D range. Query it from the WWE Ghidra project
  and write accepted names back through the Ghidra MCP server (ADR-0013).
- Use the embedded source-path strings (FMOD, Bink, UE3 class names) as cheap
  anchors first.

Effort/payoff: about 2 days. Medium payoff, concentrated in M4 (save, profile,
input, achievement and online hook targets).

## 7. Linux and Steam Deck

Findings:

- ReXGlue presets `linux-amd64`/`linux-arm64` (`CMakePresets.json` L33–45,
  L91–130): Ninja Multi-Config, clang, C++23, `-march=x86-64-v2`. Its CI
  (`.github/workflows/_build-platform.yaml`) builds in `ubuntu:22.04` for a
  glibc 2.35 floor (L31–36) with clang-20/lld-20 (L62–83), the LunarG Vulkan
  SDK (L92), g++-13 libstdc++, cmake ≥ 3.25 (L94–99) and GTK3, X11/xcb,
  Wayland, xkbcommon, libdecor dev packages (L99–101).
- UnleashedRecomp ships Linux as a Flatpak
  (`flatpak/io.github.hedge_dev.unleashedrecomp.json`, release asset
  `UnleashedRecomp-Flatpak.zip`). On the Deck it is added as a non-Steam game;
  it has no file picker in Game Mode (README "Steam Deck Support").
- Proton uses its own Vulkan loader, and `PROTON_LOG=1` writes a log (Proton
  README L317–329). **Unverified**: whether Windows-side layers work under
  Wine.
- Steam Deck: 8 RDNA 2 CUs at 1.6 GHz, 16 GB LPDDR5
  (https://www.steamdeck.com/en/tech/deck), RADV driver. **Unverified**:
  whether `VK_EXT_fragment_shader_interlock` is exposed (needed by Classic's
  FSI path). Native does not need it.

How we'd use it here:

- Enforce ADR-0006 now: code review flags Win32 outside platform layers. The
  pwsh scripts run on Linux.
- At M6: build in the rexglue 22.04 container, package a Flatpak following
  UnleashedRecomp, and run the scene suite on Linux and the Deck against the
  Windows goldens.

Effort/payoff: a week or more at M6; required for v1.0.

## 8. Agent workflow: Matt Pocock skills

Findings (plugin at
`C:\Users\Martin\.claude\plugins\cache\claude-plugins-official\mattpocock-skills\1.2.3\skills\engineering\`):

- **Main flow** (`ask-matt/SKILL.md` L13–30): `/grill-with-docs`, optional
  `/prototype`, then for a multi-session build `/to-spec` → `/to-tickets` →
  `/implement` per ticket, clearing context between tickets. Grill, spec and
  tickets stay in one context window (L28–30). `/implement` drives `/tdd`,
  ends with `/code-review` and commits to the current branch
  (`implement/SKILL.md` L9–15).
- **On-ramps**: `/diagnosing-bugs` (L42) refuses to hypothesise without a
  tight, red-capable loop (`diagnosing-bugs/SKILL.md` L18–60, "Differential
  loop" L34). `/wayfinder` (L44–46) is for huge, foggy efforts; it produces
  decisions, not deliverables, then hands off to `/to-spec`.
- **`/wayfinder`** (`wayfinder/SKILL.md` L19–80): a map plus child decision
  tickets typed research, prototype, grilling or task. Charting fires
  `/research` subagents; one ticket per session, research excepted.
- **`/setup-matt-pocock-skills`** (`setup-matt-pocock-skills/SKILL.md`): asks
  for the tracker (GitHub, GitLab, local markdown, other; L38–49), triage
  labels only if `triage` is installed (L51–57), and the domain-doc layout,
  default single-context `CONTEXT.md` + `docs/adr/` (L59). Writes
  `docs/agents/issue-tracker.md`, `domain.md` and an `## Agent skills` block
  in `CLAUDE.md` (L63–112).
- **Local tracker** (`issue-tracker-local.md` L1–30): one file per issue at
  `.scratch/<feature>/issues/NN-slug.md` with `Status:` and `Blocked by:`
  lines; a wayfinder map is `.scratch/<effort>/map.md`.
- **This repo** has no git remote, so choose **local markdown**. SVR2011 uses
  GitHub Issues (`D:\Projects\SVR2011-RECOMP\docs\agents\issue-tracker.md`);
  do not copy that. `/implement`'s commits stay local, matching the
  private-repo rule. `.scratch/` is not in `.gitignore`; decide whether
  tickets are committed or ignored.

How we'd use it here:

- Run `/setup-matt-pocock-skills` once (local markdown, single-context). Seed
  `CONTEXT.md` with terms already in use: Classic, Native, ring/IB draw,
  census, seed, scene, golden.
- Use `/wayfinder` only for M3 (texture formats, RT/resolve model, AA) and M6
  (Linux packaging). Its research tickets become `/research` subagents writing
  `docs/research/`.
- Use the scene test or `native_frame_check.py` as the `/diagnosing-bugs`
  loop; this is how the black screen was found. Use `/tdd` at the Catch2 seams
  in `tests/native/` (guest decode, texture layout, write watch).

Effort/payoff: 30 minutes; high payoff from continuity across sessions.

## Proposed workflow (M2 → M6)

Per milestone:

1. **Chart.** Foggy milestones (M3, M6): `/wayfinder` with the destination set
   to the ROADMAP exit criterion; research tickets go to `/research`
   subagents, hard-to-reverse decisions become ADRs. Scoped work:
   `/grill-with-docs`.
2. **Spec.** `/to-spec` → `/to-tickets` into `.scratch/<milestone>/`. Each
   ticket names its scenes; acceptance is the scene-test thresholds.
3. **Implement.** One `/implement` per ticket in a fresh context: `/tdd` on
   `tests/native` for pure code, `scene_test.py --renderer both` for renderer
   behaviour, then `/code-review` and a local commit.
4. **When red.** `/diagnosing-bugs`, escalating the loop: (1) scene test with
   FLIP heatmap; (2) the same with validation and sync validation; (3)
   RenderDoc captures of both renderers diffed with `rdc_export.py`; (4) a
   Canary `.xtr` of the scene; (5) Ghidra MCP on the guest code behind the
   draw.
5. **Nightly.** The full scene suite on both renderers, plus a perf gate
   ported from SVR2011's `benchmark_gate.py`.

Milestone notes:

- **M2**: tools first (input replay, scene runner with FLIP, RenderDoc capture
  flag and labels, validation switch). Then scenes: title, every menu page,
  char select, one Classic exhibition match driven by replay.
- **M3**: cherry-pick the rexglue codegen fixes, build the Canary trace tools,
  add match scenes (entrance, gameplay, finisher, results), and run resolution
  scale and MSAA as scene parameters.
- **M4**: BSim naming for hook targets; scenes per test-matrix mode × 7
  languages; an entrance audio-loop scene; 4-pad replay.
- **M5**: launcher shader pre-conversion follows the XenosRecomp cache layout;
  Native becomes the default once the whole suite passes on it.
- **M6**: 22.04 container build and Flatpak; scene suite on Linux and the Deck
  against the Windows goldens.

## Open questions

- Whether skate3-recomp's `trace_writer` is a faithful Xenia port that fits our
  pin (only file names read).
- Whether `qrenderdoc --python` runs headless on Windows, and whether
  `renderdoccmd convert` emits `zip.xml`.
- Whether Lavapipe or SwiftShader can run Classic (FSI/EDRAM) or Native. This
  only matters if CI ever leaves this PC.
- Whether RADV on the Deck exposes fragment shader interlock.
- Whether the Fable Anniversary PDB build contains Havok, FMOD or Scaleform
  versions close to ours.
- How deterministic input replay is with threaded guest code and Havok
  (untested).
- What `writable_code_segments` (rexglue `5e2dae1d`) is for (only the commit
  title read).
- CLAUDE.md says `xenia-canary/` holds the reference emulator. It is
  `.tools/xenia-canary/`, binary only, with no source.
