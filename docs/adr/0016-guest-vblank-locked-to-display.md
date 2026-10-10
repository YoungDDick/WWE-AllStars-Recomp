# ADR-0016: Guest vblank locked to the display's vblank

Status: accepted (2026-10-03)

## Context

The game advances a fixed 1/60 s per frame and paces itself on the guest
vblank, which ReXGlue generates from the host clock at 60.000 Hz, unrelated to
the monitor (`docs/research/movement-judder.md` §1–2). On a display whose real
refresh is not exactly 60 Hz or a multiple of it (a "60 Hz" panel that runs at
59.94 Hz, 119.88, …), the two clocks drift. With vsync, one frame must then be
dropped or held about every 17 s, which is judder.

## Decision

When the game is played (not in scene runs), and the display's real refresh
is within about 0.5% of a multiple of 60 Hz, the guest vblank is driven by the
display's real vblank. The game then runs at the display's rate. On a 59.94 Hz
panel that is 0.1% slower than the console, which nobody can see or hear,
because audio follows the game. Otherwise the guest vblank stays on the host
clock at 60.000 Hz.

Scene runs keep the fixed 60.000 Hz vblank, so oracle frames stay comparable
across machines and monitors.

## Considered options

- **Always 60.000 Hz, accept the drift.** Rejected: a dropped or held frame
  every ~17 s on the most common panels is exactly the judder this work removes.
- **Drop or repeat frames to stay on 60.000 Hz.** The same judder, just placed
  on purpose.

## Consequences

- Game speed depends slightly on the display. Anything that compares timing
  across machines (scene runs, speedrun-style timing) must use the fixed clock.
- Displays that are not near a multiple of 60 (144/165 Hz without VRR) still
  show a 2-3 refresh pattern; there the player is told to enable VRR or choose
  a multiple of 60.
