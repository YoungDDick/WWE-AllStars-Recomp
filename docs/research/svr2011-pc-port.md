# wwe-svr2011-pc (KaikoClanworth1) as a model for this port

Date: 2026-10-03. Repo: https://github.com/KaikoClanworth1/wwe-svr2011-pc. It is
a PC, Steam Deck and Android port of WWE SmackDown vs. Raw 2011 on ReXGlue.
Read at `de85161` (v2.0.0, 2026-10-02) from a full clone, plus `gh` for
releases and issues. Paths below are relative to that repo unless prefixed
with `SVR2011-Recomp:` (the user's repo at `D:\Projects\SVR2011-RECOMP`, HEAD
`6a12d52`). "NR" means `port/src/native/native_renderer.cpp`; "RXP" means
`port/patches/rexglue-sdk-svr2011.patch`, cited by line within the patch.
Items not checked against source are marked **unverified**.

## Summary

- **Same base as ours.** It uses the ReXGlue v0.10.0 runtime and codegen
  (SDK `c94f5ebd`, README L347–359). That commit is the "Release v0.10.0"
  commit on top of our `f5337cdc`, with 0 file changes (`gh api compare`). It
  adds one 5,177-line SDK patch, plume (MIT RHI) for D3D12 and Vulkan, and
  patched XenosRecomp.
- **Renderer is the same design as ours.** It hooks the statically linked
  XDK D3D9 functions, reads the device's Xenos register mirror at each draw,
  and turns the emulator's draws off through an external-frame patch. The
  difference is maturity: the native renderer has been the default since
  v0.2.1 (`d7b1099`), with automatic fallbacks, and it covers wide/tall
  screens, supersampling, a 60 Hz world clock, pipeline prebuild and shader
  packs.
- **Licence: permission granted.** The repo has no LICENSE file, but on
  2026-10-03 the author gave this project's owner full rights to reuse its
  code (see §1). Code may be ported, with attribution.
- **The game engine differs from ours.** SVR2011 is Yuke's (`.pac`/Yuke's LZ,
  Wwise, Havok, Bink, GameSpy). All Stars is THQ UE3 with FMOD, Scaleform,
  Bink and Quazal. The D3D/XDK layer, SDK patches, tools and launcher ideas
  transfer; game hooks (menus, frame rate, aspect, post effects) do not.
- **Strategy:** keep ADR-0002/0015 in shape. Change *when* Native becomes the
  default (runtime fallbacks instead of full parity first), and add a
  scripted-input test harness and pipeline prebuild now. See §6.

## 1. Architecture and licence

Base:

- The SDK runtime is the Xenia-derived ReXGlue v0.10.0 runtime. The game is
  recompiled with the prebuilt `rexglue.exe codegen` (README L374–383). The
  runtime is built from SDK source with `-DREXGLUE_USE_VULKAN=ON`
  (`port/build.ps1` L15–19). This is not XenonRecomp and not its own runtime.
- The manifest is 11 lines, `sdk_version = "0.10.0"`, and includes
  `svr2011_config.toml` (`port/svr2011_manifest.toml`). The config holds:
  - setjmp/longjmp addresses (L4–9);
  - 4 tail-call gap functions (L11–16);
  - about 70 pointer-only seeds, each with a "N ref(s), e.g. 0x…" comment,
    found by `port/tools/find_fptrs.py` (L18–89).

  Strict mode is not set explicitly (**unverified** what the v0.10.0 default
  does). This is the same "seed with evidence" discipline as ours.
- Every port-side game patch is a `REX_HOOK_RAW(sub_X)` override that calls
  `__imp__sub_X` (`port/src/native/native_hooks.cpp` L21–142,
  `port/src/menu_hooks.cpp` L127–128). No midasm hooks.

SDK patch, every line tagged `[svr2011]` (RXP). Areas:

