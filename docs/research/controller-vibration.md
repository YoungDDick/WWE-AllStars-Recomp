# Controller vibration: why the game sends zeros

Ticket: `.scratch/controller-vibration/issues/04-case1-game-vibration-default-on.md`.
Date 2026-10-03. Addresses are base XEX v5, read with `python scripts/disasm.py`
on `analysis/xex/default.pe`. Package facts come from the cooked packages under
`assets/Game/CookedXenon` (read-only; LZO-decompressed in a scratch directory,
nothing committed).

## Answer

- **No switch is off.** Every gate between a rumble request and
  `XamInputSetState` is open on a fresh install. The profile value (3), the
  game's own `PSI_ControllerVibration` default (3), the force-feedback
  manager's `bAllowsForceFeedback` (1) and its gain (1.0) all say "on". None
  of them is wrong, so there is nothing to default to on.
- **The game rumbles very rarely by design.** The shipped game has one live
  rumble source: a 0.1-second pulse at 50 % strength when a pawn is hit by a
  move whose combat type is `ECombatType_AERIAL` (4). The data-driven
  "VibrateController" logic event exists in the code, but no shipped package
  uses it. Neither do the stock Unreal Engine 3 sources (`SeqAct_ForceFeedback`,
  damage-type waveforms).
- **Run 1's five non-zero frames are exactly one aerial hit.** That pulse
  peaks at `100 × 655 × (1 − dt/0.1)`. With dt = 1/60 s this gives 54583, the
  logged `peak_left=peak_right=54583`. It then lasts 5 frames, matching
  `nonzero=5`. Run 2 had no aerial hit on the player, so it logged no
  non-zero request.
- The game has no vibration item in OPTIONS. Its `Get/SetControllerVibration`
  natives exist, but no shipped script or UI calls them, and no native code
  reads the value.
- Fix: none in the runtime or the guest defaults. Making the pad rumble on
  every hit would add a feature the game doesn't have. That is the owner's
  call (see "Options" below), not a default to restore.

## The engine

WWE All Stars runs on Unreal Engine 3 (package tag `0x9E2A83C1`, file version
398, licensee 37). `Coalesced.ini` line 1724 has
`ForceFeedbackManagerClassName=XeDrv.XnaForceFeedbackManager`. The spec's
"rumble player" is UE3's `UXnaForceFeedbackManager`. Its fields match
`Engine.ForceFeedbackManager`:

| Offset | Field | Default (`Default__ForceFeedbackManager`) |
|---|---|---|
| +0x34 bit 0x80000000 | `bAllowsForceFeedback` | 1 |
| +0x34 bit 0x40000000 | `bIsPaused` | 0 |
| +0x38 | `FFWaveform` | None |
| +0x3C | `CurrentSample` | |
| +0x40 | `ElapsedTime` | |
| +0x44 | `ScaleAllWaveformsBy` | 1.0 |

The vtable is at `0x82197B78`. Slot 0xD0 is `ApplyForceFeedback`
(`0x829EB7D0`) and slot 0xD4 is the stop method (`0x829EB1B8`).

## The per-frame path (already known, confirmed)

- `0x829EB7D0` (ApplyForceFeedback): it calls `0x829EB688`, then the XDK
  wrapper `0x8241A368` → `XamInputSetState`. It runs every frame for the local
  player's controller, which explains about 60 requests a second in the ledger.
- `0x829EB688` (GenerateForceFeedbackValues): it zeroes both speeds, then
  returns zeros if `FFWaveform` (+0x38) is null (`0x829EB6B4`) or `bIsPaused`
  is set (`0x829EB6C0`–`0x829EB6CC`, `rlwinm r10,r11,0,1,1`). Otherwise it
  calls vtable 0xCC (`0x829293C8`, which advances `ElapsedTime` and
  `CurrentSample` and clears `FFWaveform` after the last sample). It evaluates
  the sample with `0x829EB418` and scales by `ScaleAllWaveformsBy` unless that
  is 1.0 (`fcmpu` against `0x82001D38` = 1.0 at `0x829EB758`).
- `0x829EB418` evaluates one sample. For function 2
  (`WF_LinearDecreasing`) the speed is `(1 − elapsed/duration) × amplitude ×
  655.0` (`0x82197D58` = 655.0, `0x829EB4A8`–`0x829EB4E8`).

So zeros come from "no waveform playing", never from a gain or flag. Next:
who starts a waveform.

## Who starts a waveform: `0x82B515B0`

`0x82B515B0(PlayerController*, left, right, leftFunc, rightFunc, duration)`
is the native "play rumble" call:

- It returns at once if `PC+0x458` (`ForceFeedbackManager`) is null
  (`0x82B515C0`–`0x82B515E0`).
- It builds a new `ForceFeedbackWaveform` (`0x82B513B0`) with one sample. The
  amplitudes are `(strength × 2) & 0xFE` (`0x82B51660`, `0x82B51668`).
- It resets `CurrentSample` (`0x82B516A4`) and `ElapsedTime` to 0.0
  (`0x82B516A8`–`0x82B516B0`), and clears `bIsPaused`
  (`rlwinm r7,r8,0,2,0` at `0x82B516BC`).
