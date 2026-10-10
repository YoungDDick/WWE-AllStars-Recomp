# ADR-0022: Build on Title Update 2; addresses move by script, not byte signatures

Status: accepted (2026-10-10, the owner's decision in the TU2 and DLC spec).
Supersedes the consequence of ADR-0009 (byte signatures for hooks). The rest of
ADR-0009 (build on the base until a player-supplied TU2 can be verified, then
rebase) stands and is now done.

## Context

ADR-0009 kept the base v5 build until TU2 could be verified by hash, and asked
for hook addresses recorded with byte signatures so that they could move. None
were recorded: the manifest and `src/` hold bare addresses. TU2 relinks the
exe. 90,492 of 92,061 functions move, about 2% change and 110 are new, so
every hard-coded address moves (`docs/research/title-updates-and-dlc.md`).
TU2 is the last update the game got (`.scratch/tu2-dlc/spec.md`), so the move
happens once.

## Decision

- The recomp is generated from `assets/default.xex` (base v5, SHA-256
  `521c2325…6892`) plus `assets/default.xexp` (TU2, `a6a45704…ed6a`). Codegen
  applies the patch because it sits next to the XEX. `scripts/build.ps1`
  refuses a missing or different patch. The base build goes away.
- `scripts/tu_addrmap.py` moves the addresses. It matches `.pdata` functions
  by code with branch and address immediates masked, learns data addresses
  from `lis`-based pairs at the same place in matched code, and falls back to
  a local match that follows branch targets and maps pointers in data. It
  reports each address with its method and flags matched functions whose
  non-address operands differ, which is the re-check a hook needs. What it
  can't map is fixed by hand with TU2 disassembly as evidence.
- No byte signatures. TU2 is the last update, so there is no third image to
  move to, and the script does the job the signatures were for.

## Consequences

- Every address in `wweallstars_manifest.toml` and `src/` is a TU2 address.
  `analysis/xex/tu2.pe` (`build.ps1 -Action Image`) is the image to read;
  `analysis/xex/default.pe` (base) stays only as the script's input.
- The build and every developer need the owner-dumped TU2 next to the XEX,
  as with the ISO. TU2 is never shipped or downloaded.
- Research notes written before 2026-10-10 quote base addresses. Map them
  with `tu_addrmap.py` (report mode) before using them.