| Area | Contents | Where |
|---|---|---|
| External frame | External frame plus `SetHostDrawingDisabled`. The command processor keeps running PM4 for fences, interrupts and memexport but skips draws and resolves | RXP L48–130, L742–791, L1528–1545, L1883–1900 |
| Frame rate | `guest_vblank_hz`, precise vblank, host present counters | RXP L1715–1793, L577–592, L4695–4740 |
| Timeline | µs event timeline (`--timeline_file`) | RXP L1135–1272 |
| Kernel objects | CreateNative address map: an enumerator was being taken for an XEvent, which killed audio | RXP L4297–4378 |
| Notifications | Notify listener must not stay signalled (a Wwise thread spun) | RXP L4261–4296 |
| POSIX waits | WaitMultiple on a condvar | RXP L974–1134 |
| Saves and content | Flat saves root, `.info` kept with saves, `dlc_full_license` | RXP L403–467, L3873–4260 |
| Music player | XMP `HostMusic` user playlists, `xmp_throttle` | RXP L173–230, L2098–2304 |
| Input | Input hold: idle pads while a host menu is open | RXP L1906–1952 |
| Online | Real sockets, VDP over UDP, GameSpy DNS to a relay, QoS, endian fixes | RXP L2527–3504, L4379–4526 |
| GPU | WAIT_REG_MEM stall stats; `WriteRegister` lookup table; D3D12 DRED; a 64 KB tail on the upload pool | RXP L1273–1389, L1308–1322, L4570–4694 |
| Android | arm64 fibers, SDL3 surface, adrenotools loader | RXP L792–959, L4766–4932, L5090–5177 |

Layout: `port/src` (game and app code, 69 files), `port/src/native` (renderer),
`port/launcher` (Win32 C), `port/android`, `port/server` (Python online
server), `port/patches`, `port/tools` (73 scripts). `.gitignore` is an
allow-list (`/*` then `!/port/` and so on).

Licence:

- **No LICENSE file**, and `gh repo view` reports `licenseInfo: null`.
  `docs/TERMS.md` L8 says the code is "under the license stated there", but
  nothing is stated. Under default copyright the port's own code would be
  all rights reserved.
- **Update 2026-10-03:** the author (KaikoClanworth1) told this project's
  owner directly that we have full rights to reuse the code. Port it with a
  credit to wwe-svr2011-pc in the file header and in `THIRD_PARTY` notes.
  Before the repo goes public (ADR-0014), get that permission in writing or
  have the author add a LICENSE file compatible with BSD-3, since the grant
  is not yet recorded anywhere public.
- Third-party parts:
  - plume is MIT (`gh api repos/renderbag/plume`); the port's plume patch is
    under the port's own (missing) licence.
  - XenosRecomp is MIT.
  - `port/launcher/bink_decode.c` L8–14 and `bink_tables.h` L2–8 are
    LGPL-2.1+ (an FFmpeg port). Avoid them under ADR-0007.
- Releases ship converted game shaders: `port/tools/package.ps1` L27–39
  copies DXIL and SPIR-V into `native_shaders`, and v2.0.0 ships shader packs
  (`docs/SHADER_PRECOMPILE_PLAN.md` L7–9). ADR-0012 deliberately avoids that.

## 2. Renderer

### Model

- It uses D3D-level hooks, not PM4 replay.
  - Hooked: DrawIndexedVertices `82921B58`, DrawVerticesUP `82921698`,
    EndVertices `82921688`, SetRenderTarget `8291E618`, Clear `8291F168`,
    Resolve `82918A88`, Create*Shader, SetStreamSource, Set*ShaderConstantF.
  - Also the constant flush `829251D8`, LOAD_ALU_CONSTANT `82925D78` and the
    camera projection `825818B8` (`port/src/native/guest_d3d.h` L130–145).
  - Present is hooked in `port/src/frame_stats.cpp` L166–176.
- Each hook runs the original first, then the native renderer
  (`native_hooks.cpp` L3–6, L23–31).
- State is read from the device register mirror at `0xAD977E80`
  (`guest_d3d.h` L16–127). The layout came from the XDK copy routines'
  (register, offset) pairs (`port/docs/native_renderer_phase2.md` L130–161).
  Our `src/native` does the same; the hook addresses are identical to
  SVR2011-Recomp's (`SVR2011-Recomp: src/d3d_census.cpp` L183–344).
- The D3D map was built with a census build: `tools/gen_d3d_trace.py`
  generates 562 counting hooks, and `tools/d3d_map.py` turns their output plus
  a names table into `port/docs/d3d_map.csv` (columns include calls per frame
  per scene, r3–r8 ranges, PM4 packets, imports, evidence).
  `native_renderer_phase1.md` L11–41 covers it. This is the same idea as our
  `gen_d3d_sweep.py` and `match_functions.py`.
