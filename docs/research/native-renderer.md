# Native renderer research

## XDK D3D library location (2026-10-02, base XEX v5)

`scripts/scan_pm4.py generated/default` finds 52 guest functions that build
PM4 type-3 packet headers (raw output: `analysis/pm4-scan.txt`). 48 of them sit
in one block:

- **0x82DA8FB8 – 0x82DCB708**: the statically linked XDK D3D9 library
  (XDK 2.0.11775.3). This range is the counterpart of SVR2011's
  `0x82914800–0x82933240`.
- Draw packet builders (`DRAW_INDX` / `DRAW_INDX_2`): 0x82DAAF60, 0x82DAE6C0,
  0x82DAEBC8, 0x82DAF1A8, 0x82DAF5C0, 0x82DB3678, 0x82DB37D0, 0x82DB6D88,
  0x82DB9A10, 0x82DC54E8, 0x82DCB708.
- `ME_INIT` (device creation): 0x82DB6238.
- `INDIRECT_BUFFER`: 0x82DB5240.

Builders outside the block, to be identified: 0x82E99898 and 0x82E9A6B8
(`LOAD_ALU_CONSTANT`), 0x82F73D40 (`WAIT_FOR_IDLE`), 0x832245B0 and 0x83224BC8
(`LOAD_ALU_CONSTANT`), 0x83303C50 and 0x83303E10 (`IM_LOAD`). These may be UE3
or Scaleform fast paths that write packets directly.

Next steps (M2):

1. Pin down the exact library bounds.
2. Match the functions to D3D API names using SVR2011's hook list (Present,
   SetRenderTarget, Clear, Resolve, DrawIndexedVertices, BeginVertices,
   SetStreamSource, Create*Shader) by comparing code structure. Both games
   link nearly the same XDK.
3. Run the draw census to confirm that the D3D call counts match the PM4 draw
   counts.

## D3D hook map (2026-10-02)

`scripts/match_functions.py` matched SVR2011's D3D library
(0x82914800–0x82933240) against this image using masked signatures:
349 of 563 functions match uniquely (`analysis/function-matches.tsv`).
Every function SVR2011's native renderer and draw census hook has a unique
counterpart. The exception is EndVertices, where the 16-byte signature is
ambiguous but the bytes match exactly at the address next to BeginVertices.

| D3D function | SVR2011 | WWE All Stars | Size |
|---|---|---|---|
| SetTexture | 82917EC8 | 82DA7858 | 0x190 |
| SetVertexDeclaration | 82920F78 | 82DA9A70 | 0x18 |
| CreatePixelShader | 82921360 | 82DAB120 | 0xF0 |
| CreateVertexShader | 82921548 | 82DAB348 | 0xF0 |
| EndVertices | 82921688 | 82DAE6B0 | 0x10 |
| BeginVertices | 82921698 | 82DAE6C0 | 0x4C0 |
| DrawIndexedVertices | 82921B58 | 82DAF5C0 | 0x478 |
| SetStreamSource | 8291DD70 | 82DB2138 | 0x120 |
| SetIndices | 8291DE90 | 82DB22E0 | 0x90 |
| SetRenderTarget | 8291E618 | 82DB2AA8 | 0x350 |
| IndirectBuffer | 82922C58 | 82DB5240 | 0x3C8 |
| Clear | 8291F168 | 82DB9F30 | 0x5A0 |
| ClearRect draw | 8291EC48 | 82DB9A10 | 0x2B8 |
| Resolve | 82918A88 | 82DBC680 | 0x1150 |
| Register-mirror commit | 82924E40 | 82DBF0B0 | 0xC0 |
| Draw 14F68 | 82914F68 | 82DB3678 | 0x158 |
| Draw 150C0 | 829150C0 | 82DB37D0 | 0x388 |
| Draw 1A468 | 8291A468 | 82DB6D88 | 0x178 |
| Draw 21230 | 82921230 | 82DAAF60 | 0x130 |
| Draw 29158 | 82929158 | 82DC54E8 | 0x58 |
| Draw 2F338 | 8292F338 | 82DCB708 | 0x8C0 |

## Draw coverage: the D3D hooks see only ~20% of draws (2026-10-02)