- It stores the waveform in `FFWaveform` only if `bAllowsForceFeedback` is
  set (`0x82B516D4`–`0x82B516EC`). Otherwise it stores null (`0x82B51700`).

It has two callers (`bl 0x82b515b0`):

1. **`0x82AE3954`, the hit pulse.** This is in the pawn damage function
   (`0x82AE2E18`):
   ```
   82AE3910  lwz r11,0(r28) / lwz r10,0x2d0(r11) / bctrl   ; victim pawn state check
   82AE3924  cmpwi cr6,r3,0 / bne -> skip
   82AE392C  lbz r11,0x18(r29) / cmplwi cr6,r11,4 / bne -> skip   ; combat type == 4
   82AE393C  lwz r3,0x200(r28)                     ; victim's Controller
   82AE3940  li r7,2 / li r6,2 / li r5,0x32 / li r4,0x32
   82AE3950  lfs f1,0x25cc(r11)                    ; 0x820625CC = 0.1
   82AE3954  bl 0x82b515b0
   ```
   The damage record's byte +0x18 is the attacker's move combat type
   (written from pawn `+0x76D` or attack data `+0x43` at `0x825BF7E0` and
   `0x825BF8E4`). The FSM event "Change the combat type of a move" sets it
   (`0x8259A290`). `SD_AnimState.ECombatType` value 4 is
   `ECombatType_AERIAL` (enum read from `SD_Animation.xxx`). The vtable+0x2D0
   check is `0x82ABAC00`, which returns bit 0x100 of pawn `+0x6FC`. It is a
   per-pawn state bit, not a setting. The vtable+0x450 setter `0x82ABAC10`
   writes it, called from `0x82ADB8E4`, `0x82ADB934`, `0x82BAAC84` and
   `0x82BAACAC`.
2. **`0x825E3540`, the "VibrateController" logic event** (tag `VIBRCONT`,
   description "apply vibration to this pawns controller."). Its vtable is
   at `0x82093DF8`. Its execute method `0x825E34D8` passes the event's
   left/right strength (0–100, +0xC/+0x10) and time (+0x14). The game-level
   event factory `0x825E5F40` builds it from the 8-byte tag `VIBRCONT`
   (`0x825E68F8`–`0x825E6930`). The tag doesn't occur in any of the 1344
   cooked packages after decompression. The logic data lives in
   `CombinedGlobalCharacters.xxx` (3904 `END` tags, 1000 `TGLGRTY`, 1351
   `COMMPLCM`, ...), and `VIBRCONT` has 0 uses. Its sibling "turn player
   input off at the end of a match" (`OVRDINPT`) is unused too.

## Who could switch it off (all open)

- **`bAllowsForceFeedback`.** The constructor sets it at `0x829EB090`
  (`oris r7,r8,0x8000` / `stw r7,0x34(r30)`). No native code clears bit 31 of
  the manager's +0x34; the only `clrlwi rX,rY,1` → `stw 0x34` sites
  (`0x824E3418`, `0x824F40E0`, `0x8259C97C`, `0x82B228EC`, `0x82B22968`)
  write other objects. In script, `Engine.u` reads it only in
  `ForceFeedbackManager.PlayForceFeedbackWaveform`. No other script package
  has the name `bAllowsForceFeedback` in its name table, so none can write it.
- **`ScaleAllWaveformsBy`.** It is 1.0 (`0x829EB090` path, `lfs f0,0x1d38`)
  and nothing else writes it.
- **`PSI_ControllerVibration`.** `Game.GameProfileSettings` lists IDs
  `[1, 100..108, 25, 115, 116, 125, 126, 132]`, and `DefaultSettings` gives
  ID 1 (`PSI_ControllerVibration`) the value 3 (`PCVTO_On`). The runtime
  returns 3 for `XPROFILE_OPTION_CONTROLLER_VIBRATION` (0x10040003). Natives
  `GameProfileSettings.execGet/SetControllerVibration` (`0x82A90698`,
  `0x82A90E20`, vtable slots 0x168/0x16C) and
  `GameEngineInfo.execGet/SetControllerVibration` (`0x82A901D0`,
  `0x82A90350`, forwarding through `0x82B1FE08`/`0x82B1FDE8`) exist. No
  script function in `Game.u` references them, the only direct callers of
  `0x82B1FE08`/`0x82B1FDE8` are those exec thunks, and no UI package contains
  "Vibration". The value is stored but nothing reads it to gate rumble.
- **`PlayerReplicationInfo.bControllerVibrationAllowed`.** It defaults to 1
  and is read only by `PlayerController.ClientPlayForceFeedbackWaveform`.
  That stock UE3 path is unused: `SeqAct_ForceFeedback` and
  `ForceFeedbackWaveform` assets occur only in `Engine.xxx` itself.
- **Runtime.** The game imports `XamInputSetState`, `XamInputGetState`,
  `XamInputGetCapabilities`, `XamInputGetKeystrokeEx`,
  `XamUserReadProfileSettings` and `XamUserGetSigninState`. It does not import
  `XamInputGetUserVibrationLevel`. Ticket 03's runs showed the pad accepting
  every request.

