# WWE All Stars Recomp

A static recompilation of the Xbox 360 game WWE All Stars, built on ReXGlue
v0.10.0. It follows the layout of SVR2011-Recomp
(`D:\Projects\SVR2011-RECOMP`). Plan and milestones: `docs/ROADMAP.md`.
Decisions: `docs/adr/`.

## Hard rules

- Never modify the ISO. Game data (`assets/`), generated code (`generated/default`),
  converted shaders, logs (`analysis/`) and the decrypted image never enter Git.
- Codegen runs in strict mode only. Add a missing function as a seed in
  `wweallstars_manifest.toml`, with a comment giving the instruction-level
  evidence. Never stub it or force generation.
- The build checks the XEX SHA-256 (`521c2325…6892`, base v5) and the Title
  Update 2 patch `assets/default.xexp` (`a6a45704…ed6a`). Codegen applies TU2;
  every address in the manifest and `src/` is a TU2 address (ADR-0022).
- Run PowerShell scripts with `pwsh` (PowerShell 7). Windows PowerShell 5 lacks
  `Get-FileHash` here.

## Commands

```
pwsh ./scripts/build.ps1 -Action Codegen      # regenerate C++ from the XEX
pwsh ./scripts/build.ps1 -Action Configure    # Vulkan, source-tree SDK (patches applied)
pwsh ./scripts/build.ps1 -Action Build -Jobs 20
pwsh ./scripts/build.ps1 -Action Launcher      # launcher (Native AOT) -> out/launcher/publish
pwsh ./scripts/build.ps1 -Action LauncherTest  # launcher xUnit tests (pinned .NET SDK)
pwsh ./scripts/build.ps1 -Action Image        # TU2 memory image -> analysis/xex/tu2.pe
python scripts/disasm.py <hex addr> [count]   # disassemble the TU2 image
python scripts/tu_addrmap.py <paths>          # map base v5 addresses to TU2 (report only)
python scripts/inspect_disc.py <iso> --extract-all assets
```

## Layout

- `.tools/`: llvm, the rexglue prebuilt package, xenosrecomp and fidelityfx are
  junctions to SVR2011's `.tools`. `rexglue-source` is a separate copy at
  `f5337cdc` with this repo's `patches/` applied.
  `.tools/xenia-canary/` holds the reference emulator (binary only, no source).
  `.tools/dotnet-sdk/` is the launcher's .NET SDK, pinned by `launcher/global.json`.
- `launcher/`: the Avalonia launcher (ADR-0018): `Launcher.Core` (UI-free, tested),
  `Launcher.App` (views), `tests/Launcher.Tests` (xUnit).
- `art/icon/`: the original icon of both exes; `make_icon.py` is the source and
  writes the `.ico`, `.svg` and 64 px `.png`. Never use game art there. A
  `metadata/icons` folder (game art) fails the build (`cmake/no_game_art.cmake`).
- `analysis/xex/tu2.pe`: the TU2 memory image (`build.ps1 -Action Image`), read by
  `disasm.py` and the analysis scripts. `analysis/xex/default.pe` is the base v5
  image, kept as `tu_addrmap.py` input (file offset = address − 0x82000000).
- `docs/research/`: TU research and the Xenia baseline.

## Known game facts

- TU2: entry point `0x8241A7E0`, image base `0x82000000`, size `0x18C0000`,
  `.text` 0x82410000–0x83504018 (base v5: entry `0x8241A8E0`, `.text` end 0x83500DB0).
- Xenia reports a stuck audio loop. Canary's patch writes `li r3,1` at
  `0x833A6C98` in the base XEX, which is `0x833A9B50` in TU2 (same code).

## Agent skills

### Issue tracker

Local markdown under `.scratch/<feature>/`, committed to `main`. The public GitHub history is built without it (`scripts/publish-tree.ps1`, ADR-0023). See `docs/agents/issue-tracker.md`.

### Triage labels

The five default roles (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`) as `Status:` lines. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `CONTEXT.md` + `docs/adr/`. See `docs/agents/domain.md`.
