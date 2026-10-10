# ADR-0017: Enhanced rumble mixed on the host

Status: accepted (2026-10-04)

## Context

Enhanced vibration played its effects as guest waveforms: a hook
called the game's play-rumble native `0x82B515B0` and appended samples, and
the game's force-feedback manager played them. The research verdict was to
keep the hit shapes in the guest (`docs/research/rumble-best-practices.md` 3.4),
because guest waveforms pause with the game and use its frame time.

The owner's match log (`analysis/capture-game-20261004-090711/runtime.log`)
showed two problems with that:

- **Game time stretches the waveform.** The manager's update `0x829293C8`
  adds the game's frame time, which slow motion shrinks. An extreme
  signature's 550 ms big moment played for about 2.75 s: its right motor fell
  by 119 per frame instead of about 595.
- **One waveform at a time.** A manager plays one waveform, so a new effect
  either replaces the one playing or is skipped. Enhanced skipped any
  effect weaker on a motor (per-motor stronger wins). While the stretched big
  moment played, every mat impact of the finisher was skipped
  (`vibration impact skipped ... playing_left=100`, 09:09:31-09:09:33 and
  09:09:59-09:10:01).

## Decision

Enhanced effects are mixed in the port, at the pad seam
(`InputSystem::set_vibration_output`), by the rumble mixer
(`src/vibration/rumble_mixer.h`). Each event adds an effect for one pad. Each
pad request takes, per motor, the strongest of every active effect and the
game's own request, and then Off and the vibration strength apply. Effects run
on host time between that pad's requests, with each step capped at 50 ms;
every sample is output on at least one request and a crack on two. The Enhanced hooks no longer write
guest waveforms; stronger wins and the queued heartbeat are gone.

Original is untouched: no hook acts and the mixer stays empty, so the pad gets
exactly the game's requests.

## Considered options

- **Keep guest waveforms, fix the stretch.** Scaling each sample by the
  current time dilation does not survive a dilation change mid-sample, and one
  waveform per manager still forces a skip or a cut.
- **A host thread at a fixed rate.** Not needed: the game sends a request for
  each local player every frame (about 60 a second in the ledger, menus
  included), so the mixer advances on those requests.

## Consequences

- Pause sync no longer comes for free from the guest. The mixer must be told
  when the game pauses or a match ends (research in
  `docs/research/controller-vibration.md`).
- The vibration ledger and trace still record the game's own requests only.
  What the mixer added is in the `vibration effect` log lines, one per effect.
- Layering makes doubled events harmless (two notifies for one slam merge by
  maximum), so new event hooks need no dedup rule.