- The emulator stays alive. Draws are skipped through
  `SetHostDrawingDisabled`, and the native frame is shown at swap (RXP
  L1450–1500, NR L3047–3058). This is the same as our
  `rexglue-native-frame.patch`.
- Modes: `native_renderer=off|main|shadow` (NR L109–114). It can be switched
  live from the in-game GRAPHICS page (NR L2988–2993).

### Fallbacks

These are why Native could become the default early:

- **Stuck GPU:** a fence wait longer than 3 s logs DRED, re-enables host
  drawing and CPU-signals the fences (NR L144, L1006–1026, L702–708; `a4e455a`).
  It can be simulated with `SVR2011_NATIVE_TEST_STALL` (NR L3224–3239).
- **Missing shader pack:** the whole renderer falls back to emulation
  (NR L983–994; `701d5e3`).
- **Missing single shader:** that draw is skipped and counted (NR L417–427,
  L2537–2542).
- **Missing texture:** a 1x1 transparent placeholder (NR L794–831).

### API layer

- plume RHI with D3D12 and Vulkan backends (`port/src/native/gpu.h` L1–90).
  The backend follows the emulator's API (`gpu.cpp` L19–27).
- Vulkan adopts the emulator's VkDevice and queue (`backend_vulkan.cpp` L31–59),
  and D3D12 matches the adapter by LUID (`backend_d3d12.cpp` L79–94).
  `4864587` fixed hybrid laptops where each renderer used a different GPU.
- The plume patch adds stencil reference and blend factor, the D3D12 pipeline
  library, VkPipelineCache, adoption of an existing device, and typeless sRGB
  views (`port/patches/plume-svr2011.patch` L13–208, L474–510, L580–898).
- `RecordingList` records command lists on a separate thread (1 MB chunks),
  on by default only on Android (`recording_list.h` L1–115, NR L77–87;
  `75e585a`).

### Shaders

- Captured at Create*Shader (SVR2011 shaders sit in Yuke's-LZ `.pac` files;
  `native_renderer_phase1.md` L47–58). 467 unique shaders.
- `tools/convert_shaders.py` (L1–60): XenosRecomp → HLSL → DXC → DXIL and
  SPIR-V (`-fvk-invert-y`, `-fvk-use-dx-layout`, vulkan1.2).
  - Variants are compiled with `-DSVR_SPEC_CONSTANTS`: `.s1` for 11:11:10
    normals, `.s2` for alpha test.
  - Each shader gets `.inputs` and `.textures` sidecar files.
- XenosRecomp fixes (`port/patches/xenosrecomp-svr2011.patch`, built by
  `tools/patch_xenosrecomp.py`). All are **generic Xenos bugs**:
  - `setp_inv` (patch L313–319; `6e4de40`, wrong attire colours);
  - flow control as `if (pc <= N)` instead of switch fall-through: 49 of 118
    VS crashed RADV on the Deck (patch L622–625; `1a4d023`);
  - co-issue order and deferred `a0` (L154, L304);
  - conditional exec honouring bool/predicate (L653, L674);
  - pixel shaders testing their own booleans (L552);
  - 32 fetch slots through `g_ResourceIndex` (L28–66, L516–600);
  - vertex texture fetch via `SampleLevel`;
  - COLOR2–7 interpolators;
  - 11:11:10 SNORM normals (L124).
- Shader packs `shaders.{dxil,spv}.pak` (`SVRPAK1`) replace about 2,400 loose
  files (HDD seeks) (`tools/pack_shaders.py`; NR L315–385; `e932183`).

### Pipelines

- The key is VS/PS hash and variant, topology, RT format, depth/blend/mask,
  cull, stencil, layout and bias (NR L1735–1833).
- Every new pipeline is appended to `UserData/cache/native_pipelines.list`
  (NR L2052–2071). The shipped `port/dist/pipelines.list` has 298 entries
  from a 25-leaf menu crawl (`3c93396`).
- A low-priority thread builds them while in menus and pauses in matches
  (NR L2084–2190). A "Preparing graphics n/N" bar is shown
  (`port/src/fps_overlay.cpp` L26–49; `a236628`).
- The driver cache is saved 3 s after the last new pipeline (NR L1252–1258,
  L759–769; `b42cf33`, `d26b3f7`). `tools/merge_pipelines.py` merges player
  lists.

### Render targets, resolves and EDRAM

