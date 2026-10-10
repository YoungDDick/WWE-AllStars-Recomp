# Colour: "Original" (Xbox 360 / Xenia) versus "Enhanced" (current Native)

Date 2026-10-10. The owner says Native's colours look "more natural, less
vibrant" than the game in Xenia, and wants a two-way setting for 1.0.0:
**Enhanced** (today's look) and **Original** (the Xbox 360's look, as Xenia
shows it). This note finds where Native's colour diverged and what each option
needs. ReXGlue paths (`SDK/`) are relative to `.tools/rexglue-source`
(`f5337cdc` plus `patches/`). Other paths are relative to the repo root. Nothing
here was run in game. The numbers come from the SDK's own curve functions, which
were evaluated in a scratch script (not committed).

## Answer

- **One cause, one commit.** Ticket 39 (`0eed268`, 2026-10-03) gave Native the
  Xbox's "gamma" render-target and texture semantics through Vulkan **sRGB**
  formats. Xenos uses its own **piecewise-linear (PWL)** curve, and Xenia and
  Emulated reproduce that curve exactly. The ticket chose sRGB on purpose and
  left "the PWL path [as] the follow-up if the owner still sees a difference"
  (`.scratch/playable-build/issues/39-xenos-gamma-on-native.md`, Answer).
- **Why it looks less vibrant.** Every lit 3D pixel, fog, fade or glow drawn
  into the game's `k_8_8_8_8_GAMMA` target is encoded with sRGB instead of PWL.
  For dark linear values, sRGB gives up to **+19 levels** more (linear 0.02: PWL
  20, sRGB 39). Dark channels are lifted more than bright ones, so
  shadows open up and saturation drops: a deep red goes from S = 0.90 to 0.81.
  That is the "natural, less vibrant" look (table below).
- **The rest of the colour chain already matches Xenia.** That covers the
  game's own gamma ramp, the display gamma the kernel reports (BT.709), the
  UNORM swapchain, and HUD/UI drawn on the non-gamma view. Native's frame goes
  through the SDK's presenter and gamma-ramp pass just like Emulated's.
- **Enhanced costs nothing:** it is today's code. **Original needs new work.**
  The faithful version does the PWL maths in the converted shaders: a new
  spec-constant bit for gamma targets, plus a per-slot gamma mask for texture
  fetches. That means a XenosRecomp patch, a shader-pack reconversion and new
  pipeline variants. It is not a single cvar flip. No present-time filter can
  reproduce it (see "What does not work").
- **Setting path:** copy `wwe_vibration_mode`, which already uses
  Original/Enhanced wording. The cvar goes in `src/settings_cvars.cpp`, the row
  in `src/settings/registry.h`, and the launcher JSON is regenerated. Ship it as
  "applies at next start" (`kRequiresRestart`), because a switch needs a
  different set of pipelines.

## 1. What changed, and when

| Commit | Date | What it did to colour |
|---|---|---|
| `459d91d` | 2026-10-02 | Native keys render targets by EDRAM base, pitch and host format, so `k_8_8_8_8` and `k_8_8_8_8_GAMMA` share **one** RGBA8 image (fixes a black frame). No gamma maths at all: Native drew everything in encoded space. |
| `5bb7cd0` | 2026-10-03 | Ticket 35: trace shows lit 3D (9 opaque) and additive (4) draws go to the `k_8_8_8_8_GAMMA` view. Native was **washed out** at the versus screen (mean RGB 74/73/82 vs Emulated 64/55/55). |
| `469d262` | 2026-10-03 | Tickets 31/36: HDR texture formats (bloom chain keeps >1.0). Changed nothing at the versus screen (ticket 36 Answer). |
| **`0eed268`** | 2026-10-03 | **Ticket 39: Xenos gamma through sRGB views.** Gamma-target draws render through an `R8G8B8A8_SRGB` view. Gamma texture fetches sample through `*_SRGB` views. Versus screen now 66/57/57 vs Emulated 64/55/55. |
| `7541b33` | 2026-10-03 | Owner verdict: "The color looks perfect now", judged against Classic/Emulated, not Xenia (ticket 39 comment). |
| `6ed07f5` | 2026-10-04 | Native becomes the default renderer (ADR-0020), so every player sees the sRGB look. |
| `af86a2b` | 2026-10-04 | F12 on Emulated saves the presenter output after the gamma ramp (ticket 37 work). No colour change. |