The census was run at the main menu: `--wwe_d3d_census --gpu_draw_census`,
2,880 presents, about 59 presents/s. Per present:

| Source | Count |
|---|---|
| GPU `DRAW_INDX` | 11.7 |
| GPU `DRAW_INDX_2` | 69.8 (point lists alone: 48) |
| GPU `INDIRECT_BUFFER` | 7.9 |
| Hooked DrawIndexedVertices | 3.6 |
| Hooked BeginVertices (UP) | 2.3 |
| Hooked ClearRect | 9.8 |
| Hooked IndirectBuffer (`0x82DB5240`) | 2.0 |
| CreateVertexShader / CreatePixelShader | 3 / 3 in total |

SVR2011, for comparison, sends about 380 indexed draws per present through
DrawIndexedVertices.

### Attribution sweep

A sweep build (`-DWWE_D3D_SWEEP=ON`, `scripts/gen_d3d_sweep.py`) hooks all
691 other functions in 0x82DA5000–0x82DCC200. Each hook reads the device's
ring write cursor (device+0x30) before and after the call and counts the
DRAW headers written in between. Only about 14 draws per present are written
by D3D library calls into the device ring (`0x82DB9CC8` 9.8; `0x82DB5108`,
`0x82DBA4D0`, `0x82DBAB50`, `0x82DBAC78` and `0x82DBD7D0` about 1 each).

Busy non-draw functions:

| Function | Calls per present | What it is |
|---|---|---|
| `0x82DB6710` | about 885k | Spin/poll; the GPU-wait loop. Don't hook it in shipping builds. |
| `0x82DAFA38` | 81.5 | Shared dirty-register flush (the PM4 SET_CONSTANT builder) |
| `0x82DB1FF0` | 65.8 | SetScissorRect (PA_SC_WINDOW_SCISSOR at device+0x28C4/0x28C8) |
| `0x82DB2710`, `0x82DB2978` | 65.7 | Called once per scissor; to identify |
| `0x82DB49E8`, `0x82DB45B8`, `0x82DB4C18` | about 39 | To identify |

### Where the other ~67 draws per present come from

The GPU census lists the indirect buffers holding the draws:

- Every point-list draw (48 per present) comes from IB `0x1FC96980`.
- The rect-list draws (prim 8) are spread over dozens of IBs in
  0x1F990000–0x1FC96980. Each runs about 46 times per 60 swaps.
- These draws reach the GPU through indirect buffers that the hooked D3D
  calls never write into the device ring: precompiled or recorded command
  buffers (D3D `BeginCommandBuffer` / `RunCommandBuffer`), or packets written
  directly by UE3 or Scaleform code.

### Consequence for the native renderer

Porting SVR2011's immediate-mode hooks is not enough for this game. Options
(a decision for the project owner):

1. **Hook command-buffer recording and replay.** Find
   Begin/End/RunCommandBuffer, capture the D3D-level draws during recording,
   and replay the captured list at Run. This stays D3D-level, like SVR2011,
   but adds a capture and replay layer.
2. **PM4-level native backend.** Translate the command stream, indirect
   buffers included, in the runtime's command processor, while reusing
   `src/native`'s Vulkan pipeline, texture and shader code. This covers every
   path, but it is closer to emulation.
3. **Hybrid.** Use D3D hooks for the device ring and PM4 translation only
   inside the indirect buffers that the hooks don't cover.

Next fact-finding steps, whichever option is chosen:

- Identify who writes the 0x1F99xxxx–0x1FC9xxxx buffers: use a write watch
  (`src/native/write_watch.cpp`) on one IB, plus the guest call stack.
- Check whether the buffers are recorded once and replayed, or rewritten
  every frame.
- Find where `0x82DB5240` is not the INDIRECT_BUFFER source: the GPU sees 7.9
  IBs per present, but the hook sees 2.0.

## Gameplay measurement (2026-10-02, exhibition 1v1, Classic)

Played by hand with an Xbox pad (`--input_backend=xinput`). Session:
`analysis/runtime-20261002-220750`. Draw counts per present over each
10-census window:

| Phase | D3D-hooked draws | GPU draws | GPU IBs | Hooked share |
|---|---|---|---|---|
| Main menu | 16 | 82 | 8 | 19% |
| Character/arena select | 37–121 | 103–192 | 8–9 | 36–63% |
| In match (steady) | 170–200 | 280–318 | 12 | about 60% |
| Peak (end-of-match scene) | 549 | 617 | 16 | 89% |

