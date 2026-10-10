# ADR-0019: Native pipelines are prebuilt before play from a shipped key list

Status: accepted (2026-10-04). Amends ADR-0012.

## Context

ADR-0012 removed shader conversion from play, but Native still compiled
pipelines on first use, during play. A pipeline it has never seen is
skipped (`pipeline_pending`) or compiled on the render thread. Either one
shows on screen. The player's own pipeline store covers only what that
player has already drawn on that GPU and driver
(`.scratch/zero-stutter/spec.md`, `docs/research/zero-stutter.md`).

## Decision

- **Pipelines are prebuilt before play.** At startup, the pipeline workers
  build a plan (`src/native/prebuild_plan.h`, a pure function). It takes the
  shader pack's shaders, the shipped and stored keys (each with its converter
  id) and the GPU's features.
- **Library path where supported.** On a GPU with
  `VK_EXT_graphics_pipeline_library` and fast linking, the plan adds one
  library per (shader, spec-constant value), beside one optimized pipeline
  per key. Every other GPU gets full pipelines per key. This ticket wires the
  full-pipeline path; tickets 08 and 09 add the libraries.
- **A shipped key list.** `data/pipeline-keys.txt` is committed and
  versioned. It ships beside the game exe as `pipeline-keys.txt`. It holds
  plain `PipelineKey`s only: shader container hashes, formats and render-state
  register values, one fixed-size hex line each. It holds no shader code, no
  SPIR-V and no game data, so ADR-0012 still holds.
  `scripts/pipeline_keys.py merge` adds the keys of the owner's session stores
  (`keys.bin`) to the list and is idempotent. Coverage grows from hand
  sessions, not scripted runs.
- **Keys that no longer fit are dropped and counted.** A key whose shaders
  are not in the pack is dropped, as is a key recorded with another converter
  id. Duplicates between the list and the store are merged. A local
  `build.ps1 -Action Build` runs `pipeline_keys.py check` against the dev
  shader pack and reports keys whose shaders are missing.

## Consequences

- A first run builds every key on the list before the first match, so that
  match has no compile skips for those keys. Keys the list lacks still
  compile on a worker and join the player's store.
- A XenosRecomp patch change (a new converter id) voids the list until it is
  merged again from stores made with the new patch.
- The list grows with the owner's sessions and needs a merge before each
  release.

## Amendment (2026-10-10): retag, and the Colour setting

- **A patch change that keeps every key's meaning retags the list.**
  `scripts/pipeline_keys.py retag <list> <old id> <new id>` moves the list
  to the new converter id without touching its keys. It is for a patch that
  only adds what no listed key uses, such as colour-original ticket 02's
  `SPEC_CONSTANT_PWL_GAMMA_OUTPUT` bit. Any other patch change still voids the
  list until it is merged again.
- **The plan follows the Colour setting.** `MakePlan` takes the session's
  colour mode. A key into a gamma target (`R8G8B8A8_SRGB`) gets that mode's
  PWL output bit, whichever mode recorded it, so the shipped list (recorded
  with sRGB colour, now called Natural) also covers Original. Natural builds no
  PWL output libraries.

