# Native image quality: "darker" and anti-aliasing

Date 2026-10-03. Owner's observations after playing on Native: (1) the image
is a bit darker than Classic; (2) Native has no anti-aliasing and looks worse.
The goal is a Native image that looks better than Classic. Addresses are base
XEX v5. ReXGlue paths are relative to `.tools/rexglue-source` (`f5337cdc` plus
`patches/`). The captures are the scene runs of ticket 25
(`analysis/scenes/*`, `analysis/oracle/*`). They were made before ticket 29
(`12b45de`, `926e49a`).

## Answer

- **Darker: not a global gamma error.** Classic and Native reach the screen
  through the same presenter, the same gamma ramp and the same 256-entry
  table path. In all five boot and menu frames, Native's pre-present image
  matches Classic. On the 3D model in frames 3600 and 4750, native-1 is within
  1 LSB.
- **The likely causes are local, ranked:** (1) draws Native skips, which
  leave black where Classic has content; (2) the HDR bloom chain clipped to
  8-bit [0,1]; (3) Xenos `_GAMMA` semantics (PWL encode, decode and
  linear-space blending), which Native ignores. Number 3 cancels out in the
  menus but not for every in-match draw.
- **AA:** the game renders at 1x (no MSAA), and Classic's default adds no AA
  either (`swap_post_effect=none`), so "Classic has AA" is not true at
  defaults. Recommended order: FXAA/CAS through the presenter we already
  share (no code), then 2x2 supersampling built on
  `wwe_native_resolution_scale` with a box downsample, then forced 4x MSAA on
  the main-scene target.
- **ADR flags:** ADR-0002 (frame comparison) needs every quality option to be
  off in scene runs. ROADMAP's Deck minimum spec (ADR-0006) makes SSAA
  opt-in. The map puts scaling and AA in the "next map".

## A. How each renderer produces the final image (2026-10-03)

### Classic

- `k_8_8_8_8_GAMMA` render targets: the pixel shader output is linear. It is
  encoded to Xenos PWL gamma on write: in the FSI path when packing
  (`spirv_translator_rb.cpp:2378-2390`), or in the host-RT path, either in
  the shader (`spirv_translator_rb.cpp:1327-1348`, flag set in
  `vulkan/command_processor.cpp:6166-6175`) or by storing linear
  `R16G16B16A16_UNORM` (`gamma_render_target_as_unorm16`, default true,
  `flags.cpp:24`) and encoding on transfer/resolve
  (`vulkan/render_target_cache.cpp:1728-1730, 5781-5788`). ADR-0003 puts
  Classic on the FSI path.
- Texture fetches with sign `kGamma` (3) are decoded PWL→linear in the shader
  (`spirv_translator_fetch.cpp:1995-2010`; enum `xenos.h` `TextureSign`).
  The PWL curve is in `xenos.cpp:27-100`.
- `k_16_16` / `k_16_16_16_16` targets are remapped from -32..32 to -1..1
  through the exponent bias (`vulkan/command_processor.cpp:6388-6396`), and
  undone on resolve (`util/draw.cpp:1068-1077`).
- Present: the swap pass applies the game's gamma ramp. It uses the 256-entry
  table for 8_8_8_8 front buffers and PWL only for 2_10_10_10
  (`vulkan/command_processor.cpp:2484-2490`).

### Native

- Colour targets: `k_8_8_8_8` and `k_8_8_8_8_GAMMA` both map to
  `R8G8B8A8_UNORM` (`src/native/guest_decode.h:354-355`) and share one image
  (`guest_decode.h:359-367`, the RT gamma/linear aliasing fix in
  `native-renderer.md`). Nothing encodes PWL on write. XenosRecomp has no
  gamma handling (no `gamma`/`srgb` anywhere in
  `.tools/xenosrecomp/XenosRecomp/`). `textures.cpp` ignores the fetch
  sign fields.
- `k_16_16` → `R16G16_SNORM` (`guest_decode.h:344-345`), without the -32..32
  remap. `RB_COLOR_INFO.color_exp_bias` (bits 20-25, `registers.h:745`) and
  `copy_dest_exp_bias` are never read (`native_renderer.cpp:694-705,
  1180-1240`).
- Resolves are plain `vkCmdBlitImage` copies (`native_renderer.cpp:1226-1236`).
  A destination format missing from `GetFormatInfo` (`textures.cpp:106-136`)
  falls back to `R8G8B8A8_UNORM` (`textures.cpp:857-864`).
