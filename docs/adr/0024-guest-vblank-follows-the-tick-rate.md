# ADR-0024: The guest vblank follows the tick rate

Status: accepted (2026-10-13, the owner's choice in `.scratch/high-framerate/`
ticket 02). Amends ADR-0016.

## Context

120 fps matches (`.scratch/high-framerate/spec.md`) step the engine tick at
1/120 s in an offline match, so the game makes 120 frames per second there and
60 everywhere else. ADR-0016 assumed one fixed guest rate of 60 Hz. With a
120 Hz guest vblank all the time, a 60 Hz menu or entrance had every frame
presented twice: the picture was right, but an FPS counter such as RTSS read
120, and a switch at startup needed a restart (`video_mode_refresh_rate`).

## Decision

The guest vblank runs at the rate the game ticks at. It is 120 Hz while a
match ticks at 1/120 s and the video mode's rate (60 Hz) otherwise, and the
switch applies from the next vblank (`GraphicsSystem::SetGuestVblankRate`,
`patches/rexglue-guest-vblank-rate.patch`). ADR-0016's lock to the display
uses the current rate: a 120 Hz guest locks to every 3rd refresh of a 360 Hz
display. A switch drops the lock until the display thread locks again at the
new rate, and the presenter takes the new period at once.

## Considered options

- **A fixed 120 Hz guest vblank whenever the setting is on.** Rejected: 60 fps
  content is presented twice, FPS counters lie, and turning the setting on
  needs a restart.
- **Skip repeated presents in the presenter.** Rejected: a heuristic that has
  to guess the game's rate from held vblanks.

## Consequences

- The guest vblank's rate can change during a session. Code that assumes a
  fixed period has to read it (`GuestVblankHz`).
- Scene runs force 120 fps matches off, so they keep ADR-0016's fixed 60 Hz.
- Short 1/30 s cinematics inside a match (they keep their step) switch the rate
  to 60 and back, each switch at a cinematic boundary where the game itself
  usually hitches.
- The video mode's rate is read once, before the GPU VSync thread starts, as
  before this change; only the override is read per vblank (an atomic load).
  Querying the video mode takes the cvar registry's lock, so it stays off the
  per-vblank path and 60 fps play costs what it did in 1.1.0.