The git searches (`--grep` for colour, gamma, srgb, palette, tonemap, pwl,
saturat, contrast, bright, hdr, swapchain, colorspace, vibrant) found no other
commit that changes colour output. Nothing in `patches/` touches gamma. The
only match is a comment in `rexglue-native-frame.patch:34` saying Native's
frame goes "through the usual gamma and post-effect path".

## 2. The mechanism

### Native today (Enhanced)

- `src/native/texture_formats.h:63-94`: `GammaFormat` maps `R8G8B8A8_UNORM` to
  `R8G8B8A8_SRGB` and BC1/2/3 to their `_SRGB` twins. The header comment says
  "sRGB approximates the Xenos piecewise-linear curve". `FetchSamplesGamma`
  returns true only when all three colour sign fields are `kGamma` (3).
  `ColorTargetIsGamma` is RB_COLOR_INFO format 1.
- `src/native/native_renderer.cpp:938-965`: each colour target is created
  mutable-format with a second sRGB view (`gamma_view`).
- `native_renderer.cpp:1016-1030`: a draw whose RB_COLOR_INFO format is
  `k_8_8_8_8_GAMMA` renders into `gamma_view`. The hardware decodes for
  blending, blends in linear and encodes **sRGB** on write.
- `native_renderer.cpp:2006-2008`: the pipeline key's `color_format` is the
  sRGB format for these draws, so they have their own pipelines.
- `src/native/textures.cpp:200-202, 247-259, 704, 808-810`: textures with an
  sRGB twin are mutable. Gamma fetches sample through the sRGB view, which
  decodes **sRGB** *before* filtering. Formats without a twin (the 16-bit ones)
  are not decoded at all (`textures.cpp:250`).
- Resolves copy the encoded bytes with `vkCmdBlitImage`
  (`native_renderer.cpp:1759-1778`). The front buffer therefore holds
  sRGB-encoded bytes. The SDK then shows it as `k_8_8_8_8` through the gamma
  ramp (`SDK/src/graphics/vulkan/command_processor.cpp:2361-2370`).

### Xenia and Emulated (Original)