- Present: Native blits the front-buffer resolve into its frame and publishes
  it (`native_renderer.cpp:1985-1996, 2050-2055`). The SDK samples it
  "through the usual gamma and post-effect path" as `k_8_8_8_8`
  (`vulkan/command_processor.cpp:2349-2362`).

### What the game sets (traces)

The front buffer is resolved from the `000102D0` surface (format 1,
`k_8_8_8_8_GAMMA`) into a `k_8_8_8_8` texture (fetch dword1 `…86`, format 6)
with gamma sign (dword0 `8A0000FE`) (`analysis/session-t19a/runtime.log:266`).
Classic therefore also uses the 256-entry table, so **the display gamma ramp
is not a difference.**

Render targets seen in every trace (`analysis/codex-menu-*.log`,
`analysis/runtime-2026100*/runtime.log`, `analysis/session-t19a`):

| RB_COLOR_INFO | Format | Size | Use (traced) |
|---|---|---|---|
| `000002D0` | k_8_8_8_8 | 1280x720 | Scaleform UI (k_8 glyph textures) |
| `000102D0` | k_8_8_8_8_GAMMA, same EDRAM | 1280x720 | 3D draws with DXT1 gamma textures, depth/bloom composite, front-buffer resolve |
| `000402D0` | k_16_16 | 1280x720 | depth pass (clear flags 30) |
| `000705A0` | k_16_16_16_16_FLOAT | 400x182 (322x182 used) | bloom: samples the scene resolve (`k_8_8_8_8_AS_16_16_16_16`, gamma) |

A throwaway parser over `analysis/codex-menu-fixed3.log` (session scratchpad, not committed): of 35 draws into the GAMMA
view, 7 sample only gamma textures, 4 mix gamma and non-gamma, 19 sample only
non-gamma (`k_24_8_FLOAT` depth plus `k_16_16_16_16_EXPAND` signed bloom), and
5 sample none.

### Measurements on disk

Mean channel values, Classic (three runs) against Native (two runs), at the
same frame numbers:

| Frame | Classic RGB / Y | Native RGB / Y | Notes |
|---|---|---|---|
| boot 2700 | 47 49 68 / 50.2 | 47 50 68 / 50.6 | identity mapping |
| boot 3150 | 4.3 10.2 21.0 / 9.7 | same / 9.7 | |
| boot 3600 | 63 59 92 / 62.4 | 61 60 95 / 62.8 | native-1 within 1 LSB on The Rock (p99 diff 1) |
| boot 4750 | 64 60 88 / 62.6 | 61 59 89 / 61.2 | native-1 within 2 LSB on the model |
| match 6300 | 44 43 55 / 44.2 | 21 19 15 / 19.0 | arena preview background **black** on Native |
| match 6600 ring mat region | 87 88 89 | 88 89 89 | same camera; Native's smoke is grey blobs |
| match 6000 Hogan chest | 149 89 75 | 170 102 84 | same pose (run spread 0); Native **brighter** |

Fitting Native = Classic^g over each frame gives g = 1.00 on all boot frames.
Match frames are not comparable pixel for pixel: matches do not replay by
frame (ticket 25), and ticket 30 is open. A curve fit over them is
meaningless (g swings from 0.14 to 3.0). No capture shows a gamma-curve
darkening. Method: PIL/numpy, Rec.709 luma, per-bin medians.

### Causes, ranked

1. **Skipped draws leave holes (proven on the sheet).** The match run skipped
   about 2 `no_shader` draws per frame (`exhibition-1v1/native-1/runtime.log`,
   for example 1,440 per 720 frames from frame 9000 on) and 1,583 `no_texture`
   draws from frame 6951 (resolve target `0x15010000`, ticket 31). Frame
   6300's missing arena preview halves the frame's mean luma. Missing
   lighting, crowd or glow passes make a scene look darker as a whole.
   Ticket 29 has since landed. Re-run the scene before going further.
2. **The HDR bloom chain is clipped (code, unmeasured).** The bloom target is
   `k_16_16_16_16_FLOAT` and is resolved into a `k_16_16_16_16_EXPAND` (29)
   texture that the composite samples with sign 1 (`f29s1` in the trace).
   Format 29 is missing from `GetFormatInfo`, so the destination is
   `R8G8B8A8_UNORM`. The blit converts float to UNORM, which clamps to
   [0,1] (Vulkan spec, `vkCmdBlitImage` format conversion). Any bloom above
   1.0 is lost. That means less glow around lights and highlights, which
   reads as "darker", mostly in arenas. The `k_16_16` target misses the
   x32 remap in the same way. In the menus the bloom stays under threshold,
   which fits the menu match.