- EDRAM is not emulated. There is one host RT per (EDRAM base, pitch, format,
  height); depth is D24S8 (NR L477–504, L1326–1414). Height comes from the
  surface object +0x24 seen at SetRenderTarget (NR L3629–3644).
- A resolve copies to a host texture keyed by destination address, and later
  fetches of that address sample it (NR L3496–3624). It assumes full-surface
  copies from (0,0) "in this game" (NR L3494).
- Write-back to guest memory (read back, untile, endian swap, downscale) is
  **only** done when a size signature says the CPU reads the result:
  Superstar Threads bake, CAW portrait, highlight cover (NR L3392–3491,
  L3426–3431).
  - History: `a8bc487` added it, `5980ba6` limited it to power-of-two
    targets, `2188b98` narrowed it to the signatures (entrances had dropped to
    single-digit fps from 500+ GPU waits), `63a8c5a` added portraits.

### Textures

- Fetch-constant decode; the SDK's own `texture_util` tiling is compiled in
  (`port/src/native/textures.cpp` L560–600; CMakeLists L66–69).
- CPU conversion for 565, 1555, 4444, DXT3A and CTX1 (L61–129, L276–330).
- Change detection is a periodic re-hash, not a write watch: every 120
  frames, every 4 frames for a texture's first 240 frames, with a back-off
  for dynamic textures (L34–44, L740–764; `0be5b27`). Our `write_watch` is
  more precise.

### Vertices and constants

- Byte swap with a 16-bit half-swap taken from the declaration (NR L1558–1606).
- Static vertex buffers are cached; one that changes 3 times moves to a 64 MB
  per-frame ring (NR L1640–1733). Indices are 16-bit only (NR L1608–1638).
  Quads become triangle lists.
- Constants are re-uploaded only when the flush reports dirty blocks
  (NR L2212–2294, L3652–3711).

### Resolution, AA and screen shape

- Resolution is an integer scale that covers the window, capped by
  `native_max_scale`, default 4 (NR L1081–1152).
- "AA" (`native_2x_msaa`) is really 2x2 supersampling, averaged at present
  (NR L1102, L1113–1117). There is no MSAA.
- `native_scale_effects=false` keeps targets below 1152x640 at guest size
  (NR L73–75, L638–647).
- **Wide screens:**
  - targets ≥1152x640 are widened (NR L649–692);
  - the 16:9 aspect constant at `0x82007724` is rewritten (NR L667,
    L3035–3039), so the projection and culling widen;
  - the HUD is detected heuristically (no depth test, positions flat in
    target pixels) and drawn into the centred 16:9 area (NR L2477–2523,
    L2719–2738).
  - Only during matches (`menu_hooks.cpp` L215–280; `284fd69`).
- **Tall screens** widen the vertical FOV in the projection hook
  (`native_hooks.cpp` L144–172; `56e831d`).
- **Post-effect toggles** (DOF, motion blur, soft filter) wrap the game's
  effect functions and zero fields of its settings struct at `0x82EDE250`
  (`port/src/post_effects.cpp` L1–96; `c76de77`).

### Frame pacing and 60 fps

All of it is engine-specific:

- The timing block `0x82EDDBE8` is pinned to 60 (`port/src/frame_rate.cpp`
  L3–10, L118–130).
- The Yuke's task manager `sub_8269C728` is reimplemented: world update per
  60 Hz tick of real time (up to 4 per frame), draw once per frame.
  - The CHPH physics step is repeated per tick.
  - Input repeats the last reading during the extra updates.
  - (L12–25, L151–410; `9e529d3`, `0b15618`, `224faa9`.)
- The SDK `guest_vblank_hz` (RXP L1736–1760) drives 30 or 60.
- Entrances run at 60 by never applying the game's half-rate
  (`menu_hooks.cpp` L59–68).

### Performance

- **GPU-wait yield:** the XDK poll `sub_82919F18` (about 430k calls per frame)
  yields, then sleeps 100 µs. Render thread went from 98% to 51% of a core
  (`port/src/perf_hooks.cpp` L1–97; `d26b3f7`). Our counterpart is
  `0x82DB6710`, about 885k calls per present (`native-renderer.md`
  "Attribution sweep").
- Register writes go through a lookup table (RXP L1308–1322).
- Heavy entrances: dirty-block constant compares, redundant-bind elimination,
  SIMD swaps (NR L162–200, L570–585, L2918–2961; `75e585a`). One entrance
  went from 12 ms to 6.3 ms per frame.
