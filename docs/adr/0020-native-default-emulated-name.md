# ADR-0020: Native is the default renderer; Classic is renamed Emulated

Status: accepted (2026-10-04). Amends ADR-0002.

## Context

ADR-0002 made Native the default only at full parity on the test matrix, and
called the SDK's emulated Xenos renderer "Classic". The owner decided on
2026-10-04 to make Native the default now. "Classic" did not say what that
renderer is; players and tickets kept explaining it as "the emulated one".

## Decision

- **Native is the default.** The cvar `wwe_native_renderer` defaults to
  true, and the launcher's committed settings schema says the same (the
  schema drift test keeps the two in step). Only the default changes: a new
  install, or a settings file with no `wwe_native_renderer` line, starts on
  Native. A settings file that already chose `false` keeps it; there is no
  migration.
- **"Classic" is renamed Emulated** everywhere a person reads it: launcher
  text and buttons, the in-game settings page, log lines, the smoothness
  report, script output and switches, living docs and the glossary, and code
  identifiers.
- **Stored names keep the old spelling**, so existing files keep working:
  setting keys and cvars (`wwe_native_renderer`), settings-file values, the
  scene key `classic_runs`, the oracle run labels and paths (`classic-N`),
  `classic_noise_worst_tile_hot` in a scene's `result.json`, and the value
  `classic` in a capture's `display.json`. Old switches stay as hidden
  aliases (`capture.ps1 -Classic`, `native_frame_check.py --mode classic`),
  and the launcher still reads a fallback line that says "running on
  Classic".
- Old tickets, ADRs and dated research notes are history and keep the old
  name.

## Consequences

- Scripts that start an Emulated run name the renderer
  (`--wwe_native_renderer=false`) instead of relying on the default.
- A player whose Native start fails still gets the renderer fallback to
  Emulated and the launcher's offer to switch to it.