3. **`_GAMMA` semantics (code; direction depends on the draw).** Native
   skips both the decode and the encode. They cancel for "sample a gamma
   texture, write it unchanged", which covers most menu and UI work. They do
   not cancel otherwise. Worked with the SDK's PWL functions:

   | Case | Classic | Native |
   |---|---|---|
   | Gamma texel 128 × light 0.5 into GAMMA target | 96 | 64 (darker) |
   | Constant colour 0.1176 (traced draw, `ps c0`) into GAMMA target | 92 | 30 (darker) |
   | 50% alpha blend of 255 over 0 | 191 | 128 (darker) |
   | Additive 64 + 64 (blend `01000101`, traced) | 96 | 128 (brighter) |

   Lit or shadowed surfaces, translucent fades and fog come out darker on
   Native. Stacked additive lights come out brighter. That matches the
   "punchier, darker shadows" look of the versus screen (frame 6000 crop).
   Open question: the menu trace binds The Rock's lit draws to the GAMMA
   view, yet frame 3600 matches within 1 LSB. Before building this, check
   which view those draws use at frame 3600 (trace that frame).
4. Ruled out: the gamma ramp/presenter (shared path, same table), exponent
   bias (0 in every traced `RB_COLOR_INFO` and `RB_COPY_DEST_INFO`).

### Fix and verification

- First, re-run `exhibition-1v1` after ticket 29 and close ticket 31. Then
  judge "darker" again.
- Add `k_16_16_16_16_EXPAND`, `k_16_16_16_16`, `k_16_16_EXPAND` and
  `k_8_8_8_8_AS_16_16_16_16` to `GetFormatInfo` with float or SNORM-x32
  host formats, so resolves keep the range. Apply the -32..32 remap to
  `k_16_16` targets as Classic does. Honour `color_exp_bias` and
  `copy_dest_exp_bias`.