- `timeBeginPeriod(1)` alone removed a 39 fps cap
  (`port/src/svr2011_app.cpp` L127).
- `gpu_allow_invalid_fetch_constants=true` is forced, otherwise entrances
  render black (L274–283). SVR2011-Recomp's launcher passes it too.

### Compared with ours

| Topic | wwe-svr2011-pc | WWE All Stars (now) |
|---|---|---|
| Hook layer | D3D hooks plus device mirror | Same (ADR-0002); hybrid PM4 for the gap (ADR-0015) |
| API | plume: D3D12 and Vulkan | Hand-written Vulkan on the SDK device (ADR-0003) |
| Shaders | Captured at Create*, DXIL/SPIR-V packs shipped in the release | Captured at XDK register functions (UE3 builds shaders in place), XenosRecomp, launcher converts (ADR-0012) |
| RT/EDRAM | Host RT per surface; resolves to host textures; write-back by size signature | Host RT keyed on host format (`native-renderer.md`); resolves not yet built |
| Texture invalidation | Periodic re-hash | Page write watch |
| AA | 2x2 SSAA | MSAA planned (M3) |
| Default | Native since v0.2.1, three automatic fallbacks | Classic until full parity (ADR-0002) |
| Verification | Screenshots and log greps | `native_frame_check.py` diff, Catch2 `tests/native` |

## 3. Everything else that makes it "better"

- **Audio:**
  - A port-side XAudio2 backend: 256×6 float per frame, 5.1 or stereo fold,
    selected by the `audio_backend` cvar with SDL fallback
    (`port/src/xaudio2_audio.h` L1–7, `svr2011_app.cpp` L229–245).
  - XMA stays in the SDK.
  - Fixes that came from audio bugs are kernel-level (RXP L4261–4378).
  - USER PLAYLIST via XMP `HostMusic` with Media Foundation decoding
    (`port/src/music.cpp` L180–197, L535–575).
- **Video:**
  - Bink stays guest code.
  - Custom titantron movies are served from a host device (`umovie:`) by
    hooking the movie list and path (`port/src/user_movies.cpp` L190–274).
  - The launcher has its own Bink 1 encoder (`port/launcher/bink_encode.c`)
    and an LGPL decoder.
- **Input:**
  - XInput on PC, SDL on Android (`svr2011_app.cpp` L95, L190).
  - Keyboard as controller with the SDK's `mnk_mode` (`6f64494`).
  - PC typing into the game's on-screen keyboard: the game already polls
    `XInputGetKeystroke` (`port/src/keyboard_typing.h` L1–10).
  - A touch controller with an editor (`touch_controls.h`).
  - **No glyph swapping.**
  - The SDK input hold idles pads under host menus (RXP L1906–1952).
- **Saves:**
  - Flat `Saves/` files via the SDK flat-saves patch (`port/src/saves.h`
    L1–7).
  - A utility drive (`cache:`) with real free space. Without it, highlight
    files were never written (`svr2011_app.cpp` L67–87, L293–310; `97e41c6`).
  - The launcher can back up, restore, import and export saves.
- **DLC:**
  - STFS packages (CON/LIVE/PIRS) are unpacked into the content tree at
    startup, title updates skipped (`port/src/dlc.cpp` L55, `dlc.h` L1–7).
  - `dlc_full_license` treats DLC as fully licensed (`6900f63`). This matches
    ADR-0010's needs.
- **Achievements:** an ImGui page over the SDK `AchievementManager`
  (`port/src/achievements_page.cpp` L25, L165; `96929ca`).
- **Online, beyond a stub:**
  - GameSpy Sake, Auth and Competition re-implemented in `port/server/gamespy.py`.
  - P2P XSession over UDP, LAN plus relay (`port/src/p2p.h` L1–18;
    `b93b434`, `61c6f10`).
  - Lockstep (`0a0c585`).
  - All Stars uses Quazal, so none of this applies directly (ADR-0008).
- **Menus:**
  - New `menu.pac` records cloned by `port/tools/patch_menu.py`, plus a
    string-lookup hook (`menu_hooks.cpp` L39–120).
  - ImGui pages (graphics, achievements, managers).
  - Language through the SDK `user_language` (RXP L2458–2514).
