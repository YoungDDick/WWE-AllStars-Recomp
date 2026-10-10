# ADR-0012: Native shaders: the launcher pre-converts them

Status: accepted (2026-10-02)

## Context

Game-derived shaders cannot be shipped.

## Decision

On first setup, the launcher extracts shader containers from the XEX and packages and runs XenosRecomp locally.

## Consequences

First matches run without conversion stutter, but setup takes longer.

## Amendment (2026-10-04): containers come from the packages, not the XEX

`docs/research/shader-containers-from-disc.md` (launcher ticket 02) found
that the containers are not in the XEX. They are in the `SeekFreeShaderCache`
export of each cooked package. A static reader over the extracted disc
returns 2,019 unique containers, a superset of every capture except six, and
XenosRecomp converts all of them. Setup does not run the game.

- **Launcher setup.** It reads the containers from the packages in
  `gamedata`, runs XenosRecomp and writes the shader pack (spec "Shader
  pack"). This takes about half a minute.
- **The six exceptions.** The game compiles them at runtime from HLSL text in
  the XEX (the splash and autosave busy-icon shaders among them). The game will
  write any such container that is missing from the pack to the cache
  (launcher ticket 18, not built yet), and the launcher already folds those
  into the pack on its next shader step. Until then, Native skips those few
  draws as it does today.
- **No capture run.** No scripted capture run is part of setup.
  `--wwe_shader_dump_dir` stays a developer tool.

## Amendment (2026-10-04): pipelines prebuilt before play

[ADR-0019](0019-pipelines-prebuilt-before-play.md) adds a committed key list
of plain pipeline keys (hashes and render state, no shader code). Native
builds those keys and the player's stored keys before play. The shipped
release still contains no game-derived shaders.
