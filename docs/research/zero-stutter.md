# Zero stutter: pipelines, pacing and build speed

Date 2026-10-04. This study feeds `.scratch/zero-stutter/spec.md` (Solution
item 1). It covers pipeline precompilation, pipeline caches, frame pacing,
build options and other common stutter causes. Every claim cites a primary
source: the Vulkan registry and proposals, vendor documents and release notes,
or the source of DXVK, vkd3d-proton, UnleashedRecomp, Dolphin, Xenia, yuzu,
Ryujinx, RPCS3, Fossilize and LLVM.

Pinned upstream commits:
- DXVK `d30be2b`, written `DXVK path#Lx` (https://github.com/doitsujin/dxvk/blob/d30be2baea02a67b9502bffda0ec8b0d915bb3d2/).
- vkd3d-proton `31d1f89`, written `VKD` (https://github.com/HansKristian-Work/vkd3d-proton/blob/31d1f89ca5b3f2fd3b6f025c8e6f73ed3eaa852b/).
- Vulkan-Docs `e4e53e4`, written `VD` (https://github.com/KhronosGroup/Vulkan-Docs/blob/e4e53e4b31e13eeaee1ad99fb940aa72b2ec1b14/).
- UnleashedRecomp `cf829a9`, written `UR` (https://github.com/hedge-dev/UnleashedRecomp/blob/cf829a9eca8fb680fba4b0409ddeb6ca92f22e3c/).
- xenia-canary `7347cd1`, written `XC` (https://github.com/xenia-canary/xenia-canary/blob/7347cd12ef6e3029db3abcf17a0760d9f74f84bd/).
- Dolphin `eb23646`, written `DOL` (https://github.com/dolphin-emu/dolphin/blob/eb236466c4f0ec8bef619add0972353d3996fe6c/).

Paths in ReXGlue are relative to `.tools/rexglue-source` (`f5337cdc` plus
`patches/`). A finding marked **inferred** is not stated by a source.
**Unverified** means no primary source was found.

The owner's machine: AMD Radeon RX 7900 XT on the AMD proprietary driver. It
reports Vulkan API 1.4.349 and driverVersion `0x80018B`
(`analysis/capture-game-20261004-102736/runtime.log`). In AMD's encoding
(major<<22 | minor<<12 | patch) that is **2.0.395**, the scheme gpuinfo shows
for AMD Windows drivers.

## Answer

The spec's direction holds. Every target driver family has
`VK_EXT_graphics_pipeline_library` with fast linking, so the library path is
the main path, not an extra. Five points change details of the spec (see
"Contradictions with the spec" below). One build finding stands out: the
10,790 PPC `fmadd` sites in the generated code are calls into the CRT's `fma`
today, and an `x86-64-v3` build turns each into one instruction.

Recommendations, mapped to the spec's Solution items:

1. **Study (this document).** Each measurement named below is the gate for
   its fix.
2. **Pipelines prebuilt before play.**
   - **Library path, the default.** Split the pipeline the way DXVK does:
     - a vertex-input library: topology, restart, strides and attributes,
       with strides dynamic;
     - a pre-raster library: VS plus the spec constant, with cull mode and
       front face dynamic;
     - a fragment-shader library: PS plus the spec constant, with depth and
       stencil dynamic;
     - a fragment-output library: colour and depth formats, blend and write
       mask.

     All of these dynamic states are core Vulkan 1.3 (EDS1 and EDS2). Do not
     make blend or write mask dynamic through EDS3. DXVK keeps them in the
     output library, and AMD advises against dynamic states that do not
     change. Fast-link on a miss. Build the optimized pipeline on low-priority
     workers and swap it in. Use either a link with
     `LINK_TIME_OPTIMIZATION_BIT` (as vkd3d-proton does) or a fresh
     monolithic compile (as DXVK does). Measure both on the 7900 XT.
   - **Shipped key list as well.** On the library path the key list is still
     what lets the *optimized* pipelines be built before play, because a
     fast-linked pipeline can cost up to 50 % more GPU time. On GPUs without
     the library extension it is the only path. UnleashedRecomp ships the
     same kind of list (hashes plus render state, about 200 entries), and
     Dolphin's `.uidcache` is the same idea.
   - **The prepare step** is Dolphin's "Compile Shaders Before Starting" made
     mandatory, with progress like yuzu's. Use every core minus one, as Xenia
     does, at below-normal priority.
   - **On a miss:** fast-link, never skip. Count each miss the way Unreal
     counts "Missed" and "Too late".
3. **Shader modules eager.** On drivers with `VK_KHR_maintenance5` (core in
   1.4; AMD has had it since 23.12.1), chain the SPIR-V into the stage create
   info and drop the long-lived `VkShaderModule`. On the library path, each
   shader is compiled exactly once, into its library, on a worker. The
   per-module lock and the lazy creation then disappear. Keep the eager
   module creation from the spec as the fallback for drivers without
   maintenance5.
4. **The cache in the cache folder, tagged.** Write our own header before the
   driver blob, as zeux and vkd3d-proton do:
   - magic and version;
   - vendor, device, driverVersion and `pipelineCacheUUID`;
   - data size and a hash.

   Never pass the blob on a mismatch, write it with an atomic rename, and
   measure hits with `VK_EXT_pipeline_creation_feedback`. Use
   `FAIL_ON_PIPELINE_COMPILE_REQUIRED` to probe for a hit without stalling.
   Do not adopt `VK_KHR_pipeline_binary` now: there are no Intel Arc reports,
   and the driver cache plus a `VkPipelineCache` covers the need.
5. **Arena intro at 60 Hz.** Nothing here changes the plan: profile with
   Tracy first. Two inferred contributors are worth checking in that
   profile:
   - the `fma` calls, if Bink's IDCT or colour conversion uses `fmadd`;
   - synchronous `NtReadFile` on the loader thread.
6. **Faster code.** Measure in this order:
   1. `x86-64-v3` with `-ffp-model=strict`. Under strict, clang keeps
      contraction off, so PPC `fmul`+`fadd` pairs stay unfused.
   2. `-O3` against `-O2`. Expect a small difference.
   3. ThinLTO through lld-link with `/lldltocache`.
   4. IR PGO (`-fprofile-generate` / `-fprofile-use`).

   BOLT is ELF only, so it is out. `target_clones` costs a resolver call per
   call on COFF and does not suit generated code. Keep `-ffp-model=strict`.
   `x86-64-v3` covers 97.7 % of Windows Steam users (AVX2, September 2026),
   which is close enough that a v3 exe with a v2 fallback is worth measuring.
7. **Best-in-class pacing.**
   - **Present wait.** The owner's AMD driver has `VK_KHR_present_id2` and
     `VK_KHR_present_wait2` (Adrenalin 25.10.2 and later). AMD has never had
     v1 `present_id` / `present_wait`. Wait on present N−1 before releasing
     guest frame N+1: that keeps one frame queued, the waitable-swapchain
     default. Log the wait's completion time in the swap log.
   - **Treat the timestamp as feedback, not as scan-out.** The spec only asks
     drivers to come close to the first pixel, and Khronos itself says
     present_wait lacks the accuracy for pacing. `VK_EXT_present_timing` is
     the real tool, but today only NVIDIA developer drivers have it.
   - **Thread priority.** Put the pacing thread in MMCSS "Games". Set
     process HighQoS and turn off `IGNORE_TIMER_RESOLUTION` with
     `SetProcessInformation`.
   - **Timer.** Wake about 1 ms early and spin on QPC to the deadline, as
     DXVK's `util_sleep` does.
   - **Check the 360 Hz case first (inferred).** On fixed refresh, vblank +
     5 ms lands only 0.56 ms before a 360 Hz refresh. Any later present
     shows 2.78 ms late.
   - **Pass bar (PresentMon):** `MsBetweenDisplayChange` is 16.67 ms on
     ≥ 99.9 % of frames, animation error is about 0, and the mode is
     Independent Flip.
   - Skip Anti-Lag 2 and Reflex.

### Contradictions with the spec

1. **"One library per shader."** Specialization constants "must be provided
   alongside the shader stages using them and cannot be provided at link
   time" (VD `proposals/VK_EXT_graphics_pipeline_library.adoc` L612-620). The
   key's one `spec_constants` word is shared by VS and PS
   (`src/native/pipelines.cpp:194-205`). So a library is per (shader, spec
   value), not per shader. The plan and its Catch2 test ("one library per
   shader") must use that pair.
   - Alternative: DXVK reads spec constants from a uniform buffer in base
     pipelines (DXVK `src/dxvk/dxvk_context.cpp#L7302-L7305`). That would
     need a change to XenosRecomp's output.
   - Ideally each stage gets only its own bits. Today both stages get the
     whole word.
2. **"Present-id and present-wait."** On the owner's AMD driver only the v2
   extensions exist. The AMD Vulkan release notes list
   `VK_KHR_present_id2`/`present_wait2` under 25.10.2 and never list v1, and
   gpuinfo has no AMD-proprietary v1 reports
   (https://www.amd.com/en/support/kb/release-notes/rn-rad-win-vulkan). The
   presenter must implement v2 first and v1 second, the reverse of the
   spec's wording. DXVK disables present_id2 on NVIDIA below 595
   (DXVK `src/dxvk/dxvk_device_info.cpp#L604-L620`).
3. **"Uses the display's own feedback for pacing."** Present-wait's timestamp
   is not a scan-out time (see D.1). It is good for limiting queued frames
   and for logging. Pacing from it needs a phase estimate over many frames,
   not a per-frame target.
4. **"Every vertex and pixel shader in the pack is compiled into its
   library."** Where depth and stencil live depends on dynamic state. Without
   EDS1 dynamic depth/stencil, `VkPipelineDepthStencilStateCreateInfo`
   belongs to the *fragment-shader* library (proposal L225-300), and the
   per-shader library count would multiply.
   - The plan therefore needs the EDS1 depth/stencil states. They are core in
     Vulkan 1.3, which every target has.
   - The depth-only key (`pixel_shader == 0`) needs a fragment-shader library
     with no stage. Whether every driver accepts that is **unverified**.
     Build one per depth-only variant and test it.
5. **Device extensions are not enabled today.** ReXGlue's device creation
   requests only a fixed list (`src/ui/vulkan/vulkan_device.cpp:172-245`).
   That list has no GPL, no EDS3, no maintenance5, no present_id2/wait2 and
   no pipeline-creation features. Every pipeline and pacing item above needs
   a ReXGlue patch to request and enable these. This is not in the spec's
   ticket list.

The spec's out-of-scope note that `movement-judder.md` still says IMMEDIATE
is unchanged. FIFO is the default (spec, Further Notes).

## Support table

First driver with each extension. AMD, NVIDIA and Mesa dates come from their
release notes. Intel versions are first reports on gpuinfo, because Intel's
notes were not checked. Sources:
- AMD [A]: https://www.amd.com/en/support/kb/release-notes/rn-rad-win-vulkan
- NVIDIA [N]: https://developer.nvidia.com/vulkan-driver
- gpuinfo [G]: `https://vulkan.gpuinfo.org/listdevicescoverage.php?extension=<EXT>&platform=windows` (or `linux`)
- Mesa [M]: `https://docs.mesa3d.org/relnotes/<version>.html`

| Extension | AMD Windows | NVIDIA Windows | Intel Arc Windows | RADV / Steam Deck |
|---|---|---|---|---|
| graphics_pipeline_library (fastLinking true) | 24.2.1, 2024-02-26 [A] (gpuinfo 2.0.299 on 7900 XT) | 473.33 beta, 2022-03-29 [N] | 101.4499, 2023-06 [G] | default since Mesa 23.1, 2023-05-10 [M] |
| extended_dynamic_state3 | 23.3.2, 2023-03-22 [A] | 517.57, 2022-09-30 [N] | 101.4369; ColorBlendEquation 101.5330 [G] | Mesa 22.3 [M] |
| dynamicPrimitiveTopologyUnrestricted | from 2.0.388 (2026) [G] | from 517.57 [G] | from 101.8974, 2026-08 [G] | false on Deck [G] |
| vertex_input_dynamic_state | 23.3.2 [A] | 458.17, 2021-04-13 [N] | 101.3490, 2022-11 [G] | yes (Deck 23.2.99) [G] |
| shader_object | 24.9.1, 2024-09-30 [A] | 531.54, 2023-03-31 [N] | no reports [G] | Mesa 24.1 [M] |
| pipeline_binary | 24.9.1 [A] | 553.00, 2024-08-23 [N] | no reports [G] | Mesa 24.3 [M] |
| maintenance5 | 23.12.1, 2023-12-05 [A] | 532.34, 2023-07-28 [N] | 101.5522 [G] | yes [G] |
| present_id / present_wait (v1) | never [A][G] | 471.69, 2021-08-06 [N] | no reports [G] | yes, Mesa 23.x [G] |
| present_id2 / present_wait2 | 25.10.2, 2025-10-29 [A] | 573.38, 2025-06-08 [N] | no reports [G] | wait2 in Mesa 25.2 [M] |
| present_timing | not listed [A] | developer drivers [K] | unverified | "coming soon" [K] |
| AMD anti_lag / NV low_latency2 | 24.9.1+ [AL] | yes (Reflex) | n/a | n/a |

- [K]: https://www.khronos.org/blog/vk-ext-present-timing-the-journey-to-state-of-the-art-frame-pacing-in-vulkan (2025-12-04)
- [AL]: https://gpuopen.com/learn/integrating-amd-radeon-anti-lag-2-sdk-in-your-game/

**Inferred:** driver 2.0.395 is newer than 2.0.388, so it has GPL with
fastLinking and IndependentInterpolationDecoration, EDS3, maintenance5 and
present_id2/wait2, and probably `dynamicPrimitiveTopologyUnrestricted`. Which
Adrenalin release 2.0.395 corresponds to is **unverified**. Our device log
should print these features at startup so the owner's run confirms them.

**When an extension is missing.** DXVK uses GPL only when both
`graphicsPipelineLibrary` and `IndependentInterpolationDecoration` are true
(DXVK `src/dxvk/dxvk_device.cpp#L115-L122`). Otherwise it compiles full
pipelines. On gpuinfo Windows, fastLinking is false only on three "Vortek"
emulator entries [G]. Without present_wait (Intel Arc), the presenter keeps
today's timer pacing. The table shows Intel Arc as the one family without
present_id2/wait2.

## Evidence

### A. Graphics pipeline libraries and dynamic state

**The four parts.** VD `proposals/VK_EXT_graphics_pipeline_library.adoc`
L225-300:

| Library | Our key fields that land there |
|---|---|
| Vertex input interface | topology, primitive restart, `strides`, `attributes`, `dummy_locations` |
| Pre-rasterization shaders | VS, spec constant, `cull` (cull mode, front face), viewport state, layout |
| Fragment shader | PS, spec constant, `depth_control` (depth/stencil state); multisample only with sample shading |
| Fragment output interface | `color_format`, `depth_format` (`VkPipelineRenderingCreateInfo`), `blend_control`, `color_mask`, multisample |

**Linking.**
- `graphicsPipelineLibraryFastLinking` means a link "is comparable to
  recording a command in a command buffer" (proposal L145-150).
- `LINK_TIME_OPTIMIZATION_BIT_EXT` "should perform equivalently to a pipeline
  created monolithically", and needs every library built with
  `RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT` (L375-400).
- A fast-linked pipeline can carry "as bad as a 50% penalty". Vendors advise
  keeping such pipelines under 10 % of frame work (L595-610).
- The Khronos blog by Valve's Dan Ginsburg (2022-03-29) recommends a fast
  link right away and an LTO link on a background thread. It says NVIDIA, AMD
  and Intel desktop drivers expose fast linking
  (https://www.khronos.org/blog/reducing-draw-time-hitching-with-vk-ext-graphics-pipeline-library).

**DXVK.**
- **Shader libraries hold no render state.**
  - Pre-raster: viewport, scissor, depth bias, cull mode and front face are
    dynamic (`src/dxvk/dxvk_shader.cpp#L368-L376`).
  - Fragment-shader: depth test, write and compare, and stencil test, ops,
    masks and reference are dynamic (`#L453-L487`).
- **State libraries are small and built per state.** Vertex input has
  topology, restart and attributes, with dynamic stride. Fragment output has
  formats, blend and multisample (`src/dxvk/dxvk_graphics.cpp#L222-L255`,
  `#L490-L551`).
- **On a miss** it fast-links a base pipeline and queues the optimized one at
  low priority (`#L1097-L1127`).
- **The optimized pipeline is a fresh monolithic compile, not an LTO link**
  (`#L1454` onward; DXVK never sets `LINK_TIME_OPTIMIZATION`).
- **It falls back to a full pipeline** when the shaders need patching
  (`canCreateBasePipeline`, `#L1240-L1347`): dual-source blending, flat
  shading, VS inputs missing from the layout, FS inputs not written by the
  previous stage, sample-rate shading without dynamic multisample state, and
  similar cases.
- **Inferred:** our XenosRecomp shaders use none of the flat-shading or
  dual-source cases. Unwritten interpolators and dummy vertex locations
  (`dummy_locations`) are the cases to check.
- **History.** DXVK 2.0 (2022-11-10) moved to GPL and said it "largely
  replaces the state cache" (https://github.com/doitsujin/dxvk/releases/tag/v2.0).
  DXVK 2.7 removed the state cache and made `VK_KHR_maintenance5` required
  (https://github.com/doitsujin/dxvk/releases/tag/v2.7).

**vkd3d-proton** builds the full pipeline when the D3D12 PSO is created. It
sets `LINK_TIME_OPTIMIZATION` on the primary pipeline (VKD
`libs/vkd3d/state.c#L6799-L6803`). It builds libraries only for variants with
state that is unknown at creation (`#L6150-L6225`, `#L6940-L6955`). It makes
topology, primitive restart and vertex stride dynamic (`#L4424-L4446`).

**Dynamic state, field by field.**
- EDS1, all core in 1.3 (VD `appendices/VK_EXT_extended_dynamic_state.adoc`
  L32-38): cull mode, front face, topology within its class, vertex stride,
  depth test/write/compare, stencil test and op.
- EDS2's primitive-restart enable is also core in 1.3
  (`appendices/VK_EXT_extended_dynamic_state2.adoc` L25-33).
- EDS3 adds blend enable, blend equation and write mask
  (`proposals/VK_EXT_extended_dynamic_state3.adoc` L49-213).
- Formats are never dynamic.
- `VK_EXT_vertex_input_dynamic_state` makes the whole vertex input dynamic.
  DXVK does not use it. It uses a cheap vertex-input library plus dynamic
  stride instead. DXVK enables only six EDS3 states, none of them blend
  (`src/dxvk/dxvk_device_info.cpp#L969-L975`).

**Cost of dynamic state.**
- AMD's RDNA guide: "Avoid enabling dynamic states in a PSO that are not
  changing" (https://gpuopen.com/learn/rdna-performance-guide/).
- NVIDIA advises "a low number of dynamic states"
  (https://developer.nvidia.com/blog/vulkan-dos-donts/).
- No vendor names a specific cost for EDS1 on RDNA (**unverified**).
- **Inferred:** the EDS1 states DXVK uses are safe, because every DXVK game
  runs that way on RDNA.

**Shader objects.**
- `VK_EXT_shader_object` makes all state dynamic
  (https://www.khronos.org/blog/you-can-use-vulkan-without-pipelines-today).
- It has no Intel Arc reports [G]. Neither DXVK nor vkd3d-proton uses it.
- Its own proposal says apps wanting pipeline-like behaviour "should just use
  pipelines" (VD `proposals/VK_EXT_shader_object.adoc` L683).
- Not recommended.

### B. Key lists and precaching elsewhere

**UnleashedRecomp** compiles on asset load and from a shipped list.
- **Asset load.** A hook marks loaded models and materials. The game's own
  `CheckMadeAll` returns 0 until their pipelines are built, and
  `ExitLoading` waits on a counter (UR `UnleashedRecomp/gpu/video.cpp#L6944-L7034`).
- **Workers.** `max(2, cores*2/3)` threads at lowest priority, raised while
  loading (`#L6270-L6327`).
- **Shipped list.** `UnleashedRecomp/gpu/cache/pipeline_state_cache.h` holds
  about 200 entries of XXH64 shader hashes, vertex-declaration hash, blend,
  cull, depth, topology, strides and formats. It is compiled at the first
  `Present` (`video.cpp#L439-L441`, `#L7218-L7301`).
- **Why the list exists.** The README: post-process and 2D shaders "are not
  detected ahead of time, so a list of pre-determined pipelines are compiled
  during the game's boot sequence"
  (https://github.com/hedge-dev/UnleashedRecomp#asynchronous-shader-compilation).
- **How it is recorded.** A `PSO_CACHING` build writes missed pipelines to a
  text file on exit, with config-dependent fields masked out
  (`#L398-L401`, `#L6382-L6387`).
- **A miss** compiles synchronously on the render thread (`#L4134-L4142`).
- **No `VkPipelineCache`.** Its RHI, plume, passes `VK_NULL_HANDLE`
  (https://github.com/renderbag/plume/blob/11926860e878e68626ea99ec88562ce2b8badc4f/plume_vulkan.cpp#L1634).

**Unreal** (https://www.unrealengine.com/en-US/tech-blog/game-engines-and-shader-stuttering-unreal-engines-solution-to-the-problem,
Feb 2025):
- The bundled PSO cache was best practice until 5.2. It was dropped as
  default because gathering it is costly and it misses PSOs in dynamic
  worlds.
- Precaching on object load replaced it. Epic still advises "a very small PSO
  bundled cache" for global shaders
  (https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine).
- A precached object can wait (skip) or use a default material
  (`r.PSOPrecache.ProxyCreationDelayStrategy`).
- **Inferred:** our game is a fixed set of arenas and wrestlers with no user
  content, so a recorded list covers far more of it than it can for
  Fortnite.

**Dolphin** (https://web.archive.org/web/2024/https://dolphin-emu.org/blog/2017/07/30/ubershaders/):
- Hybrid ubershaders render a new configuration at once and swap in the
  specialized shader when it is ready.
- "Compile Shaders Before Starting" waits for every UID in
  `{GameID}.uidcache`, which holds configuration only, no shader code
  (DOL `Source/Core/VideoCommon/ShaderCache.cpp#L57-L75`, `#L795-L866`;
  `Source/Core/DolphinQt/Config/Graphics/GeneralWidget.cpp#L297-L302`).
- **Inferred:** GPL fast linking gives us what Dolphin's hybrid mode gives,
  without writing an ubershader.

**Xenia canary.**
- It stores guest microcode (`.xsh`) and pipeline descriptions (`.xpso`)
  and rebuilds them at title start on up to cores − 1 threads
  (XC `src/xenia/gpu/d3d12/pipeline_cache.cc#L249-L330`). Vulkan does the
  same since 2026-01-16 (`src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc#L3316`).
- It keeps a version constant, `PipelineDescription::kVersion`
  (`d3d12/pipeline_cache.h#L249`).

**yuzu (eden fork) and Ryujinx.**
- yuzu keeps keys (`vulkan.bin`) apart from the `VkPipelineCache`
  (`vulkan_pipelines.bin`), and builds every key at boot with a progress
  callback.
- It skips keys whose dynamic-state flags do not match the device, so the
  file stays portable
  (https://github.com/eden-emulator/mirror/blob/d16735f5b618942136d6ab53466e3be0a382c30a/src/video_core/renderer_vulkan/vk_pipeline_cache.cpp#L611-L707).
- Ryujinx logs "Background pipeline compile missed on draw"
  (https://github.com/NextendoNetwork/Ryujinx-Nextendo/blob/05577ab543f4322cb7534c1f5caca0e0079791bf/src/Ryujinx.Graphics.Vulkan/ShaderCollection.cs#L677-L680).

**Fossilize**, Valve's pre-caching format, records the create infos *and*
SPIR-V, and replays them on any device
(https://github.com/ValveSoftware/Fossilize/blob/556da9ff7d7616704dda515756219ab1dab693ec/README.md).
Our list is the same model without the SPIR-V. The player's machine supplies
the SPIR-V (ADR-0012).

**RPCS3** offers "async" (skip until compiled) and "async with interpreter"
modes (https://github.com/RPCS3/rpcs3/blob/46aee28f85b9cecc8fd0e5dee0c771f9c88cd27a/rpcs3/rpcs3qt/tooltips.h#L205-L208).

**Legal angle.**
- No source discusses whether shipping hashes and render state is a problem.
  UnleashedRecomp says it "does not include any game assets" (UR `README.md#L9`)
  and commits hashes, render state and vertex-element bytes.
- Dolphin declined to share UIDs for practical reasons, not legal ones (blog,
  "Sharing Shaders").
- Xenia's `.xsh` and Fossilize's `.foz` contain shader code, which ADR-0012
  rules out for us.
- **Inferred:** keys only, as the spec says, matches the most conservative
  practice among these projects.

### C. VkPipelineCache, feedback and threading

**Header and validation.**
- Incompatible initial data leaves the cache empty, and invalid data is
  ignored. The 32-byte header has vendorID, deviceID and pipelineCacheUUID
  (VD `chapters/pipelines.adoc` L8179-L8183, L8510-L8588).
- Our code relies on that (`src/native/pipelines.cpp:425-438`).
- vkd3d-proton still checks vendor, device and UUID itself before passing a
  blob (VKD `libs/vkd3d/cache.c#L373-L410`).
- Kapoulkine advises a prefix header with driverVersion, size and hash, plus
  atomic writes, because some drivers mis-check
  (https://zeux.io/2019/07/17/serializing-pipeline-cache/). He is a Roblox and
  meshoptimizer author, not a vendor, so this is practice, not authority.

**Driver caches.**
- AMD Adrenalin has its own shader cache
  (https://www.amd.com/en/resources/support-articles/faqs/dh3-012.html).
- Mesa caches on disk (https://docs.mesa3d.org/envvars.html).
- NVIDIA documents a disk cache only for GL; Vulkan is **unverified**.
- DXVK passes no `VkPipelineCache` at all
  (`src/dxvk/dxvk_graphics.cpp#L1407`, `#L1512`).
- **Inferred:** our app cache mostly duplicates the driver cache on AMD.
  Keep it, because it is cheap and covers drivers without a disk cache.
  Measure it with creation feedback (`APPLICATION_PIPELINE_CACHE_HIT_BIT`,
  `pipelines.adoc` L10909-L10920) before relying on it.

**Probing for a hit.**
- `FAIL_ON_PIPELINE_COMPILE_REQUIRED` (core in 1.3) returns
  `VK_PIPELINE_COMPILE_REQUIRED` instead of compiling.
- DXVK uses it to probe for a hit before it decompresses SPIR-V
  (`src/dxvk/dxvk_shader.cpp#L293-L302`).
- For us it lets the render thread ask "is this already in the driver cache?"
  without risking a stall.

**Pipeline binaries.** `VK_KHR_pipeline_binary`
(https://www.khronos.org/blog/bringing-explicit-pipeline-caching-control-to-vulkan,
2024-08-27) hands per-pipeline binaries to the app and can disable the
driver's internal cache. It is useful only if we want exact control. It has no
Intel Arc reports [G].

**Threading.**
- A pipeline cache is internally synchronized
  (`pipelines.adoc` L8114-L8127).
- AMD: its driver "will not spawn extra threads to compile". Share "a single
  PSO cache". Compiling pipelines that share shaders at the same time may
  serialize on a lock (https://gpuopen.com/learn/rdna-performance-guide/).
- DXVK runs one worker per core, capped at 64, all at the lowest thread
  priority. About 2/7 of them take low-priority (optimized) jobs
  (`src/dxvk/dxvk_pipemanager.cpp#L87-L121`).
- Ours runs 2–4 workers (`src/native/pipelines.cpp:418`). That is right
  during play. The prepare step should use every core.

**Shader modules.** With maintenance5, `VkShaderModuleCreateInfo` can be
chained into the stage create info, and the proposal calls shader modules
deprecated (VD `proposals/VK_KHR_maintenance5.adoc` L282-290).

### D. Frame pacing

**D.1 Present wait.**
- `vkWaitForPresentKHR` returns when the present id is reached. The spec
  says only that it "should" be "as close as possible to the presentation of
  the first pixel", with "no requirement for any precise timing relationship"
  (https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresentKHR.html).
- The v2 extensions moved support to a surface capability and dropped the
  time-to-light wording
  (https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_wait2.html;
  VD `proposals/VK_KHR_present_wait2.adoc`).
- `VK_EXT_present_timing`, ratified 2025-12-04, reports present stages up to
  first pixel visible and VRR. Khronos says present_wait "lacks sufficient
  accuracy" for pacing (blog [K];
  `proposals/VK_EXT_present_timing.adoc`).

**DXVK's presenter.**
- It waits for presents on a separate thread, in FIFO only. It prefers v2 and
  falls back to v1 (`src/dxvk/dxvk_presenter.cpp#L1975-L2017`, `#L204-L208`).
- It then sleeps to align with scan-out and signals the latency event
  (`#L2026-L2050`).
- Its present-timing mode targets "60 Hz on a high-refresh rate display"
  (`dxvk.conf` L537-542). It snaps to a refresh multiple within 1 % on fixed
  refresh and uses relative timing on VRR (`#L1465-L1479`). That is the same
  idea as ADR-0016.
- It uses `minImageCount + 1` images (`#L1297-L1305`).

**D.2 Latency.**
- DXGI's waitable swapchain defaults to a maximum latency of 1
  (https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency).
  Microsoft advises 2 only when CPU and GPU each fit a frame but their sum
  does not
  (https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains).
- The Vulkan equivalent is to wait for present N−1 before starting frame N+1.
- `VK_AMD_anti_lag` (Adrenalin 24.9.1+) and `VK_NV_low_latency2` pace a
  GPU-bound CPU that runs ahead
  (https://docs.vulkan.org/refpages/latest/refpages/source/VK_AMD_anti_lag.html,
  https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_low_latency2.html).
- **Inferred:** they add little here. The guest runs a fixed step paced on
  the guest vblank and reads input inside guest code.

**D.3 Present modes and VRR on Windows.**
- FIFO queues, MAILBOX keeps one entry, and FIFO_LATEST_READY takes every
  ready request at each vblank
  (https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html).
- For DXGI, Microsoft says VRR needs tearing allowed (sync interval 0)
  (https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays).
- A flip-model borderless window can reach Independent Flip, "the same
  efficiency as fullscreen exclusive"
  (https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model).
- How AMD's Vulkan FIFO maps onto this, whether it shows a frame on arrival
  under FreeSync, and whether it goes through DXGI is **unverified**. AMD
  publishes nothing. PresentMon's `PresentMode` column settles it on the
  owner's machine.

**The 360 Hz margin (inferred).** At 360 Hz a refresh is 2.78 ms, so guest
vblank + 5 ms is 0.56 ms before the second refresh after it. On fixed refresh,
a present that is 0.6 ms late shows one refresh later: 19.44 ms, then
13.89 ms. Under VRR the display follows the present time directly. In both
cases the margin and the timer's jitter matter, and the swap log's
`arrival_after_vblank_us` plus present-wait times will show them.

**D.4 Windows timing.**
- `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` exists since Windows 10 1803, with
  no stated resolution
  (https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw).
- `timeBeginPeriod` has been per process since Windows 10 2004. On Windows 11
  it is not guaranteed for an occluded or minimized window
  (https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod).
- `SetProcessInformation(ProcessPowerThrottling)` can request HighQoS and
  make Windows "always honor" timer requests
  (https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation).
  **Inferred:** this also covers the power-plan and core-parking worries for
  our threads.
- **MMCSS.** Categories are High 23–26, Medium 16–22 (foreground) and Low
  8–15. "GPU Priority" is "not yet used" and "SFIO Priority" is "not used"
  (https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service).
  The owner's registry has "Games" at Medium. Microsoft does not publish the
  defaults.
- **Do not use `TIME_CRITICAL` or REALTIME.** REALTIME starves input threads
  (https://learn.microsoft.com/en-us/windows/win32/procthread/scheduling-priorities).
- **DXVK's sleep** raises timer resolution with `NtSetTimerResolution`, sleeps
  until a threshold, then spins on QPC (`src/util/util_sleep.cpp#L30`,
  `#L43-L104`). It keeps an absolute schedule so error does not build up
  (`src/util/util_fps_limiter.cpp#L67-L72`).
- **Context.** The SVR2011 port gained from `timeBeginPeriod(1)`
  (`docs/research/svr2011-pc-port.md`, Performance). Our own measurement
  found `Sleep(1)` at 1.08 ms (`entrance-video-stutter.md` §F).

**D.5 Metrics.**
- PresentMon:
  - `MsBetweenDisplayChange` is how long the previous frame stayed on screen.
  - `MsAnimationError` is the simulation step minus the display step.
  - `PresentMode` names the flip mode.

  (https://github.com/GameTechDev/PresentMon/blob/00db2cab/README-ConsoleApplication.md,
  L95-145; `AnimationErrorTracker.cpp#L119-L130`.)
- CapFrameX:
  - "1 % low average" is 1000 / the mean of frame times at or above the 99th
    percentile.
  - "1 % low integral" is the frame time at which the slowest frames sum to
    1 % of total time.

  (https://github.com/CXWorld/CapFrameX/blob/9f488ea6/source/CapFrameX.Statistics.NetStandard/FrametimeStatisticProvider.cs#L257-L386.)
- The smoothness report should name which of these it computes. The spec's
  "1 % low no worse than 16.7 ms" fits either one if computed over
  displayed times.

### E. Build options

The context below was checked locally:
- `scripts/build.ps1:73` sets `-march=x86-64-v2`.
- ReXGlue adds `-fno-strict-aliasing -ffp-model=strict` and `-O3`
  only in the Release config (`CMakeLists.txt:131-139`). RelWithDebInfo
  therefore gets CMake's `-O2`.
- **Correction (ticket 11):** those are `add_compile_options` in ReXGlue's own
  directory, so they reach only the SDK. The generated code and the game are
  built with clang's defaults (`-ffp-model=precise`, strict aliasing on).
  Ticket 11 added `-ffp-contract=off` to both; the rest is ticket 16.
- `generated/default` has 109,145 functions in 279 files and 10,790
  `std::fma(` calls.

**FMA.** Compiled with the project's clang 21.1.8 on a test file:

| Flags | `std::fma(a,b,c)` | `a*b+c` |
|---|---|---|
| v2, strict or precise | `call fma` (CRT) | separate mul and add |
| v3, strict | one `vfmadd213sd` | separate (correct) |
| v3, precise | `vfmadd213sd` | **fused (wrong for PPC `fmul`+`fadd`)** |
| v3, precise + `-ffp-contract=off` | `vfmadd` | separate (correct) |

- On x64 the UCRT picks an FMA3 implementation at startup when the CPU has it
  (https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/get-fma3-enable-set-fma3-enable).
  That page names "transcendental" functions, so whether `fma` itself uses
  the instruction is **unverified**.
- Either way, today each `fmadd` costs a function call.
- **Hypothesis, medium:** v3 is the largest single CPU gain available.
- Strict sets contraction off (https://clang.llvm.org/docs/UsersManual.html,
  `-ffp-model`), so v3 under strict stays exact.
- Strict also emits constrained FP intrinsics, which block transformations
  that depend on rounding mode (https://llvm.org/docs/LangRef.html,
  constrained floating-point intrinsics).
- Dropping strict for `precise -ffp-contract=off` would need an audit of how
  ReXGlue handles MXCSR first. XenonRecomp switches denormal flushing per
  instruction (https://github.com/hedge-dev/XenonRecomp/blob/main/README.md).
  The spec keeps strict, which this supports.

**Adoption.** Steam Hardware Survey, September 2026, Windows: AVX2 97.68 %,
FMA 97.84 %, BMI2 97.64 %, AVX 99.28 %, SSE4.2 99.90 %
(https://store.steampowered.com/hwsurvey/?platform=pc). UnleashedRecomp
already requires AVX (`-march=sandybridge`, UR `CMakeLists.txt:36-41`;
`README.md:28-30`).

**-O3 over -O2 in LLVM 21**
(`llvm/lib/Passes/PassBuilderPipelines.cpp`, release/21.x):
- O3 adds argument promotion (L975-978), call-site splitting (L1124-1125)
  and non-trivial loop unswitching (L675, L685, L1353).
- The inline threshold rises from 225 to 250 (`InlineCost.cpp:78`).
- The unroll threshold rises from 150 to 300 (`LoopUnrollPass.cpp:167-172`).
- **Inferred:** generated straight-line code with `(ctx, base)` arguments
  benefits little. The measurement is cheap.

**ThinLTO.**
- It merges only summaries at link time and runs the backends in parallel.
- lld-link supports it for COFF, with `/opt:lldltojobs=` and `/lldltocache:`
  (https://clang.llvm.org/docs/ThinLTO.html).
- The 2016 numbers put a ThinLTO build of clang close to a non-LTO build
  (https://blog.llvm.org/2016/06/thinlto-scalable-and-incremental-lto.html).
- PDB generation with ThinLTO on 300 MB of input is **unverified**. Measure
  link RAM and time.
- UnleashedRecomp's Release preset turns on full IPO (UR
  `CMakePresets.json:53-58`).

**PGO.**
- The clang manual: "For best performance with PGO, IR-based instrumentation
  should be used" (`-fprofile-generate` / `-fprofile-use`). There is a
  context-sensitive second round, `-fcs-profile-generate`. The runtime is
  `clang_rt.profile-x86_64.lib` (https://clang.llvm.org/docs/UsersManual.html,
  "Profile Guided Optimization").
- Sampling PGO on Windows needs DWARF and Intel SEP. Not worth it.
- A function whose CFG changes loses its profile (`hash_mismatch`,
  `PGOInstrumentation.cpp:1409-1430`).
- **Inferred:** address-based names keep most of a profile across codegen
  changes.
- lld-link already sorts by the PGO call graph (`/call-graph-profile-sort`,
  `lld/COFF/Options.td:299-309`).

**BOLT** "operates on X86-64 and AArch64 ELF binaries" (`bolt/README.md:12`).
It is not an option on Windows.

**Function multiversioning.** On COFF, `target_clones` goes through a
resolver on every call (`clang/lib/CodeGen/CodeGenFunction.cpp:2940-2961`).
That rules it out for 109k small functions.

**Other recomps.**
- XenonRecomp puts 256 functions in a file
  (`XenonRecomp/recompiler.cpp:2605-2611`).
- N64Recomp puts 50 functions in a file (`src/config.cpp:435-443`).
- Zelda64Recomp builds its recompiled functions with only
  `-fno-strict-aliasing` (`CMakeLists.txt:71-76`).

### F. Other stutter sources worth a ticket

1. **Dedicated `vkAllocateMemory` per texture and render target (inferred,
   medium).**
   - `CreateDedicatedAllocationImage` runs for every texture and target
     (`src/native/textures.cpp:215`; target views at
     `src/native/native_renderer.cpp:732`).
   - NVIDIA: "vkAllocateMemory() is an expensive operation on the CPU". Use
     sub-allocation (https://developer.nvidia.com/blog/vulkan-dos-donts/).
   - The number of live allocations has an implementation limit, and going
     past it is undefined
     (https://docs.vulkan.org/refpages/latest/refpages/source/vkAllocateMemory.html).
   - ReXGlue already ships VMA (`src/ui/vulkan/vulkan_mem_alloc.cpp`).
   - Ticket: log allocation time in the hitch line. If it shows, move texture
     memory to VMA.
2. **Readbacks** allocated a host buffer each time
   (`entrance-video-stutter.md` §B). They are off by default now
   (`wwe_native_resolve_readback`, `src/native/native_renderer.cpp:93`).
   If they are ever turned back on, use a ring.
3. **Synchronous guest file reads** (`NtReadFile` on the calling thread,
   `src/kernel/xboxkrnl/xboxkrnl_io.cpp:187-240`). Check them in the Tracy
   profile of the intro and the load into the match (spec item 5).
4. **Bink 1 decode in recompiled guest code**
   (`entrance-video-stutter.md` §C).
   - It probably benefits from v3 and from LTO across the `BINK` section's
     functions (**inferred**).
   - A host Bink decoder would replace guest code, which the manifest's hook
     rules allow only with instruction-level evidence. Not recommended before
     the profile.
5. **Power and scheduling.** HighQoS through `SetProcessInformation` (D.4)
   is the documented way to keep Windows from throttling the process. No
   further power-plan changes are recommended. Changing the user's power plan
   is outside an app's remit.
6. **The guest GPU-wait spin** at `0x82DB6710` costs one host core
   (`entrance-video-stutter.md` §F). It does not lengthen frames on the
   7900 XT. It matters on 4-core machines and the Steam Deck. SVR2011's yield
   halved its render thread's CPU use (`svr2011-pc-port.md`, Performance).

## Measurements that gate each fix

| Fix | Measurement |
|---|---|
| Library path | Startup log of GPL, fastLinking and EDS features. Counts of fast-linked draws and optimized swaps (spec story 43). GPU time per frame with fast-linked against optimized pipelines on the route. |
| Key-list prebuild | `pipeline_pending` = 0 and render-thread compiles = 0 on a fresh setup. Prepare duration on all cores. |
| Cache tags | A creation-feedback hit rate on the second run, and a forced mismatch that is not loaded. |
| `x86-64-v3`, -O3, ThinLTO, PGO | Guest ms per frame on the intro and the route, from Tracy or `perf.csv`. Build time and peak link RAM. Every scene against its oracle. |
| Present wait | Swap-log wait times, queued frames, PresentMon `MsBetweenDisplayChange` and animation error, with FreeSync on and off. |
| MMCSS, HighQoS, hybrid spin | Pacing jitter (`arrival_after_vblank_us` spread) with a CPU load running in the background. |

## Open

- Which Adrenalin release is 2.0.395, and whether present_wait2 reports
  support on the owner's surface: read it from our own startup log once the
  features are requested.
- Whether AMD Vulkan FIFO in a borderless window is Independent Flip and
  follows FreeSync: PresentMon on the owner's machine.
- Whether a fragment-shader library with no stage (depth-only keys) works on
  AMD, NVIDIA and Intel.
- Whether the UCRT `fma` uses the FMA3 instruction. A v3 build makes the
  question moot.