- PWL curve: `SDK/src/graphics/xenos.cpp:27-100` (`PWLGammaToLinear`,
  `LinearToPWLGamma`). The SDK comment says it is based on the Source Engine's
  `X360GammaToLinear`/`X360LinearToGamma` and the D3D9 code in game
  executables
  (https://github.com/ValveSoftware/source-sdk-2013/blob/master/mp/src/mathlib/color_conversion.cpp#L329).
- Fetch: kGamma components are PWL-decoded **in the shader, after filtering**.
  The comment reads "Keep parity with D3D12 by applying explicit Xenos PWL
  gamma-to-linear conversion in shader code rather than relying on host sRGB
  sampling semantics" (`SDK/src/graphics/pipeline/shader/spirv_translator_fetch.cpp:2000-2006`).
- Write: PWL encode in the FSI pack path
  (`spirv_translator_rb.cpp:2390`), which the launcher selects with
  `--render_target_path_vulkan=fsi` (`launcher/src/Launcher.Core/LaunchArguments.cs:11`).
  On the host-RT path the encode is in the shader (`spirv_translator_rb.cpp:1346`).
  Alternatively, gamma targets are stored as linear `R16G16B16A16_UNORM`
  (`gamma_render_target_as_unorm16`, default true, `SDK/src/graphics/flags.cpp:24-26`,
  used only on the host-RT path, `vulkan/render_target_cache.cpp:246-247`).
- Xenia Canary has the same default and description:
  `gamma_render_target_as_unorm16 = true`
  (`.tools/xenia-canary/xenia-canary.config.toml:224`;
  https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/gpu/render_target_cache.cc,
  `DEFINE_bool(gamma_render_target_as_unorm16, true, ...)`). There is no
  `gamma_render_target_as_srgb` cvar in Canary's config or in that file.
  Xenia runs `gpu = "any"`, which on Windows is D3D12 with the RTV path
  (config lines 225, 254-267).

### Shared by both, and the same as Xenia (ruled out)

| Stage | Here | Xenia default |
|---|---|---|
| Display gamma the kernel reports to D3D's ramp code | hard-coded type 2 (BT.709), power 2.2222 (`SDK/src/kernel/xboxkrnl/xboxkrnl_video.cpp:37-40, 150-158`); no cvar | `kernel_display_gamma_type = 2`, `kernel_display_gamma_power = 2.22222233` (config lines 377-378; `xboxkrnl_video.cc` `DEFINE_uint32(kernel_display_gamma_type, 2, …)`) |
| Gamma ramp | the game's 256-entry table, captured from DC_LUT writes (`SDK/src/graphics/command_processor.cpp:421-457`); the front buffer is `k_8_8_8_8`, so Emulated uses the same table (`docs/research/native-image-quality.md` "What the game sets") | same mechanism |
| Swapchain | `B8G8R8A8_UNORM`, `SRGB_NONLINEAR` colour space, no sRGB encode (`SDK/src/ui/vulkan/vulkan_presenter.cpp:1245-1256`) | same |
| Post effect | `swap_post_effect = none` (`SDK/src/graphics/graphics_system.cpp:51`); Native adds CAS 0.5 (`src/settings_cvars.cpp:18`), which sharpens but does not change the palette | `postprocess_antialiasing = ""`, `postprocess_scaling_and_sharpening = ""` (config 109, 133) |
| Dither | `present_dither = false` (`SDK/src/ui/presenter.cpp:86`) | `postprocess_dither = true` (config 119): noise below 1 LSB, no palette shift |
| In-game brightness | none: OPTIONS has Sound, GamePlay, Controls only (`docs/research/options-menu-gfx.md`, Answer) | — |

### How big the difference is

Evaluated with the SDK's PWL functions and the standard sRGB formulas:

| Linear value written by a shader | PWL byte (Xbox/Xenia) | sRGB byte (Native) | Δ |
|---|---|---|---|
| 0.005 | 5 | 16 | +11 |
| 0.02 | 20 | 39 | **+19** |
| 0.05 | 51 | 63 | +12 |
| 0.18 | 110 | 118 | +8 |
| 0.25 | 127 | 137 | +10 |
| 0.5 | 191 | 188 | −3 |
| 0.9 | 243 | 243 | 0 |

The mean shift over linear 0..1 is +3.5 levels. The largest lift is in the
shadows.

| Linear colour from the shader | PWL bytes, saturation | sRGB bytes, saturation |
|---|---|---|
| (0.6, 0.08, 0.02) red | 204 72 20, S 0.90 | 203 80 39, S 0.81 |
| (0.05, 0.1, 0.4) blue | 51 83 166, S 0.69 | 63 89 170, S 0.63 |
| (0.3, 0.15, 0.08) skin | 140 102 72, S 0.49 | 149 108 80, S 0.46 |

A textured surface under a light (`enc(dec(t) × k)`) shifts as well. For
example, t = 64 at k = 0.25 gives PWL 16 and sRGB 30, and t = 192 at k = 0.5
gives 128 and 140. A texture drawn unlit cancels out in both modes (decode,
then encode with the same curve), so menus, UI and pass-through draws are
identical. That is why menu frames matched Emulated within 1 LSB
(`docs/research/native-image-quality.md` "Measurements on disk").

The versus-screen means (66/57/57 Native vs 64/55/55 Emulated, ticket 39) hide
this. The curves differ most in the darks, and a scene mean is dominated by
mid and bright pixels.

A smaller secondary difference: Native filters gamma textures in linear space
(hardware sRGB decode happens before filtering), while Xenia filters the
encoded texels and then decodes (`spirv_translator_fetch.cpp:2000-2006`). This
affects minified and mipmapped texels only slightly. Which of the two the real
Xenos texture unit does is **unverified**.

## 3. What "Original" needs

### What real hardware did

- The game renders into the `k_8_8_8_8_GAMMA` view of surface `000102D0` and
  resolves it to the `k_8_8_8_8` front buffer, a texture fetched with the gamma
  sign (`native-image-quality.md`, `analysis/session-t19a/runtime.log:266`).
  So the front buffer holds **PWL-encoded** bytes.
- The display controller then applies the 256-entry ramp that D3D builds for
  the display gamma the kernel reports. The SDK's notes on DC_LUT setups
  observed in other games are at `SDK/include/rex/graphics/register_table.inc:290-420`.
- WWE All Stars' actual ramp values were never dumped. Whether they are
  identity is **unverified**. They are shared by Native, Emulated and Xenia,
  so they do not explain the difference.
- HDMI output range (the dashboard's Standard/Intermediate/Expanded reference
  levels) and the TV's own gamma (2.2 vs 2.4/2.5) were outside the game's
  control. Xenia does not model them, and neither does this port. That a
  "real Xbox on a real TV" look would differ from Xenia on a PC monitor is
  **unverified** and out of scope. "Original" should mean "the bytes the Xbox
  360 and Xenia produce".

### Options

**A. PWL in the converted shaders, sRGB storage kept (recommended).**

- For an output to a gamma target, the shader writes
  `y = sRGB_decode(PWL_encode(x))` into the sRGB view, so the stored byte is
  exactly the PWL byte for opaque writes.
- For a gamma fetch, it samples the UNORM view and applies `PWL_decode` in the
  shader, as Xenia does, filtering in encoded space.
- Blending still runs in the hardware's sRGB-linear space. This is close to,
  but not exactly, PWL-linear. It affects the ~4 additive draws and the alpha
  fades on gamma targets.
- Work:
  - XenosRecomp (`patches/xenosrecomp-wweallstars.patch`, `XenosRecomp/shader_common.h`
    spec-constant list at lines 4-11): a new `SPEC_CONSTANT_PWL_GAMMA_OUTPUT`
    bit and the PWL functions.
  - Per sampler slot, a gamma bit the shader reads after each sample. The fetch
    sign is runtime state, so this needs a push-constant or shared-constants
    mask; Native already computes `FetchSamplesGamma` per slot at bind time
    (`textures.cpp:704, 808-810`).
  - `native_renderer.cpp:1996-2008`: set the bit for gamma draws when Original
    is on.
  - `textures.cpp:247-259`: do not take the sRGB view for gamma fetches when
    Original is on.
  - Reconvert the shader pack (new converter id; ADR-0012).
  - New pipeline keys for the prebuild plan and store (`src/native/prebuild_plan.h`, ADR-0019).
- Faithfulness: opaque lit surfaces and constants become byte-exact with
  Xenia; blends are within a few levels.

**B. Xenia-exact: linear `R16G16B16A16_UNORM` storage for gamma targets.**

- PWL encode on resolve, PWL decode in the shaders, and an encode/decode pass
  whenever the game switches between the GAMMA and the plain view of surface
  `0x2D0`. The game does this every frame: 3D goes through GAMMA, Scaleform UI
  through `k_8_8_8_8` (`native-image-quality.md` render-target table).
- This brings back the two-image problem that `459d91d` fixed and adds copy
  costs, as Xenia's own cvar text warns. It is exact, but it is the most work
  and the most risk.

**C. Use the Emulated renderer.**

- It is already PWL-faithful, but much slower, and ticket 37 (dark Emulated in
  owner sessions) is still open. It is useful as the reference for verifying
  A, not as a player setting.

### What does not work

- **A present-time LUT** that converts sRGB-encoded bytes to PWL-encoded bytes
  at swap. The front buffer mixes pixels whose last write was a linear shader
  value (which would need the LUT) with pass-through UI, text and texture
  pixels (which must not be changed). A global curve would darken the HUD,
  menus and movies, so it would not be "Original".
- **Turning the sRGB views off** (going back to before `0eed268`). That is the
  washed-out state of ticket 35 (lit surfaces darker, additive glow brighter),
  not the Xbox look.

### Verification for A

Use the versus screen at frame 6000, the frame that lines up between runs
(ticket 35/39, `analysis/scenes/t39/`). Native-Original should be within the
Emulated envelope, and the dark regions should match per pixel. Menu frames
must stay as they are. Then the owner compares by eye against Xenia.

## 4. Wiring the option

Follow `wwe_vibration_mode`, the existing Off/Original/Enhanced row:

1. **Cvar:** `src/settings_cvars.cpp` (pattern at lines 33-37). For example
   `REXCVAR_DEFINE_INT32(wwe_native_color_mode, 1, …)` with 0 = Original and
   1 = Enhanced, `.range(0, 1)` and `.lifecycle(kRequiresRestart)`.
2. **Row:** `src/settings/registry.h:58-126` (`PlayerSettings()`). Add a
   GRAPHICS entry with `{{"0","Original"},{"1","Enhanced"}}`, the help text,
   `/*startup=*/true` and `/*native_only=*/true`. Emulated is always Original.
   Keep it out of `GraphicsPresets()` (lines 21-27): it is a look, not a cost.
3. **Launcher:** regenerate `launcher/src/Launcher.Core/SettingsSchema.json`
   with `scripts/export-settings-schema.ps1` (the exe's `--print-settings-schema`).
   `SettingsSchemaDriftTests` fails until you do. Add the key to the lists in
   `launcher/tests/Launcher.Tests/SettingsSchemaTests.cs` and
   `LaunchArgumentsTests.cs:10-14`. The launcher writes the value to
   `wweallstars.settings.toml` in user data (`SettingsFile.cs:6-11`,
   `PortableLayout.cs:40`), and the game loads that file after its main config
   (`src/settings/config_file.h:17-25`). No launch argument is needed.
4. **In game:** the F4 page reads the same registry. A `kRequiresRestart` row
   is saved and marked "Applies the next time the game starts"
   (`docs/research/live-settings.md`, `settings_page.cpp:122-137`).
5. **Runtime switching:** possible in principle, since the mode only picks
   views and a spec bit per draw. But the first frames after a switch would
   hit uncompiled pipelines (`pipeline_pending` skips, visible pops) unless
   the prepare step builds both variants. Ship it as restart-only first.

Areas the change affects:

- **HUD/UI:** unaffected. Scaleform UI draws use the non-gamma view
  (`000002D0`).
- **Achievement toast and ImGui overlay:** unaffected. They are drawn by the
  host after the guest frame.
- **F12/screenshots:** follow the chosen mode, since they capture the
  presenter output (`af86a2b`).
- **Bloom:** its source resolve is sampled with the gamma sign
  (`native-image-quality.md` render-target table), so it follows the mode.
- **Scene runs:** CONTEXT.md says scene runs turn enhancements off (glossary,
  "Enhancement"), so they would run in Original.

## 5. Suggested player-facing text

- Row label: **Colour**, matching the repo's British spelling ("Colours" in
  the tickets).
- **Original:** "The Xbox 360's colours: richer and deeper shadows, as in Xenia."
- **Enhanced:** "Softer, more natural colours with more detail in dark areas."
- Help line, in the style of the Vibration row: "Original matches the Xbox 360's colours, richer with
  deeper shadows. Enhanced is softer and more natural, with more detail in
  dark areas. Native only."

## Decisions (owner, 2026-10-10)

- Default: **Enhanced**. Original is opt-in.
- Blending: option A's approximate blending is close enough; B is not needed.
- Before tickets: capture a matched Native/Xenia dark-scene pair to confirm
  the sRGB curve is the whole difference.

## Capture check (2026-10-10): sRGB is only part of it

The owner played one Native match (Hogan vs Cena, SmackDown, default
enhancements) and pressed F12 (`analysis/color-play/`, not committed). Patches
were compared against the Emulated oracle
(`analysis/oracle/exhibition-1v1-eea473933760/classic-1/`), auto-aligned within
±40 px. Pairs: own 0003 / ref 6600 (arena flyover), 0008 / 7200 (Hogan
entrance), 0024 / 9000 (Cena salute). "Remapped" = Native bytes sRGB-decoded,
then PWL-encoded (`LinearToPWLGamma`).

| Patch | p10 E / N / remapped | Saturation E / N / remapped | MAE N / remapped |
|---|---|---|---|
| ring floor | 12 / 77 / 69 | 0.016 / 0.015 / 0.018 | 63.6 / 62.1 |
| apron banner | 6 / 16 / 5 | 0.217 / 0.188 / 0.233 | 40.6 / 35.6 |
| upper crowd/roof | 16 / 50 / 32 | 0.466 / 0.291 / 0.394 | 40.9 / 32.8 |
| Hogan torso | 36 / 44 / 25 | 0.661 / 0.602 / 0.696 | 21.4 / 24.0 |
| Hogan tights | 16 / 27 / 11 | 0.548 / 0.506 / 0.604 | 11.3 / 15.0 |
| Cena shirt | 80 / 95 / 90 | 0.408 / 0.356 / 0.387 | 17.7 / 18.4 |
| Cena jeans | 36 / 102 / 97 | 0.299 / 0.152 / 0.166 | 23.0 / 22.8 |
| Cena face/arm | 32 / 84 / 77 | 0.511 / 0.373 / 0.407 | 25.8 / 22.3 |

Native is lifted in the darks and less saturated everywhere, as section 2
predicts. The PWL remap moves every patch the right way, but on the ring
floor, jeans, face and crowd it closes little of the gap: the dark shading
(creases, floor shadow) is missing on Native, which a curve cannot restore.
A second cause in lit 3D draws looked likely here, but the diagnosis below
shows these frames were confounded by timing; it is not established. Caveats: hand-played frames are near,
not identical, moments; the reference arena frame has pyro lighting.

## Diagnosis (2026-10-10): the gap was mostly timing, not shading

Scripted headless runs of `tests/scenes/exhibition-1v1.toml`, Native against
the Emulated oracle. Outputs in `analysis/scenes/shade-loop/` (not committed).

- **Frame 6600 is inside a fade-in from black.** A Native sweep (6540..6660,
  step 10) gives frame means 1, 15, 30, 41, 56, 65, 70, 72, 73, 72, 72; the
  oracle's 6600 is 41.4. Load time varies by a few frames between runs, and
  debug cvars or tracing slow Native further, so a fixed frame number lands at
  a different fade level. The earlier "glow composite adds 26 levels of haze"
  reading (default 66.5 vs composite skipped 40.2) compared two different
  moments of the fade and is **withdrawn**. Native does draw the pyro glow
  (sweep frame 6600 shows the same soft blob as the oracle).
- The hand-played patch table above is affected the same way: near, not
  identical, moments, with fades and pyro lighting changing brightness fast.
- The one fully matched, steady lit-3D frame, the main menu wrestler at frame
  4750 (`shade-loop/emulated` vs `shade-loop/native`, downscaled to 1280x720),
  matches: torso p10 47 vs 48, mean 136.8 vs 136.8; trunks p10 16 vs 16, mean
  104.2 vs 102.2; menu panel MAE 0.8.
- So no second cause beyond the sRGB-vs-PWL curve is established. The curve
  difference itself is still real (section 2) but has not been isolated in a
  matched in-match frame.
- The arena composite (`387e0fcaec7b5894`, HLSL
  `cache/shader-native/hlsl/ps_382f4f7c1cdcb46e.hlsl`) is depth fog plus
  depth-scaled glow: `SceneDepthTexture` (k_24_8_FLOAT depth resolve, found by
  Native via `ToPhysical`'s 0x1000 offset) and `BlurredImage`, added with
  SRC_ALPHA, ONE.
- Pitfalls for the next check: `wwe_native_debug_skip_ps` ignores `0x` hex
  (pass decimal; check `debug_cvars` in report.json); `wwe_native_debug_resolve`
  takes a texture base address, not the resolve's object address; compare only
  steady frames (no fades, no pyro), matched by content, not frame number.

## Xenia comparison and the fog pass (2026-10-10, later)

Owner screenshots from Xenia Canary (default config, D3D12; Win+PrtScn at
2560x1440, `analysis/color-xenia/`), compared with the owner's Native F12
captures (`analysis/color-play/`) downscaled to 1280x720:

- 2D UI matches (menu text panel mean 75.0 Native vs 77.2 Xenia, MAE 4.5), so
  the two capture paths are comparable.
- Main menu wrestler (no fog pass) matches Xenia: Hogan chest mean 116.8 vs
  126.0, p10 53 vs 55, saturation 0.57 vs 0.56. A PWL remap makes it worse.
- Entrance and arena shots (fog pass) do not: Xenia has darker backgrounds and
  a deeper purple Cena shirt (saturation about 0.52 vs 0.29; poses and zoom
  differ, so the numbers are rough).

Matched scripted frames at Hogan's entrance (Native default `native-arena`
7200, Native with `387e0fcaec7b5894` skipped `n7200-skip` 7210, matched by
edge correlation 0.34):

| Region | normal − skipped (Native) |
|---|---|
| Hogan (near) | +3 to +4 |
| far dark wall | +13 |
| far left wall (glow of the yellow wall) | +40 (R +69, B +2) |
| roof truss (fog, neutral) | +38 |

- The pass is distance-based on Native, so the depth resolve and its decode
  work.
- Frame mean: Native 52.6, PWL-remapped 43.3, Native with the pass skipped
  24.5 to 32 over the matching frames, Emulated 24.0 and 29.8 (two runs).
  Native's fog/glow pass adds clearly more than the Emulated path does, and
  the curve explains only part of it.
- Unresolved: why. Candidates: the pass's constants (`FogParam`, `FogColor`,
  `BloomParam`, `BloomControl`, `MinZ_MaxZRatio`) or depth values differ
  between renderers; the glow chain's gamma-fetch decode or threshold differs;
  blending of the additive pass. Next probe: dump the pass's constants on both
  renderers at a matched entrance frame (`--wwe_native_trace_ps` on Native;
  Emulated needs an equivalent) and compare the depth values.