During gameplay, about 110–120 draws per present still bypass the hooks.
That confirms the hybrid design (ADR-0015). Even at peak, the total is about
600 draws per present, well within what SVR2011's native renderer handles.

## Native bring-up, step 1 (2026-10-02)

- **Shaders.** UE3 builds almost every shader object in place and never calls
  Create*Shader (3 + 3 calls). Both paths end in the XDK register functions,
  which are now hooked: `0x82DAB250` for vertex shaders (header copy at
  object+0x368, microcode pointer at +0x20) and `0x82DA96A8` for pixel
  shaders (header at +0x28, microcode at +0x18). The container is rebuilt
  from header and microcode. The main menu registers 371 VS and 909 PS
  objects, with 165 + 816 unique containers.
- **XenosRecomp.** A private copy of the source is in `.tools/xenosrecomp`,
  with `patches/xenosrecomp-wweallstars.patch` (SVR2011's patch plus a fix
  declaring each vertex input once, since UE3 skinning fetches BLENDWEIGHT0
  twice). 976 of 981 convert. 5 are quarantined in
  `cache/guest-shaders-rejected`: undeclared temp/constant registers, and one
  crashes the tool (`vs_e20973377b9b2417`). See `scripts/triage_shaders.py`.
- **Native run (headless).** Stable, and every bound shader resolves. Nothing
  is drawn yet: the device's vertex declaration (device+0x2ED8) is null at
  every draw. UE3 issues draws with no D3D vertex declaration bound; on Xenos
  the vertex shader's vfetch instructions carry format, offset and stride.
  **Next:** build the Vulkan vertex input layout from the vertex shader's
  vfetch data (XenosRecomp already decodes it), not from the declaration.
- **Vertex layouts without declarations (solved).** In the container, the
  vfetch instructions are placeholders (format 0, stride 0). D3D patches them
  in the live microcode (shader object+0x20) when it binds the shader to a
  declaration. The native renderer now reads each element's vfetch from the
  live microcode at draw time: the element list and code offset come from the
  container header (`guest::DecodeVertexElements` and `DecodeVertexFetch`),
  and stream = 95 − fetch constant. Layouts are cached by the decoded fetches.
  Result: on the main menu every hooked indexed draw goes through the native
  Vulkan path (4 per frame), none are rejected and 3 pipelines are built.
  **Next:** command-buffer (IB) translation for the remaining ~80% of menu
  draws (ADR-0015), then the first visual check in a window.

## Correction: the real coverage gap (2026-10-02)

The "about 80% of draws bypass the hooks" figure above was wrong. GPU draw
counts include D3D's own internal draws:

- 48 point-list draws per present, from a buffer D3D builds once (SVR2011
  shows the same pattern).
- One rect-list draw per Resolve.

The native renderer covers both through its Clear and Resolve hooks.

The ~8 indirect buffers per present are 48–64 KB segments of D3D's command
ring (0x1F990000–0x1FC96980). Their contents change on every reuse; they are
not pre-recorded command buffers.

Recounted from the gameplay session (per present):

| Phase | GPU draws | Points (D3D) | Resolves | Hooked | Unexplained |
|---|---|---|---|---|---|
| Main menu | 82 | 48 | 13 | 16 | **6** |
| Select screens | 155 | 48 | 13 | 88 | **6** |
| In match | 280–300 | 48 | 16 | 160–180 | **50–56** |

The gap that remains is about 50 draws per present during a match: draw
paths that the hooked D3D entry points (DrawIndexedVertices, BeginVertices,
ClearRect) do not cover. Cursor attribution over the D3D library and its 229
UE3 callers does not place them. Next: count draws per primitive type in a
match on the GPU side and find which other D3D draw function or inline path
they use. ADR-0015 (hybrid) still holds, with a much smaller translated part.

## First windowed native run: black screen (2026-10-02)

`--wwe_native_renderer=true` in a window shows a black screen. Showing render
targets 1–4 with `--wwe_native_debug_target` is black as well. The capture
method was checked against Classic, which captures fine.

