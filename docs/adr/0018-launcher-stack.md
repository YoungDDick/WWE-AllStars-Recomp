# ADR-0018: Launcher: separate Avalonia app, Native AOT, portable zip

Status: accepted (2026-10-04)

## Context

M5 needs a launcher that turns the player's own disc into a playable install
and starts the game with one Play button (`.scratch/launcher/spec.md`). No ADR
recorded its stack; ROADMAP M5 only names "Avalonia launcher". The research
(`docs/research/launcher.md`, §2 and §8) compared Avalonia, WPF, an in-process
ImGui page and others, and the owner chose in its §8.

## Decision

- **Two processes.** The launcher, `WWEAllStarsLauncher.exe`, is a separate
  app. The game exe stays a pure game, and the in-game PC SETTINGS page (F4)
  stays the in-game settings surface.
- **Avalonia 11 on .NET with Native AOT**, trimmed. Avalonia is MIT, runs on
  Windows and Linux (ADR-0006), and draws its own window chrome. Native AOT
  needs no installed .NET runtime.
- **Projects** under `launcher/`: a UI-free core (`Launcher.Core`, every
  decision and the test seam), the Avalonia app (`Launcher.App`, views only)
  and xUnit tests. Windows-only code sits behind small interfaces in the
  app's `Platform` folder.
- **Hand-off.** The launcher passes only folder paths and hidden plumbing on
  the game's command line; player settings go through
  `wweallstars.settings.toml`, because a command-line setting is pinned in
  game.
- **Toolchain.** The .NET SDK is pinned in `launcher/global.json`
  (`rollForward: disable`), installed repo-locally in `.tools/dotnet-sdk` by
  `scripts/bootstrap.ps1` (SHA-512 checked), and checked by
  `scripts/build.ps1 -Action Launcher` / `-Action LauncherTest`.
- **Shipping.** A portable zip, no installer, no admin rights. v1 is
  unsigned (ADR-0014: releases built by hand).

## Considered options

- **WPF** (SVR2011's launcher): Windows-only, so the Linux/Deck build would be
  a rewrite.
- **In-process ImGui page** (Unleashed, Zelda64Recomp): setup would run inside
  the game exe, which cannot start without game data; it also cannot look like
  a stand-alone launcher.
- **Framework-dependent .NET** (Xenia Manager): asks the player to install a
  runtime first.

## Consequences

- Native AOT cannot link Avalonia's native renderers into the exe, so the
  launcher ships as one exe plus `libSkiaSharp.dll`, `libHarfBuzzSharp.dll` and
  `av_libglesv2.dll` beside it. Packaging must copy all four.
- Unsigned files show SmartScreen's "Windows protected your PC"; the README
  must explain "More info" → "Run anyway".
- Native AOT needs the MSVC linker on the build machine (found through
  `vswhere`), like the game build.
