# XenosRecomp fixes to port from wwe-svr2011-pc

Ticket: `.scratch/playable-build/issues/08-xenosrecomp-fixes-to-port.md`.
Date: 2026-10-03.

## Sources

- **Theirs (RP):** wwe-svr2011-pc `port/patches/xenosrecomp-svr2011.patch` at
  `de85161` (v2.0.0). The patch is applied to hedge-dev XenosRecomp, the
  Unleashed base.
- **Ours (OP):** `patches/xenosrecomp-wweallstars.patch`, applied to the
  re:Blue fork in `.tools/xenosrecomp`. Built with `-DSVR2011_RECOMP`
  (`scripts/build-xenosrecomp.ps1:37`), which turns on `XR_XENOS_FIXES`
  (OP L8–9).
- **Ours, patched source (OS):** `.tools/xenosrecomp/XenosRecomp/shader_recompiler.cpp`.
- **Reference semantics:** `.tools/rexglue-source` (Xenia-derived):
  `src/graphics/pipeline/shader/spirv_translator_alu.cpp:80-98` and
  `include/rex/graphics/format/ucode.h`.

The re:Blue base already contains several of the fixes that SVR had to add to
the Unleashed base. Our gap is therefore smaller than their patch suggests.

## Method

- Input: every container dumped so far. This is the union of
  `cache/guest-shaders`, `-capture`, `-new` and `-new2`: 1637 containers
  (300 VS, 1337 PS). `vs_e20973377b9b2417` is excluded because it crashes the
  tool. Hashes below are the dump file names, which are the FNV-1a of the
  container (`src/native/shader_library.cpp:47-57`).
- **The dumps do not cover all scenes.** No list maps the dumps to scenes, so
  a match may load shaders that are not here. Repeat the scan after the roster
  sweep.
- Tool: a throwaway copy of our patched XenosRecomp, built in the scratchpad
  with the flags from `build-xenosrecomp.ps1`. It writes `// XRSCAN` comments
  into the HLSL:
  - co-issue: the vector op writes a temp, and the scalar expression reads one
    of the components it wrote;
  - cexec-pred blocks: for each block, how many instructions inside are not
    predicated with the block's condition, and how many write `p0`;
  - PS constant arrays that cross c224;
  - `setp_inv` and `setp_*_push` uses.

  The produced HLSL was then grepped for `switch (pc)`, vertex texture fetch,
  sampler slots of 16 and above, and COLOR2–7 reads. Nothing in the repo was
  changed.
- 4 containers fail DXC as before: `ps_10399a8d65acd7f7`,
  `ps_40a6ea1273f5a0e4`, `vs_1e0900911d152ab9`, `vs_fa136f7a30253c7d`.

## Ranked list: missing from our patch

### 1. Co-issue order (scalar reads its sources before the vector write)

- **Status:** missing. This is the only missing fix that All Stars shaders hit.
- **Bug:**
  - OS emits the vector op and writes its destination first (OS L801–988).
  - Only then does it evaluate the scalar op (OS L1001–1228).
  - A scalar operand that names the vector's destination register therefore
    reads the new value.
  - On Xenos, both ops read their operands before either one writes. Xenia
    computes both results and only then stores them
    (`spirv_translator_alu.cpp:80-98`).
- **Hits:** 10 shaders, one instruction each.

  | Shader | What it is | Hazard |
  |---|---|---|
  | `vs_617942e5d86b89f5` | GPU skinning (`BoneMatrices`, `MaxBoneInfluences`) | `r1.z = max(r1.y,r1.y); ps = max(r1.z,r1.z)`: the bone index for one influence is read after it has been overwritten |
  | `vs_acc20ff94e73fab0` | GPU skinning | same |
  | `vs_f3fdb71a746b9874` | GPU skinning | same |
  | `ps_82e22013bfa71657` | UE3 lit material, spot light (`SpotAngles`, `LightPosition`) | `rsqrt(r8.x)` after `r8.xyz` is written |
  | `ps_a1983ca42b3ce0ef` | same family | same |
  | `ps_a727273b6e740e2c` | spot-light material | `r6.w` read after write |
  | `ps_aec60664087e0993` | spot-light material | `r6.y` read after write (spot cone) |
  | `ps_f3b3759a54ba06a5` | spot-light material | `r6.x` read after write |
  | `ps_30d9aee78a81c2b6` | lit material with lightmap and sky colours | `rsqrt(r5.w)` after `r5.xzw` is written |
  | `ps_38fed11e3ff6ad33` | lit material with sky colours | `r7.y` read after write |

- **Visible symptom:**
  - VS: vertices weighted to the affected influence take another influence's
    bone matrix. Wrestler meshes show spikes or stretching, worst in motion.
  - PS: wrong normalisation or attenuation, so spot and point light falloff,
    spot cones and specular are wrong on the affected materials.