- **Launcher:**
  - 5,204 lines of Win32 C (`port/launcher/svr2011_launcher.c`).
  - Tabs: Play, Settings, Online, Install (direct XDVDFS read, L336–473), DLC,
    Saves, Paint Tool, Movies, Android.
  - Self-update from GitHub releases with its own unzip, because `tar.exe`
    broke under Proton (`port/launcher/updater.c` L161–180; README L153).
  - A CLI test mode: `--capture <tab> file.bmp`, `--install` (L1–30).
- **Platform:**
  - Optimus and PowerXpress exports (`port/src/main.cpp`).
  - Wine and Deck detection (`platform.h`); the Deck goes fullscreen.
  - Linux and Deck run only through Proton (README L134–153).
  - Android is a native arm64 build (`port/tools/build_apk.py`, Turnip via
    adrenotools in `gpu_driver.cpp`).
- **Crash handling:** minidump plus a symbolized text report in
  `UserData\crashes` (`port/src/crash_report.cpp`). PDBs come from
  `-gline-tables-only` (CMakeLists L157–165).
- **Mods and content:** user music and movies, 10 HD logos, 10 Paint Tool
  pages, unused match types (`docs/MATCH_TYPES_RESEARCH.md`), managers,
  mixed-gender matches.
- **Other:** Discord rich presence over raw IPC (`discord_presence.cpp`).
- **Config:** `svr2011.toml` next to the exe; the same settings in the
  launcher and in game (README L198–222).

## 4. Workflow and tooling

- **Timeline.** 125 commits, all "Co-Authored-By: Claude". History starts at
  the squashed `4ea739a` (2026-09-28 16:17, 23,581 lines); pre-release history
  is not public. Releases (`gh release list`):
  - v0.1.0: 09-28
  - v0.2.0, v0.2.1, v0.2.2, v0.3.0: 09-29 (native default, Deck, Vulkan
    backend, Android)
  - v0.3.1, v0.3.2, v1.0.0–v1.0.3: 09-30
  - v2.0.0: 10-02 (frame rate, online, managers)

  That is about 4 days from first public source to 2.0. The manifest header
  says codegen first ran on 2026-09-25 (`port/svr2011_manifest.toml` L1).
- **Unattended runs.**
  - `port/tools/session.ps1` and `run_test.ps1` launch with `--log_file
    --audio_mute=true`. The window is parked off-screen without focus and
    captured with `PrintWindow`.
  - Input comes from `SVR2011_INPUT_FILE`, a text script appended to live:
    `press`, `hold`, `stick`, `trigger`, `wait`, `type`, `key`, `touch`
    (`port/src/script_input.h` L1–19; `svr2011_app.cpp` L207–215).
  - `test_guard.ps1` never kills a player's game and uses test saves.
- **Verification** is screenshots plus log greps (`crawl_menus.ps1` L149–163:
  access violations, "menu select", "fps: N avg"). There are no image asserts.
  `crawl_menus.ps1` walks every `menu.pac` leaf with several workers; the
  crawl also fed `pipelines.list`.
- **Finding functions:**
  - `find_fptrs.py` (data pointers and `lis+addi` pairs in a dumped image);
  - `callgraph.py` (over the generated C++);
  - the D3D census build;
  - `dis.sh` (objdump);
  - `port/tools/sampler/sampler.c` (out-of-process 1 ms StackWalk64 profiler)
    with `prof_funcs.py` and `prof_lines.py`;
  - the SDK `--timeline_file`.
- **Renderer debugging:**
  - env-var probes: per-draw dump `SVR2011_NATIVE_DUMP_FRAME` (NR L2811–2893),
    solid/no-depth/no-blend, PS override, texture-slot bisection;
  - an emulator-side draw log `SVR2011_GPU_DRAWLOG` (RXP ~L1408) to diff
    against native;
  - `catch_white.ps1` dumps the frame when it goes white.