- The present hand-off (external frame into the swap path) looks correct.
- Draws go through, with no rejections: hooked indexed draws render into
  1280x720 k_8_8_8_8 targets with alpha blending, and 9 pipelines are built.
- The 48 point-list draws per frame are one 24-draw buffer
  (`DRAW_INDX_2 00010081`, one vertex each) replayed twice: a D3D-internal
  sync trick with no visible content. The main menu really is about 22 draws.
- In headless runs the game never gets past the startup screens (4 UI quads
  per frame), so menu-level debugging needs windowed runs.
- Main-menu BeginVertices calls never reach EndVertices: they are D3D's own
  Clear/Resolve quads.

Next suspects, in order:
1. Textures sampled as black: the default texture is used, or guest texture
   loads fail.
2. Constants or blend state leaving alpha at 0.
3. Draws landing in a target that is not the one resolved into the front
   buffer.

Tools: `--wwe_native_debug_no_blend`, `--wwe_native_debug_null_slots`, and
`--wwe_native_trace_frame` in a windowed run timed to the main menu.

## Black screen diagnosis (2026-10-02, diagnosing-bugs loop)

Feedback loop: `python scripts/native_frame_check.py --frame N [-- flags]`.
It runs headless, dumps the presented native image
(`--wwe_native_dump_frame`) and reports RED when the image is all black.
Probes: `--wwe_native_debug_solid_ps` (every pixel shader solid magenta) and
`--wwe_native_debug_target`.

Hypotheses and what the probes showed:

1. Colour write mask 0: **falsified** (0xF on visible draws).
2. Culling: **falsified** (cull 0).
3. Textures or constants: not the first cause. With the magenta pixel shader
   the frame was still black.
4. Draws landing in a target that never reaches the front buffer:
   **confirmed and fixed.** Magenta showed only in render target 5. The game
   clears and resolves a surface as k_8_8_8_8_GAMMA (RB_COLOR_INFO 000102D0)
   and draws into it as k_8_8_8_8 (000002D0). The native key contained the
   guest format, so these became two images. `guest::ColorTargetKey` now keys
   on EDRAM base, pitch and *host* format (regression tests in
   `tests/native/test_guest_decode.cpp`). With the magenta probe, the
   presented frame is now non-black.

Still open (the loop is RED with the real shaders):

- **A.** The real pixel shaders output black. Suspects: texture binding, the
  constant layout, the alpha-test threshold.
- **B.** Geometry: the magenta probe draws a single 200 px wide, full-height
  bar where a full-screen startup quad is expected. The position attribute is
  k_16_16 (signed integer). The 32-bit byte swap of vertex data probably
  leaves its x/y halves swapped; `wwe_native_swap_half2` corrects that only
  for texcoords.

### Diagnosis, continued (2026-10-02, late)

- Ground truth at frame ~600 (Classic, windowed): the Bink-era THQ logo, a
  3D red swoosh on white. It is real geometry, not a video quad. Positions are
  k_16_16 signed integer (exp_adjust 0, stride 4); raw words such as
  `F14DF627` give x = 0xF14D, y = 0xF627 on the 360 (first half = x), and
  feed a 3D projection (c0–c3).
- **Fix in place (theory sound, not yet visually verified):** 16-bit
  positions are swapped back in the shader (`tfetchPosition`, mask bits 16+/24+
  via `guest::HalfSwapBit`). Only x/y are swapped for 2-component data: the
  old `.yxwz` texcoord swap would have set w = 0. Tests in
  `test_guest_decode.cpp`.
- The magenta probe now draws a thin line at the bottom edge instead of a
  200 px bar. Geometry is still wrong, and with the real shaders the frame is
  still black.
- **Next probe:** a differential loop. Dump the Classic presented frame at the
  same frame number (runtime patch) and compare it with the native dump. Then
  check the vertex shader's clip transform (`g_NdcScale`, half-pixel offset)
  and the integer-versus-normalized interpretation of the 16-bit position.
- **Differential loop:** `scripts/native_frame_check.py --mode diff`. Classic's
  front buffer is dumped through `--readback_resolve=full` plus
  `DumpGuestFrontBuffer`; it is stored as ARGB. Classic frame 600 is the
  THQ intro video.