- Gamma: give `k_8_8_8_8_GAMMA` targets linear storage (`R16G16B16A16_UNORM`,
  as Classic's default), PWL-encode on resolve, and PWL-decode `kGamma`
  fetches in the converted shaders. The fetch sign has to reach the shader:
  a per-slot bit in push constants, or a specialization constant. This
  changes a keying rule from the black-screen fix: the GAMMA and non-gamma
  views then need different storage again, or one linear image with an
  encode/decode view rule.
- Verify on frames that line up: the versus screen (frame 6000 regions have
  run spread 0) and menu frames, with the Classic envelope. Add a per-pass
  check: dump the bloom resolve (`--wwe_native_debug_resolve`) against
  Classic's readback at the same frame. Matches can only be judged
  statistically (luma histograms over many frames) until ticket 30 makes
  them deterministic.

## B. Anti-aliasing (2026-10-03)

### What the game asks for

- Every traced `RB_SURFACE_INFO` is `14000500` or `05800190`. `msaa_samples`
  (bits 16-17, `registers.h:669-676`) is 0, so the game renders at 1x.
- Xenia Canary's baseline log does show 4xMSAA targets
  (`analysis/xenia-baseline/run2_vulkan_debug.log:1820, 12913, 12933,
  1885652`). Each is created next to VS `1E6883FCCDE1F688` with no pixel
  shader, around `VdRetrainEDRAM` and device setup. These are D3D's internal
  clears, not scene rendering.
- Classic follows the guest: it would emulate 2x/4x
  (`native_2x_msaa`, `flags.cpp:18`; MSAA resolve shaders,
  `vulkan/render_target_cache.cpp:69-111`), but the game never asks for it.
  `swap_post_effect` defaults to `none` (`graphics_system.cpp:35`), and the
  repo sets no AA cvar. **Classic at default has no AA either.** The owner's
  "worse" impression may come partly from Native's LOD bias (section C).

### Native today

- Single-sample everywhere: render targets (`native_renderer.cpp:440, 657`),
  pipelines (`pipelines.cpp:256-258`), textures (`textures.cpp:240`).
- `wwe_native_resolution_scale` 1-3 (`native_renderer.cpp:82-86, 976-978`)
  scales targets, viewports, scissors and resolve targets
  (`native_renderer.cpp:627-628, 787-821`; `textures.cpp:855-856`), and
  downsamples CPU readbacks (`native_renderer.cpp:513-520`). The output is
  published at 1280·s × 720·s, and the presenter fits it to the window. On
  the owner's 2560x1440 swapchain (`classic-1/runtime.log`: "Created
  2560x1440 swapchain"), scale 2 is a resolution increase, not AA.
- The presenter in front of Native already offers FXAA
  (`--swap_post_effect=fxaa|fxaa_extreme`, `vulkan/command_processor.cpp:2470-2540`)
  and FidelityFX CAS or FSR1 (`--present_effect=cas|fsr`,
  `ui/presenter.cpp:47-77`). Our build enables them (`scripts/build.ps1:52-54`).

### Options

| Approach | Quality | Cost / risk | Fit with Native |
|---|---|---|---|
| FXAA (SDK swap effect) | Edges only, softens Scaleform text and HUD (runs on the final frame) | ~0 code. Test it now | Works today |
| CAS sharpen (presenter) | Not AA. Offsets FXAA/SSAA softness | ~0 code | Works today |
| FSR1 | Upscaler, not AA. Needs "well anti-aliased" input, before UI (AMD docs) | UI is already composited, so it would sharpen HUD aliasing | Poor fit as an AA |
| FSR2/3, TAA | Best AA | Needs motion vectors the game does not produce, and jitter in every converted VS | Out of scope |
| **2x2 SSAA**: scale 2 plus box downsample to the output | Everything: geometry, alpha test, specular, Scaleform | 4x pixel work. Effects targets scale too (`native_scale_effects` idea in svr2011-pc) | Uses the existing scale path. wwe-svr2011-pc does exactly this (`svr2011-pc-port.md:235-239`, item 8) |
| **Forced 4x MSAA** on the 1280x720 main targets | Geometry edges only. Alpha-tested hair/crowd need alpha-to-coverage | Sample count in the pipeline key; `vkCmdResolveImage` before each resolve blit (blit needs single-sample sources, Vulkan VUID-vkCmdBlitImage-srcImage-00233); depth resolve via sample 0 (Vulkan 1.2 depth/stencil resolve) for `ResolveDepth`; clears are fine | Moderate. Only targets where `guest_width·s == g.width` (`native_renderer.cpp:1478-1479`) |

Interactions to watch:

- Resolves: every scene resolve (front buffer, bloom source) must
  downsample or resolve first.
- Depth resolves: post-processing samples `k_24_8_FLOAT` depth (`f23s0`).
- Half-pixel offset: per guest pixel, so it scales correctly
  (`guest_decode.h:402-427`).
- Scaleform: renders into the same 1280x720 target. It is supersampled under
  SSAA (good) and multisampled under MSAA (harmless).
- Alpha test: off in every traced draw (`colorctl 87000007`, bit 3 clear).
  Check hair and crowd in a match trace before relying on MSAA.
- Oracle: scene runs must stay 1x, with no FXAA and no MSAA (ADR-0002).
  With scale > 1, compare a downsampled dump.

### What "better than Classic" means at 720p

Classic at default is 1x, with no post-process AA and LOD bias 0. Native beats
it on parity plus: (a) 2x2 SSAA at a 720p output, (b) a higher internal
resolution on 1080p/1440p displays, (c) optional CAS. MSAA brings edge AA at
a fraction of the SSAA cost for the Deck.

### ADRs and plans

- ADR-0001 puts MSAA/AA in v1.0 scope. ROADMAP M3 lists "resolution scale
  1–4x; MSAA/AA" (`docs/ROADMAP.md:49`); the native cvar caps at 3.
- ADR-0002: Classic is the reference. AA must be a cvar that the scene runner
  forces off. That is a constraint, not a conflict.
- ADR-0006 / ROADMAP:35: the Steam Deck must run at native resolution and the
  original frame rate. SSAA (4x fill) must be opt-in and measured on the Deck.
  MSAA or FXAA is the Deck default candidate.
- ADR-0003 (Vulkan only) and ADR-0015 (hybrid hooks) are unaffected.
  ADR-0012: an SSAA/MSAA mode needs no new converted shaders. Gamma decode in
  fetches does: the XenosRecomp patch, a new converter ID.
- `.scratch/playable-build/map.md:28, 79`: scaling and AA are "next map".
  Starting AA now conflicts with the map's order. Only the "darker" work
  belongs to the current map.

### Recommended order

1. (Now, no code) Capture Native with `--swap_post_effect=fxaa` and
   `--present_effect=cas` for the owner's sign-off. Record the GPU frame time.
2. Fix "darker" (section A). AA comparisons need parity first.
3. SSAA: `wwe_native_supersample` renders at scale s and box-downsamples the
   frame to the output before `Publish`. Port wwe-svr2011-pc's averaging with
   attribution. Measure the GPU time at 1x/2x on the desktop, then on the Deck.
4. Forced 4x MSAA for the main-scene targets, with resolve and depth-resolve
   handling. Measure against SSAA.
5. Optional: alpha-to-coverage for alpha-tested draws, if match traces show
   any.

Risks: effects targets (400x182 bloom) scaled with the scene change the bloom
look. Resolves and readbacks are 4x larger. Without MSAA, FXAA blurs UI text.

## C. Other differences (2026-10-03)

- **LOD bias:** Native adds -0.5 (`textures.cpp:41-43, 401-403`). Classic adds
  none (no such cvar in the SDK). Sharper but more shimmer at 1x, which looks
  like worse aliasing. Default it to 0 until SSAA exists.
- Anisotropy: both default to 3 (`wwe_native_anisotropy`,
  `textures.cpp:31-33`; `anisotropic_override`, `texture/cache.cpp:52`).
  This is not a difference.
- Half-pixel offset: on in both (`native_renderer.cpp:73-75`;
  `util/draw.cpp:29`).
- Native's present blit is `VK_FILTER_LINEAR` only for the 1:1 copy
  (`native_renderer.cpp:1995`). Scaling to the window happens in the shared
  presenter.
- Arena intro (frame 6600): smoke and sparks show as flat grey blobs on
  Native. Likely the same blend/gamma or missing-texture cause; check with a
  trace.
- `wwe_native_async_textures` shows nothing for a frame or two on new
  textures (`textures.cpp:36-39`): brief dark pop-in.

## Recommended spec outline

- Re-run `exhibition-1v1` after ticket 29. Close ticket 31 (`0x15010000`
  `no_texture`). Re-judge "darker".
- Native keeps HDR range: add the EXPAND, AS_16_16_16_16 and 16_16_16_16
  formats; apply the k_16_16 x32 remap; honour the exponent biases. Verify on
  the bloom resolve against Classic.
- Trace frame 3600 to settle which view lit draws use. Then decide on Xenos
  gamma semantics (linear storage for GAMMA targets, encode on resolve,
  decode on `kGamma` fetch in converted shaders).
- Make the LOD bias default 0, or tie it to supersampling.
- Owner sign-off captures with SDK FXAA and CAS on Native (no code).
- `wwe_native_supersample` (2x2 box downsample), ported from wwe-svr2011-pc
  with attribution. The scene runner forces it off. Measure desktop and Deck.
- Forced 4x MSAA on the main-scene targets (pipeline key, colour and depth
  resolve before blits). Measure against SSAA.
- Update ROADMAP/map: AA starts in the next map. Native scale cap 3 vs
  ROADMAP's 4x.

## Sources

- `.tools/rexglue-source/src/graphics/vulkan/command_processor.cpp` (2349-2362,
  2470-2540, 6166-6175, 6384-6402), `vulkan/render_target_cache.cpp`,
  `pipeline/shader/spirv_translator_rb.cpp`, `spirv_translator_fetch.cpp`,
  `xenos.cpp`, `flags.cpp`, `graphics_system.cpp`, `util/draw.cpp`,
  `ui/presenter.cpp`, `include/rex/graphics/registers.h`, `xenos.h`.
- `src/native/native_renderer.cpp`, `textures.cpp`, `guest_decode.h`,
  `pipelines.cpp`; `.tools/xenosrecomp/XenosRecomp/`.
- Captures: `analysis/oracle/boot-main-menu-975ae51bca24`,
  `analysis/oracle/exhibition-1v1-0ba0cc0b6859`, `analysis/scenes/*`. Traces:
  `analysis/codex-menu-*.log`, `analysis/session-t19a/runtime.log`. Xenia:
  `analysis/xenia-baseline/run2_vulkan_debug.log`.
- `docs/research/svr2011-pc-port.md` (wwe-svr2011-pc,
  https://github.com/KaikoClanworth1/wwe-svr2011-pc), `docs/adr/0001-0015`,
  `docs/ROADMAP.md`, `.scratch/playable-build/map.md`, tickets 29-31.
- AMD FSR1 input requirements:
  https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-spatial/
- Vulkan `vkCmdBlitImage` (format conversion, single-sample source):
  https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdBlitImage.html