- **No CI** (no `.github/`). Two issues, both closed within hours (#1 shader
  path, #2 hybrid laptop GPU).

## 5. Diff against SVR2011-Recomp (the user's repo)

Same game and SDK tree. SVR2011-Recomp pins `f5337cdc`
(`SVR2011-Recomp: scripts/prepare-vulkan.ps1` L11); wwe-svr2011-pc pins
`c94f5ebd`, the same tree. Both use the same D3D hook addresses.

wwe-svr2011-pc has, SVR2011-Recomp lacks:

- Native as the default, with stuck-GPU, missing-pack and per-draw fallbacks.
  SVR2011-Recomp's Native is an opt-in beta with Classic as the default
  (`SVR2011-Recomp: src/native/native_renderer.cpp` L31,
  `launcher/portable/Settings.cs` L23).
- A D3D12 backend that works (via plume) beside Vulkan. SVR2011-Recomp hit
  DEVICE_HUNG on the emulated D3D12 path and went Vulkan-only.
- Up to 4x scale with SSAA (SVR2011-Recomp: 1–3x, no AA, `pipelines.cpp`
  L237), wide/tall screens, effect toggles, effects kept unscaled.
- A real 60 Hz world clock (30/60 option, 60 fps entrances). SVR2011-Recomp
  paces to vblank with entrances at 30 (`performance.md` L607–640).
- A pipeline list plus prebuild in menus with a progress bar; shader packs.
- Resolve write-back only where the CPU reads it. SVR2011-Recomp writes back
  more widely (`93b98ca`, `7597e71`) and has burst readback for Classic.
- XAudio2 5.1, the input hold, PC typing, a touch controller.
- Flat saves, a utility drive, DLC install and licence, achievements page,
  languages, Discord, a crash reporter.
- Online (GameSpy server, P2P), menu injection, gameplay features.
- Launcher self-update, Proton/Deck support, Android.
- The scripted-input test harness, the menu crawler, an out-of-process
  sampler.

SVR2011-Recomp has, wwe-svr2011-pc lacks:

- A BSD-3 licence (`SVR2011-Recomp: LICENSE`).
- A hash-pinned toolchain (`bootstrap.ps1` L9–11) and an XEX SHA-256 gate.
- Catch2 unit tests for decode, texture layout and write watch, plus Python
  and PowerShell tests.
- A page write watch for textures and buffers.
- A perf acceptance gate (`benchmark_gate.py`: 59.9 fps, p99 ≤ 17.5 ms) and
  Tracy.
- FSR1/CAS present.
- The Classic FSI render-target path and the burst readback mode.
- A disc manifest with SHA-256 verified extraction.
- No game-derived shaders in the repo. Its release binary still embeds them
  (`SVR2011-Recomp: CMakeLists.txt` L23–36).

## 6. Transfer to WWE All Stars

### Engine differences

- SVR2011 is Yuke's: `.pac`, Wwise (RXP L4276), Havok, Bink 1, GameSpy.
- All Stars is THQ UE3 with Havok 4.5.1, FMOD Ex, Scaleform GFx 2.x, Bink 1
  and Quazal (`docs/ROADMAP.md` "Target").

So everything at the XDK and SDK level transfers, and every game-level hook
must be found again. All Stars also differs in ways that matter:

- **Shaders.** UE3 builds shader objects in place (3+3 Create*Shader calls),
  so capture must stay at the XDK register functions (`native-renderer.md`
  "Native bring-up, step 1").
- **Vertex layout.** No vertex declaration is bound at UE3 draws, so layouts
  come from the live vfetch microcode, not from SVR2011's declaration path.
- **Draw coverage.** About 50 draws per present in a match bypass the hooked
  entry points (`native-renderer.md` "Correction"). SVR2011 needs none of
  ADR-0015's PM4 part.
- **Menus are Scaleform.** There is no `menu.pac`, so menu injection must hook
  GFx or UE3 instead.

### Ranked adoption list

Code may be ported with attribution (licence granted 2026-10-03, see §1).

| # | What | Effort | Payoff | ADR impact |
|---|---|---|---|---|
| 1 | **Scripted input file driver** (`script_input.h` grammar, appended live) plus an off-screen, no-focus window and `PrintWindow` capture. Replaces the keyboard scripts (done in ticket 11); is the input half of `tooling.md` §4 | 1–2 days | Very high: unattended M2–M4 scenes | None |
| 2 | **Runtime fallbacks so Native can be the default before full parity**: fence timeout → re-enable host drawing and CPU-signal; missing shader → skip draw and count; missing pack → Classic; placeholder textures | 1–2 days | High: earlier dogfooding, safer default | **ADR-0002**: amend "Native default only at full parity" to "default once the scene suite passes, with automatic per-draw and whole-renderer fallback to Classic" |
| 3 | **Pipeline list plus menu-time prebuild plus VkPipelineCache persistence**, list grown by a menu crawl and merged from sessions; shader blobs in one pack file | 2 days | High: removes first-use hitches (M3/M5 frame-time goal) | Refines ADR-0012: the launcher converts shaders and writes the pack. Do not ship game shaders as they do |
| 4 | **XenosRecomp fixes**: setp_inv, flow control without fall-through (Deck RADV), co-issue order, conditional exec, PS booleans, 32 slots, vertex texture fetch | 1 day to re-derive and test against our 5 quarantined shaders | High: correctness, and the Deck crash avoided before M6 | None (our patch, MIT upstream) |
| 5 | **GPU-wait yield hook** on our XDK poll `0x82DB6710` (about 885k calls per present) | Hours | High: frees a core (98→51% in SVR2011) | None |
| 6 | **Resolve policy**: host-texture resolves by destination address; CPU write-back only for consumers proven by evidence (CAS portraits, save thumbnails) | Part of M3 | High: avoids the single-digit-fps trap they hit (`2188b98`) | Fits ADR-0015; note in the M3 design |
| 7 | **SDK patches by area**: input hold, flat saves and `.info`, `dlc_full_license`, utility drive, `guest_vblank_hz`, upload-pool tail, `WriteRegister` table, xobject address map, `timeBeginPeriod(1)`, Optimus/PowerXpress exports | 2–3 days total, each re-implemented and tested | Medium–high (M4 saves/DLC, perf) | ADR-0010 gets a concrete licence path; ADR-0005 unchanged (same SDK tree) |
| 8 | **2x2 SSAA as the first AA mode**, plus "keep sub-720p effect targets unscaled" | 1 day | Medium: AA that works with any deferred or resolve chain; MSAA can follow | ROADMAP M3 "MSAA/AA": SSAA first |
| 9 | **Crash reporter** (minidump plus symbolized text) and PDBs via `-gline-tables-only` | 1 day | Medium | None |
| 10 | **Out-of-process sampler** plus `find_fptrs.py`-style pointer seeding | 1 day | Medium for M1-style seed discovery (we have `find_table_targets.py`) | None |
| 11 | **Launcher ideas**: self-update from GitHub releases with its own unzip (Proton-safe), a CLI capture mode for launcher tests, save backup/import | M5 | Medium | ADR-0014 unaffected |
| 12 | **60 Hz world clock** (update per tick, draw per frame) | Research first: the UE3 tick and Havok step are unknown | Post-v1 (ROADMAP "60 fps") | None now |

Not worth adopting:

- D3D12 via plume. ADR-0003 holds: the D3D12 problem was the emulated path,
  but one API is enough and Native already shares the SDK Vulkan device.
- Periodic texture re-hash in place of our write watch.
- Their online stack (Quazal, not GameSpy).
- `menu.pac` patching.
- Wide-screen hacks (ROADMAP: out of scope).

Renderer strategy:

- **Keep the D3D-hook renderer and the hybrid plan.** Their result shows
  hook-level native rendering can reach a shipped default in days, which
  supports ADR-0002/0015.
- Try more D3D-level hooks for the remaining ~50 match draws before building
  PM4 translation, as SVR2011 never needed any (ADR-0015 already says the
  translated part is small).
- **Change the timing of the Native default** (row 2).
- Consider plume only if a second API is ever wanted. It is MIT, so it is
  compatible with ADR-0007.

## 7. Open questions

- Licence: resolved 2026-10-03. The author granted reuse (§1).
- Whether the v0.10.0 codegen runs in strict mode by default with their
  11-line manifest.
- Whether All Stars' movies are Bink 1 `BIK` in every case (string evidence
  only, `tooling.md` §5), relevant if we ever add custom movies.
- Whether All Stars polls `XInputGetKeystroke` (needed for the PC typing
  approach) or uses Scaleform text input only.
- Whether UE3 or Scaleform in All Stars read resolves back on the CPU (what
  needs write-back), and where.
- Whether their 2x2 SSAA cost fits the Deck at 720p in All Stars' heavier UE3
  scenes. They report 55–60 fps on the Deck with Native (README L136), but
  for a lighter Yuke's frame.
- Whether the plume adoption of an existing device works on RADV/Deck
  natively. They only run under Proton/vkd3d and Windows.
- Pre-release history (before `4ea739a`) is not public, so how they got from
  boot to a native renderer is not visible.