- **Port:**
  - RP L150–482: `deferredA0` is declared at L154. The scalar block moves ahead
    of the vector block at L304. Scalar `a0` writes are deferred at L325–330,
    the vector block is re-emitted at L335–475, and the deferred `a0` is
    flushed at L477–481.
  - The `a0` deferral must come along with the reorder. Without it, a `maxas`
    would change `a0` before the vector op reads it. Today OS writes the scalar
    `a0` after the vector op (L1222, L1226).
  - The scan found no vector `MaxA` that is co-issued with a scalar reading
    `a0`.
  - An alternative is Xenia's approach: put the vector result in a temporary
    and store it after the scalar op. It is equally small and also covers a
    scalar `setp` that is co-issued with a vector `setp_*_push`.

### 2. Flow control as `if (pc <= N)` instead of switch fall-through

- **Status:** missing. The `switch (pc)` fallback is still at OS L2058–2065 and
  L2080–2084.
- **Hits:** 0 of 1637. The re:Blue recursive-descent structurizer
  (OS L1938ff) handles every dumped shader. No `switch (pc)` or
  `while (true)` appears in our output or in
  `cache/shader-native/hlsl`.
- **Symptom if it were hit:** some Vulkan drivers fail to create the pipeline
  (RADV/Proton crash in `vkCreateGraphicsPipelines`, RP commit `1a4d023`). The
  draw would go missing.
- **Port:** RP L618–644 and L720–727 (`1a4d023`). This is a safety net only, for
  a backward `cjmp` or an unmatched loop (OS L1898–1934).

### 3. Conditional exec on the predicate (`(p0) exec` / `(!p0) exec`)

- **Status:** missing. OS L2158–2164 runs the block unconditionally.
- **Hits:** 1959 cexec-pred blocks in 170 shaders (126 VS, 44 PS). In every
  valid shader, each instruction in the block is predicated with the block's
  own condition, and no block writes `p0`. The output is therefore identical.
  Blocks with unpredicated instructions occur only in the 4 rejected
  containers, which decode as garbage (undeclared `c1`, `r44`, `r52`), and the
  gate does not fix them.
- **Symptom:** none today.
- **Port:** RP L669–684 plus the block close at L700–706. This brings the code
  in line with the hardware semantics.

### 4. PS constant-array tail 224 → 256

- **Status:** missing. OS L1375 and L1467 still use `224` for pixel shaders.
- **Hits:** 0. No PS constant array crosses c224.
- **Symptom if it were hit:** array elements at c224 and above read as 0,
  giving wrong colours or parameters.
- **Port:** RP L496–502 and L522–528.

### 5. Not in their patch: `setp_*_push` destination (bug in both)

- **The bug:** OS L771–792 (and RP's code) set `p0` and then write
  `dest = p0 ? 0 : src0 + 1`. `ucode.h:1550-1564` says `p0` comes from the
  `.w` lanes, but `dest` is 0 only when `src0.x == 0 && src1.x == 0`. The
  translation tests the write-mask lane for both.
- **Hits:** `ps_74207a8f72ae8f0f` (depth-of-field/blur post: `FocusDistance`,
  `BlurKernelSize`), 15 instructions forming a counter chain.
- **Unverified whether visible:** the result only differs if the `.x` and
  `.w` source lanes differ. Check the swizzles before fixing.

## Already covered: no action

| SVR fix | RP | Ours | All Stars hits |
|---|---|---|---|
| `setp_inv` 1→0 | L311–317 (`6e4de40`) | OS L1145–1147 (re:Blue, enabled by OP L247) | 0 shaders use `setp_inv` |
| cexec on a bool constant | L647–667 | OP L408–420 | 88 shaders test bools |
| `cjmp` on an unnamed bool | L691 | OP L419 | present |
| PS tests its own booleans | L552–554 | unified file `BOOL_BIT(n+128)` (OP L73, L369–370; `src/native/vk_context.h:46`) | present |
| 32 fetch slots | L28–66, L505–614 | `SAMPLER_SLOTS = 32` (OP L189) | 0: the highest slot used is 15 |
| Vertex texture fetch via `SampleLevel` | L9–14, L76–113, L488–490 | `XR_SAMPLE` (OP L81–90) | 0 VS fetch textures |
| COLOR2–7 interpolators | L134–147 | OP L203 | 0 PS read COLOR2+ |
| 11:11:10 normals | L115–127 | OP (`tfetchR11G11B10`) | n/a |
| `g_NdcScale` | L35, L716, L733 | OP L43, L426–427 | n/a |
| `CondExecPredCleanEnd` return typo | L652 | OP L410 | n/a |
| `-DSVR_SPEC_CONSTANTS` variants | L38–45, L66–71 | not needed: Vulkan specialization (`src/native/pipelines.cpp:192-199`) | n/a |
| Bicubic `SampleLevel` | L84–98 | not needed: Unleashed-only path (OS L444–447) | n/a |

## Recommendation

- Port fix 1 now, with attribution to wwe-svr2011-pc (reuse permitted;
  `docs/research/svr2011-pc-port.md`). Verify it with a skinned wrestler close
  up and a spot-lit arena surface against the oracle frame.
- Fixes 2–4 are a few lines each and change nothing visible today. Port them
  in the same change for safety.
- Look at fix 5 separately. The 5 rejected containers are a separate decode
  issue and do not come from SVR's list.