## Run 1 matches one aerial hit exactly

For an aerial hit: amplitude = 50 × 2 = 100, function 2, duration 0.1 s.
`ApplyForceFeedback` advances `ElapsedTime` before sampling, so the first
non-zero frame at dt = 1/60 s gives `100 × 655 × (1 − 0.01667/0.1) =
54583.3`, truncated to **54583** by `fctidz`. Frames 1–5 are non-zero, and
frame 6 reaches 0.1 s and ends the waveform: **5 frames**. That is the run 1
ledger (`nonzero=5 peak_left=54583 peak_right=54583`). The pulse lasts about
83 ms and starts at 83 % strength, so it is easy to miss during play.

## Options for the owner

1. Accept the game as shipped: the pad pulses on aerial hits only. To check,
   take an aerial hit (an opponent's top-rope or diving attack) and look for
   `nonzero=5`, peak 54583.
2. Add PC-only extra rumble, such as a pulse on every hit, as a new
   settings option with a guest hook at the damage function (`0x82AE3928`
   compares the combat type). This is a feature beyond the 360, so it needs
   the owner's decision and its own ticket.
3. If an 83 ms pulse is too short to feel on SDL, stretch or re-arm short
   pulses in the runtime. This is a host-side question (spec case 3), not a
   game default.

# Enhanced vibration (2026-10-04)

Spec: `.scratch/enhanced-vibration/spec.md`. Same image and tools as above.

## The damage record and its callers

Research agent, 2026-10-04 (packages decompressed in a scratch directory, nothing
committed).

**ECombatType** (`SD_Animation` export 237, `Enum SD_AnimState.ECombatType`,
20 names): 0 NONE, 1 STRIKE, 2 STRONG_STRIKE, 3 GRAB, 4 AERIAL, 5 FINISHER,
6 STRONG_GRAB, 7 RUNNING_STRIKE, 8 RUNNING_GRAB, 9 RUNNING_STRONG_STRIKE,
10 RUNNING_STRONG_GRAB, 11 WEAPON_STRONG, 12 WEAPON_WIDE, 13 WEAPON_WIDE_STRONG,
14 WEAPON, 15 EXTREME_SIGNATURE, 16 CHARGED_STRIKE, 17 CHARGED_GRAB,
18 WEAPON_BURST, 19 MAX. Values 16–18 are spelled `ECombatTYPE_` in the name
table.

**Callers of `0x82AE2E18`**: 9 `bl` sites; its only data reference
(`0x823BC410`) is its .pdata entry, not a vtable slot. Every record comes from
the constructor `0x825B7698`, which zeroes +0x18 (`stb` at `0x825B76C8`).

| Call site | In | Path | +0x18 |
|---|---|---|---|
| `0x825BFB24` | `0x825BF5F8` | Logic event "force damage to selected body part" | set: victim +0x76D (`0x825BF7E0`), or the opponent's attack data +0x43 / +0x76D (`0x825BF8E4`) |
| `0x8255926C` | `0x82559178` | Reaction state applies the pending hit at pawn+0x940 | copied from the hit (`0x82AE2858`), 0 if cleared |
| `0x82AE3BE0` | `0x82AE3B68` | Else-branch of the row above | as above |
| `0x82B72EDC` | `0x82B72D10` | Damage over time tick (inferred class) | 0 |
| `0x82B74028` | `0x82B73FC8` | Damage-on-hit buff (inferred) | 0 |
| `0x82BFA68C` | `0x82BFA4A0` | Native buff, damage over time | 0 |
| `0x82BFBB50`, `0x82BFBC48` | `0x82BFB8D8`, `0x82BFBBE0` | Native area buff | 0 |
| `0x82BFFB70` | `0x82BFFA90` | Native weapon buff on hit | 0 |

Only the first three carry a move's combat type, so buffs and damage over time
give no pulse (type 0).

**Record fields**: +0x00 head, +0x04 body, +0x08 arms, +0x0C legs (ints,
applied by `0x82AD81D8` at `0x82AE3820`–`0x82AE38C4`); +0x14 stun
(`0x82AE38D8`–`0x82AE390C`); +0x18 combat type (u8, read at `0x82AE3654`,
`0x82AE392C` and the jump table at `0x82AE3A58`); +0x1C instigator: the
attacker on the pending-hit path only (pawn+0x95C, `0x82AE2758`), the victim
itself elsewhere.

**Registers at `0x82AE392C`** (write scan of `0x82AE2E18`–`0x82AE3B60`): r28
victim pawn and r29 record, never written after `0x82AE2E38`/`0x82AE2E30`;
r26 final body damage (int, `0x82AE2FB0`); r27 = 0x83740000; r25 = `L"Game"`.

## The hook: a midasm hook at the aerial check

Chosen: a `[[entrypoint.midasm_hook]]` at `0x82AE392C` (`WweHitPulse`,
`src/vibration/enhanced_hooks.cpp`), not a wrap of `sub_82AE2E18`.

- `0x82AE3910`–`0x82AE3928` call vtable+0x2D0 on the victim
  (`lwz r10,0x2d0(r11)` / `bctrl`) and `bne cr6,0x82ae3958` when it returns
  non-zero. So `0x82AE392C` (`lbz r11,0x18(r29)`) is reached exactly by the hits
  that pass the game's own gate. A function wrap would have to re-derive every
  branch before it.
- `0x82AE3958` (`lwz r10,0x804(r27)`) is the next block and a branch target
  already; it reads no register the skipped code (`0x82AE392C`–`0x82AE3954`)
  sets. The hook returns true in Enhanced, and codegen emits
  `goto loc_82AE3958`, so the game's own aerial call never runs and each hit
  gets one pulse, from the table. In Original and Off it returns false and the
  shipped code runs.
- A midasm hook gets registers only, no `ctx`/`base`. It is passed r1, r13 and
  fpscr and calls `0x82B515B0` on a side context with that r1, as the game's
  own `bl 0x82b515b0` at `0x82AE3954` does from the same stack pointer (the
  SDK's `rex::CallFrame` pattern). `base` is the kernel memory's
  `virtual_membase()`.
- Generated code (`generated/default/wweallstars_recomp.101.cpp`):
  `if (WweHitPulse(ctx.r1, ctx.r13, ctx.fpscr, ctx.r28, ctx.r29)) goto loc_82AE3958;`
  ahead of the `lbz`.

## AI wrestlers never rumble a pad

Answer: an AI controller has no force-feedback manager, but its +0x458 is
another, possibly non-null field, so the gate is what keeps the native from
writing into it. `RumbleTarget` (`src/vibration/force_feedback.h`) checks the
gate and the manager's vtable before any call.

- **pawn+0x200 is `Pawn.Controller`.** `0x82ADB870 lwz r29,0x200(pawn)` goes to
  the `UnPossess` event (`0x82AD90E8`, FName from `L"UnPossess"` at
  `0x820BCABC`); `0x8294F06C stw r3,0x200(r29)` stores a newly spawned
  AIController.
- **+0x458 is `PlayerController.ForceFeedbackManager` only.**
  `UGameViewportClient::GetForceFeedbackManager` (`0x8286D128`) returns
  `LocalPlayer->Actor` +0x458 (`0x8286D180`–`0x8286D18C`). Class sizes from
  the `UClass` constructor (`bl 0x8275D498`, size in r5): `PlayerController`
  0x4F8 (`0x826F34B4`), `GamePlayerController` 0x560 (`0x82ACB4F4`),
  `AIController` 0x2FC (`0x826BE62C`), `SD_AIController` 0x30C (`0x82484524`),
  `GameAIController` 0x61C (`0x82AB788C`). No AI class derives from
  PlayerController. In a `GameAIController`, +0x458 lies inside a native member
  at +0x36C (constructor `0x82AAF2A0`), so it is not a manager and may be
  non-null; `0x82B515B0` would then pass its null check (`0x82B515C0`) and write
  +0x34..+0x40 of the wrong object.
- **pawn+0x6FC bit 0x100 means "the pawn's AI controller possesses it".**
  `0x82ADB858` (runs with the bit clear) unpossesses, has pawn+0x1AA0 possess
  the pawn, and sets the bit (`0x82ADB8E4`); `0x82ADB8F8` hands it back to
  pawn+0x1A9C (a `GamePlayerController`, cast at `0x82A5DF58`) and clears it
  (`0x82ADB934`). The game itself reads "bit set" as "pawn+0x200 is a
  GameAIController" (`0x82ADE860`–`0x82ADE878`: `if (vtbl[0x2D0]())
  Cast<AGameAIController>(pawn+0x200)`). vtable+0x2D0 is `0x82ABAC00`
  (`lwz r11,0x6fc(r3)` / `rlwinm r3,r11,24,31,31`).
- **The manager's vtable is `0x82197B78`**, written by its constructor
  (`0x829EB078 lis r10,-0x7de7` / `0x829EB07C addi r9,r10,0x7b78` /
  `0x829EB084 stw r9,0(r30)`), which sets `bAllowsForceFeedback` too
  (`0x829EB090`).
- Residual edge (inferred): CPU wrestlers also get a `GamePlayerController` at
  pawn+0x1A9C. If a CPU pawn ever ran with the bit clear (global `0x837455A8`
  non-zero, teardown at `0x82ADBF7C`, a tag swap), its hits would rumble that
  local player's pad index, as the game's own aerial pulse would.

## Waveforms and "stronger wins"

- The play-rumble native `0x82B515B0` builds a `ForceFeedbackWaveform` whose
  `Samples` TArray sits at +0x38 (data), +0x3C (count), +0x40 (max). Each sample
  is 8 bytes (`0x82B51660`–`0x82B51680`): left amplitude, right amplitude
  (`(strength × 2) & 0xFE`), left function, right function, float duration.
  `0x82508B30` is `TArray<8 bytes>::AddItem`: count+1, grow through
  `0x82413378` when past max, return the new slot.
- Functions (`0x829EB418`, switch on the byte): 0 constant
  (`mulli r10,r11,0x28f`, `amplitude × 655` kept to 16 bits, so strength stops
  at 50), 1 linear increasing, 2 linear decreasing (the game's aerial pulse),
  3 sine rise, 4 cosine fall, 5 sine bump, 6 noise
  (`docs/research/rumble-best-practices.md` section 2).
- The per-frame update `0x829293C8` adds dt to `ElapsedTime`; past the current
  sample's duration it advances `CurrentSample` (`0x829293F0`), carrying the
  remainder, and after the last sample clears `FFWaveform` unless the waveform
  loops (`0x8292940C`–`0x82929418`). So a set `FFWaveform` is still playing
  from `CurrentSample` on; `PlayingPeak` (`src/vibration/stronger_wins.h`)
  takes the strongest of those samples per motor. Pro vibration's hit shapes
  (`src/vibration/shapes.h`) pass sample 1's left and right separately to the
  native (r4/r5, r6/r7), then set its amplitude bytes exactly (the native keeps
  only even ones) and append the body through `0x82508B30`.

## Critical health: the crossing and the double pulse

- **The crossing** is in the same function, ahead of the HUD sound.
  `0x82AE3978`–`0x82AE39A4` skip to `0x82AE3A4C` unless a game object
  (`0x83740804`) is set and not in the victim's +0x2C list. `0x82AE39A8 bl
  0x82af2d90` returns an object whose float +0x158 is the critical threshold.
  `0x82AE39B4 fcmpu f27,f0` / `ble 0x82ae3a2c`: health on entry (f27, from
  vtable+0x3DC at `0x82AE2E3C`) must be above it. `0x82AE39BC`–`0x82AE39F0`:
  health now (vtable+0x3DC again) must not be above it (`bgt 0x82ae3a2c`).
  `0x82AE39F4`–`0x82AE3A28` then build `L"SFX_UI_HUD_CRITICAL_01"`
  (`0x821FB6E8`, `lis r11,-0x7de0` / `addi r4,r11,-0x4918`) and play it
  (`0x824DB3C0`). So `0x82AE39F4` runs once per crossing, never again while
  the wrestler stays critical. r28 is still the victim there.
- **Hook**: `[[entrypoint.midasm_hook]]` `WweCriticalPulse` at `0x82AE39F4`,
  void (the sound still plays). This site is not behind the possession gate, so
  `RumbleTarget`'s own gate-bit and vtable check is what keeps AI controllers
  away from the native.
- **Second pulse: one waveform with three samples**, not two calls. One call
  holds one sample, and a second call would need a host timer that neither
  pauses with the game nor sees the guest's frame time. Instead the hook calls
  the native for the first pulse, then appends a gap sample and a second pulse
  to the waveform it just installed, through the game's own
  `TArray::AddItem` (`0x82508B30`). The game's per-frame update plays the
  samples in order (`0x829293C8`), pauses them with the game, and stronger
  wins sees the remaining pulse until the last sample ends. Since pro
  vibration it is a left-motor heartbeat in the shape table
  (`src/vibration/shapes.h`: left 50 %, 70 ms, gap 120 ms, 70 ms; right 0).
- **FPSCR**: the hooks restore the side call's fpscr. At both hook sites the
  next float instruction follows a call or a label, where codegen checks the
  flush mode at run time rather than assuming it.
- A finisher that drops the victim to critical starts its big moment first
  (`0x82AE392C`). The heartbeat (weaker on both motors) does not replace it; it is
  appended to that waveform as gap, pulse, gap, pulse, so the warning follows
  the finisher instead of being lost (logged `vibration pulse queued`).

# Full-coverage vibration research (ticket 01)

Ticket: `.scratch/full-coverage-vibration/issues/01-research-pad-index-pause-attacker-reversal.md`.
Date 2026-10-04. Same image and tools. Package facts come from decompressed
`Game.u`, `Engine.u`, `SD_Animation.u` and `CombinedGlobalCharacters.u` in a
scratch directory. The game was not run. Labels are as in `impact-events.md`.

## 1. Controller to pad index

**Answer (proven):** the pad index is `ULocalPlayer.ControllerId`:
`PlayerController +0x2E4` (`Player`) → LocalPlayer `+0x58` (`ControllerId`).
The game's own helper does this exact read.

- **Who calls `XamInputSetState`.** The import thunk is `0x834FF9A4`. Its only
  caller is the XDK wrapper `0x8241A368(userIndex r3, XINPUT_VIBRATION* r4)`,
  which passes r3 through unchanged (`0x8241A410 mr r3,r30` / `0x8241A414 bl
  0x834ff9a4`). The wrapper is called from `ApplyForceFeedback`
  (`0x829EB7F8`) and from the stop method (`0x829EB1D8`, zeros).
- **`ApplyForceFeedback(manager r3, ControllerId r4, DeltaTime f1)`**
  (`0x829EB7D0`, vtable `0x82197B78` + 0xD0) keeps r4 in r31 and hands it to
  the wrapper (`0x829EB7F4 mr r3,r31`).
- **Its caller is the Xenon viewport input poll `0x82884258(viewport r3,
  DeltaTime f1)`.** It loops over pads 0–3 in r29 (`0x828849B8 addi
  r29,r29,1` / `cmpwi cr6,r29,4` / `blt 0x82884328`). For every pad it ends
  here:
  ```
  828847C8  lwz r3,0x38(r30)          ; viewport->ViewportClient (FViewportClient)
  828847CC  mr r4,r29                 ; pad index
  828847D4  lwz r10,0x28(r11) / bctrl ; GetForceFeedbackManager(pad) = 0x8286D128
  828847E0  cmplwi cr6,r3,0 / beq 0x82884800
  828847EC  mr r4,r29 / fmr f1,f29    ; pad, DeltaTime
  828847F4  lwz r10,0xd0(r11) / bctrl ; ApplyForceFeedback
  ```
- **`GetForceFeedbackManager(client r3, pad r4)`** (`0x8286D128`) walks
  `GEngine` (`0x8371D180`) → `GamePlayers` (data `+0x2B0`, count `+0x2B4`).
  It picks the LocalPlayer with `+0x68 == client` and `+0x58 == pad`
  (`0x8286D15C lwz r5,0x58(r10)` / `cmpw cr6,r5,r4`) and returns
  `LocalPlayer +0x38 (Actor) → +0x458` (`0x8286D180`–`0x8286D18C`).
- **`APlayerController::SetPlayer` (`0x827E9EB0`)** links both ways:
  `0x827E9EE0 stw r4,0x2e4(r31)` (`PC.Player`) and `0x827E9EE4 stw
  r31,0x38(r4)` (`Player.Actor`). It clears the old player's `+0x2E4` first.
- **The game's own read, `0x82ADEE38` GetControllerId(pawn):**
  ```
  82ADEE44  lwz r3,0x200(r3)   ; pawn.Controller
  82ADEE48  bl 0x82a5df58      ; Cast<GamePlayerController>
  82ADEE54  lwz r3,0x2e4(r3)   ; .Player
  82ADEE58  bl 0x82503ca0      ; Cast<LocalPlayer> (class cache 0x8371D630)
  82ADEE64  lwz r3,0x58(r3)    ; .ControllerId
  82ADEE78  li r3,-1           ; either cast failed
  ```
  The pause menu stores this value for the pawn that paused (`0x82AF8954`).
- **Host recipe.** Start after `RumbleTarget` has returned the controller.
  Its manager-vtable check already proves a PlayerController, so `+0x2E4` is
  `Player`.
  1. `player = Load32(controller + 0x2E4)`. Reject 0.
  2. Reject unless `Load32(player + 0x38) == controller` (the back link,
     `0x827E9EE4`).
  3. `pad = Load32(player + 0x58)`. It is valid only in 0–3.

  A stronger form is the exact inverse of the routing: search
  `GEngine +0x2B0/+0x2B4` for the player. An AI controller has no `Player`,
  since it is not a PlayerController, and `RumbleTarget` already drops it.
- **Requests every frame (proven).** The loop reaches `0x828847C8` for every
  pad, even when `XInputGetState` fails (`0x82884384 bne 0x82884790` →
  `0x828847C8`). A request is sent whenever a LocalPlayer with that
  ControllerId has an Actor with a manager. `GenerateForceFeedbackValues`
  zeroes both speeds first (`0x829EB6A4`/`0x829EB6AC`) and sends them even with
  no waveform. The only thing that skips the whole poll is the client's
  vtable+0x4C (`0x82884280`). That slot is `0x826DDF00 li r3,0` in the only
  client vtable with GetForceFeedbackManager at +0x28 (`0x82052DC8`). So one
  local player gives about 60 requests a second on its pad, in menus too,
  because the frontend has a PlayerController. Pads with no LocalPlayer get
  none.

## 2. Pause, slow motion and leaving the match

**Answer:** pad requests continue every frame while paused (**proven**). The
game's own waveform keeps playing on engine time through the pause until it
runs out (**inferred**: nothing stops it). The in-match pause is the pause-menu
object's flag (**proven**: it is the game's own test), and the host can read it
from fixed addresses.

- **Slow motion is a global engine time scale (proven).** `FEngineLoop` is the
  static at `0x835E0CA0` (built at `0x82419F10`, ticked by `0x82417890`). Each
  frame:
  ```
  82417B60  lfs f0,0x2c(r25)       ; time scale
  82417B64  stfs f31,0x28(r25)     ; real frame time
  82417B68  fmuls f30,f0,f31       ; scaled DeltaTime
  82417B6C  lwz r3,-0x2e80(r26)    ; GEngine
  82417B80  lwz r10,0xcc(r11) / bctrl  ; GEngine->Tick(scaled)
  ```
  `0x82417B2C`–`0x82417B58` ramp `+0x2C` toward `+0x30` by `+0x34` for `+0x38`
  frames. The TIMESCLE logic event (execute `0x8259BA60`, tag getter
  `0x8258DD40`) sets the target and the ramp (`0x8259BAE8`–`0x8259BB2C`). The
  force-feedback DeltaTime comes from this tick: Client tick `0x8272AF30` →
  `0x82884258` f1 → f29 → `ApplyForceFeedback` f1 → waveform tick
  `0x829293C8 fadds f0,f1,f0`. The step from `GEngine->Tick` to the Client
  tick is **inferred** from stock UE3, and the owner's slow-motion log shows
  the stretch. Host read: `float scale = *(be float*)(0x835E0CA0 + 0x2C)`;
  real dt is at `+0x28`.
- **The pause menu (proven).** It is a singleton at `*(0x835E5180)`, built
  lazily by `0x824CD158`. Its constructor `0x82AF9E00` zeroes `+0x64`/`+0x68`
  (`0x82AF9E44`). The game's own test is `0x82AFDEA0` IsPauseMenuUp:
  ```
  82AFDEB4  lwz r11,0x540(0x83740000) ; if non-zero: return 0
  82AFDEDC  lwz r3,0x5180(0x835E0000)  ; menu
  82AFDEF0  lwz r11,0x64(r3) / bne -> 1
  82AFDEFC  lwz r11,0x68(r3) / beq -> 0 ; else 1
  ```
  The `ig_pause` movie loader (`0x82AF596C`, string `0x821FF800`) and
  `0x82AF5FE0` set `+0x64` to 1. Pause-out (`0x82AF6850`, which plays
  `Play_SFX_UI_Pause_Out`) clears it (`0x82AF6954`). Pause-in (`0x82AF8920`,
  which plays `Play_SFX_UI_Pause_In`; callers `0x824F0C80`, `0x824F1384`,
  `0x82B4AC40`, `0x82B52144`) pauses the commentary and the announcer
  (`0x82B41960`, `0x82B746A8`).
- **What the pause stops (proven).** `UWorld::Tick` computes a tick reason
  (`0x82810094 bl 0x82afdea0` → `li r27,5`). `0x8280AD00` returns 0 ("no
  normal tick") for reasons 0, 3, 4, 5 and 6, or for `WorldInfo.Pauser`
  (`0x8280AC08`). So the match freezes. Pause-in never touches `FEngineLoop`
  (no `0x835E0CA0` reference in `0x82AF8920`–`0x82AF8AF4`), and the input poll
  `0x82884258` has no pause test. So `ApplyForceFeedback` keeps running with a
  non-zero DeltaTime.
- **This game never sets `bIsPaused` (inferred, strong).**
  - Only `PlayerController.SetPause` calls
    `ForceFeedbackManager.PauseWaveform` (`Engine.u`, two VirtualFunction
    calls).
  - No other package has the name `SetPause`, and the image has no
    `L"SetPause"` for a native event call.
  - The native writers of bit `0x40000000` at `+0x34` (`0x824F4E40`,
    `0x824FF544`, `0x82B25174`) belong to other classes (vtables
    `0x8200A28C`, `0x8201094C`).

  So during a pause the game keeps sending its waveform as it advances, then
  zeros. In Enhanced the hooks no longer write guest waveforms, so this is
  nearly always zeros.
- **Host pause read:** `menu = Load32(0x835E5180)`;
  `paused = Load32(0x83740540) == 0 && menu && (Load32(menu + 0x64) ||
  Load32(menu + 0x68))`. That is three loads from fixed addresses, cheap on
  every pad request. `+0x68` is a second "menu up" word (set at
  `0x82AF7CF4`). Include it, since the game does.
- **Leaving the match.** No single flag was found. These signals are all
  host-readable:
  1. Match end, the `0x82AE8388` hook (**proven** site).
  2. `GWorld` (`0x83721860`) changes on a map load (**inferred**: the frontend
     is its own map).
  3. The pause menu's quit and restart paths reset the time scale to 1.0
     (`0x82AFCBE8`, `0x82AFCDA8`, `0x82AFD3A0`: `stfs f0,0x2c(0x835E0CA0)`
     with `f0 = 1.0`). That is a sign the match is being torn down.

  Mixer effects are short (about 3 s of real time at most), so a stale effect
  dies on its own anyway.

## 3. Attacker at the hit hook `0x82AE392C`

**Answer (proven):** at `0x82AE392C`, `victim +0x95C` is this hit's attacker
on all three move paths. The damage record's `+0x1C` is **not** safe: on the
logic-event path it holds the victim itself.

- **Writers of `+0x95C`** (every `stw` with displacement 0x95C in `.text`):
  - `0x825586E8`, `0x8255871C`, `0x82558840`: reaction states.
  - `0x82ADCCB4`: the pending-hit reset `0x82ADCC78`, called only from
    `0x82AE3FA4`, after the damage call.
  - `0x82AE2758`: TakeHit, below.

  None lies in `0x82AE2E18`–`0x82AE3B60`. (`0x82AD51F0` writes a global,
  `0x8374095C`.) The direct callees before the hook (`0x82AD81D8`,
  `0x82AE7C48` AttemptMatchEnd, ...) do not write it.
- **`0x82AE2328` TakeHit(victim r3→r30, attacker r4→r22, ...)** queues the hit:
  `0x82AE2758 stw r22,0x95c(r30)`, then it fills the pending record at
  `victim+0x940` (`0x82AE27CC`; combat type at `0x82AE2858`). Because the
  record is `victim+0x940`, its `+0x1C` **is** `victim+0x95C`.
- **Pending paths:**
  - `0x82AE3B68`: reads the attacker (`0x82AE3B88 lwz r30,0x95c(r3)`),
    skips the damage if it is null (`0x82AE3BA4`), sets
    `attacker+0x938 = victim` (`0x82AE3BA8`), then calls the damage function
    with r4 = victim+0x940 (`0x82AE3BE0 bl 0x82ae2e18`).
  - The reaction state `0x82559178`: `0x82559264 addi r4,r3,0x940` /
    `0x8255926C bl 0x82ae2e18`. It reads `+0x95C` again right after, at
    `0x8255927C`.
- **Logic-event path** (APLYDMG `0x825BF5F8`): the record is a stack local
  (`0x825B7698` at `0x825BF658`). Its attacker is `GetTargetPawn(victim, 5)`
  = `victim+0x95C` (`0x825BF664 li r4,5` / `bl 0x82addb58`), which the event
  uses to get the attack data's combat type (`0x825BF6F0`). The record's
  `+0x1C` holds the victim.
- **Host recipe at `0x82AE392C`:** r28 = victim and r29 = record, both
  unchanged since the prologue.
  1. Go on only when `Load8(r29 + 0x18)` (combat type) is 1–18. Buffs and
     damage over time give 0.
  2. `attacker = CastToPawn(Load32(r28 + 0x95C))`. Skip if it is null or
     equal to r28.
  3. Apply `RumbleTarget(attacker)`.

## 4. Reversal: SETIASC category 6

**Answer:** category 6 is `eREVERSAL` (**proven**, enum). The pawn in r3 at
`0x8259A6CC` is the reverser (**proven**: it owns the stat). The reversed pawn
is its partner (**inferred**).

- **Enum (proven):** `SD_Animation` export 1035,
  `SD_AnimStateConcreteAttack.EAttackCategories`: 0 eNONE, 1 eREGULAR_HIT,
  2 eSTRONG_HIT, 3 ePOWER_MOVE, 4 eAERIAL_MOVE, 5 eCOUNTER, 6 eREVERSAL,
  7 eSUBMISSION. `GamePawn.AttackCategory` (Game.u export 1811) is a byte of
  that enum. SETIASC stores its category into the pawn byte `+0x770`
  (`0x8259A574 stb r11,0x770(r3)`). That `+0x770` is `GamePawn.AttackCategory`
  is **inferred**, because property offsets are not serialized.
- **Switch (proven).** Decoded from the `bdzf` chain at
  `0x8259A588`–`0x8259A5A4`. r11 = category − 1; the counters sit on
  `pawn+0x19B0`.

  | Category | Own counter | `+0x938` (LastHitVictim) counter |
  |---|---|---|
  | 1 regular hit | +0x90 | +0x94 |
  | 2 strong hit | +0x98 | +0x9C |
  | 3 power move | +0xA0 | +0xA4 |
  | 4 aerial | +0xA8 | +0xAC |
  | 5 counter | +0xB8 | none |
  | 6 reversal (`0x8259A6CC`) | +0xBC | none |
  | 7 submission | +0xB0 | +0xB4 |

  `GetSuccessRate_Reversals` = `+0xBC / (+0xBC + +0xB4 + +0xA4)`: reversals
  over reversals plus power moves and submissions taken (the missed chances).
- **Package evidence (proven counts, `CombinedGlobalCharacters.u`).** A SETIASC
  record is `\0SETIASC`, a float frame, int 1, then an int category. There are
  397 uses: 3 → 175, 6 → 142, 5 → 38, 7 → 28, 4 → 10, 1 → 2, 2 → 2. Next to
  the category-6 events are `fxreversal` (60 times, with `LeftForeArm`: a
  reversal effect), `GrabIrishWhip_Reversal_A_01`,
  `Rev_HeadShoulderHold_To_ArmDrag_V`,
  `CornerAttackHeadPunchReversal_V_Shared_01`, and the comment "A counter or a
  reversal". Category 5 sits next to `Cnt_*` and `GrndCounter*`.
- **Who r3 is (proven).** The execute casts the script's pawn
  (`0x8259A550 lwz r11,0x34(r4)` / `0x8259A560 lwz r3,0x44(r11)` /
  `0x8259A564 bl 0x82436528`) and bumps that pawn's own reversal count
  (`0x8259A6CC lwz r11,0x19b0(r3)`, `+0xBC`). So r3 is the pawn that made
  the reversal. Category 6 never touches `+0x938`, so the event names no
  partner.
- **Reversed pawn (inferred):** `CastToPawn(Load32(r3 + 0x81C))`
  (CurrentTarget), falling back to `+0x95C` and then `+0x938`, as `Partner`
  does. Skip it if it equals r3.
- **Timing (inferred).** The event fires at a frame inside a reversal
  animation state, so the reversal is under way: this is not an attempt. One
  event per reversal is likely but not proven.

## Owner checks (not settled statically)

- **Pause:** in Enhanced, pause during a big moment. Expected: the pause read
  turns on when the menu opens and off when it closes (one log line each), and
  the pad goes silent at once.
- **Reversal:** reverse one move, and get reversed once. Expected: one
  `reversal` line per reversal, with the reverser being the pad of the
  wrestler who caught the move.
- **Leaving:** quit to the main menu during a rumble. Expected: a `GWorld`
  change, if the mixer logs it.