- Timing: tracing delays Native by 60+ frames at 7200, so constants must be
  captured in a run whose frames are matched by content afterwards.

## Fade-free check (2026-10-10, ticket 01): the fog pass is fine

The "fog pass adds too much on Native" reading above is **withdrawn**. The
game fades through a post pass (`842752c38494bfb1`: `saturate(rgb * a) *
ColorScale`, lerped to `OverlayColor`) that runs after the scene is resolved
to `0x17F80000`. The Emulated oracle's frame 7200 is mid-fade (0.6x its own
scene surface), so it cannot be compared with Native frames at another fade
level. The pass's constants are fixed for the whole run, its modelled output
from Emulated's inputs matches Native's measured contribution, and the
pre-fade scene surfaces of both renderers, matched by content, differ by only
a few levels (Native above in the darks, a full PWL remap a little below).
That residual is the sRGB-vs-PWL curve of section 2. Details and numbers:
`.scratch/colour-original/issues/01-fog-glow-pass-brighter-on-native.md`,
Answer.

## Open questions

- Default: Enhanced, today's look and the owner's sign-off in ticket 39, or
  Original, the oracle and Xenia look? CONTEXT.md says each enhancement's
  default is the owner's call.
- Is A's approximate blending close enough, or does the owner want B's exact
  blending?
- Is the "less vibrant" impression entirely the sRGB curve? It is the only
  colour difference found in code, but no matched Native/Xenia capture pair
  exists yet. A dark-scene pair (arena, entrance) would confirm it.
- The game's actual DC_LUT ramp values (**unverified**). They do not affect
  this comparison, since all three renderers share them.