- At frame 600 every input checks out: vertex format R16G16_SSCALED with
  stride 4 and swap mask 0x10000, and constants c5–c8 form a full-screen
  2D transform. Viewport 1280x720 and scissor are full screen. The math says
  the quad covers the screen, yet the magenta probe shows only a line, or
  nothing.
- **Loop weakness:** frame 600 is mid-video, and the probe result varies
  between runs (a line in one run, nothing in another). Next: a
  frame-capture tool (RenderDoc) to see the actual GPU inputs and outputs,
  or a deterministic static scene for the loop.

## Black screen fixed: missing user-memory draws (2026-10-03)

The native renderer now displays the THQ intro and the All Stars title screen.
The missing paths were D3D user-memory draws, not a need for PM4 translation
to display these screens:

- `0x82DAEB80` (DrawVerticesUP) calls BeginVertices, copies the vertices, then
  commits the ring inline at `0x82DAEBB8`. It never calls EndVertices. Hooking
  the wrapper captures the movie's YUV draw.
- `0x82DAEBC8` (BeginIndexedVertices) allocates both buffers. The index output
  pointer is r10; the vertex output pointer is at caller stack+0x54, confirmed
  by the stores at `0x82DAF0DC/E0`. EndVertices commits this draw too. These
  previously uncaptured draws include the passes compositing the movie into
  the front buffer.
- `0x82DAF108` (DrawIndexedVerticesUP) commits inline at `0x82DAF198` and needs
  the same completion hook as DrawVerticesUP.

The hooks now retain both allocations and submit indexed draws with the
guest's index width and signed base-vertex offset. Completing a draw consumes
the pending allocation so a later EndVertices cannot submit it again.

The capture loop had a separate bug: Present called DumpGuestFrontBuffer
after the native readback, overwriting the native PPM with stale guest memory.
Native runs now skip the Classic dump. The image checker also requires over
1% sampled nonblack coverage by default, so a two-pixel magenta line no longer
passes. This is a visibility check, not a pixel-accuracy guarantee.

Validation: all 24 native tests passed (22,152 assertions). A real-shader
frame-600 capture changed from entirely black to the THQ intro. A 35-second
windowed run reached the All Stars "PRESS START" screen, with no fatal guest
targets and no rejected draws in the frame-1500/1800 statistics. Evidence:
`analysis/runtime-20261003-000821/shot-003.png` and its runtime log. Match
rendering and complete draw coverage have not been verified by this fix.

The final differential run passed: `python scripts/native_frame_check.py
--mode diff --frame 600 --seconds 16`, mean absolute difference 3.3/255
(threshold 24), with both captures showing visible content. Frame 600 is
animated, so this comparison does not establish exact pixel equivalence.

Reproduce: `python scripts/native_frame_check.py --frame 600 --seconds 16`.

## Match draw census: nothing unattributed (2026-10-03, ticket 04)

A full Classic 1v1 match (`analysis/session-t04/`) with
`--wwe_d3d_census --gpu_draw_census` balances in every 60-frame window
(`scripts/check_census.py`: 544 windows, 0 mismatched). The "50 unexplained
draws per present" above were two D3D draw entry points the census did not
count:

- BeginIndexedVertices `0x82DAEBC8` (DrawIndexedVerticesUP): about 50 per
  present in-ring. The native renderer already hooks it (user-memory draws).
- DrawVertices `0x82DAF1A8` (non-indexed, about one per present, all from UE3
  through `0x82888178`). The native renderer does not hook it yet.

ADR-0015's PM4 path is not needed for any draw seen in a match. Per-phase
numbers: `python scripts/census_report.py <runtime.log> [phase=frame ...]`;
the census now also logs each draw builder's guest callers
(`draw-census callers`).

## Resolves the CPU reads back: none (2026-10-03, ticket 06)

`--wwe_resolve_read_watch` (`src/resolve_watch.cpp`) makes each resolve
destination's pages fault in the guest CPU views and records the first CPU
access per page after each resolve. From boot to the result screen of a 1v1
match, 11 destinations (all `k_8_8_8_8`, 256x256 to 1280x720) were resolved
and the CPU read none of them; three were written first after being freed
(allocator `0x82411B90`, memcpy `0x82C307A0`). Native keeps every resolve on
the GPU with no write-back. Table and limits: ticket 06.
