# Impact events: hook points for rumble beyond damage

Date 2026-10-04. Base XEX v5, read with `python scripts/disasm.py` on
`analysis/xex/default.pe`. Package facts come from LZO-decompressed copies of
`assets/Game/CookedXenon` in a scratch directory. Nothing was committed and the
game was not run. Builds on `docs/research/controller-vibration.md` (damage
function `0x82AE2E18`, play-rumble native `0x82B515B0`, gate `pawn+0x6FC` bit
0x100, `pawn+0x200` Controller).

Labels: **proven** means the instructions or package data show it directly.
**Inferred** means it follows from naming, layout or usage but was not traced
end to end.

## Summary

- Body-to-mat impacts go through one function: **`0x82B337B8`
  PlayGroundEvent(actor, type)**. It is called from the
  `AnimNotify_FmodGroundEvent` notify (27,427 notifies across 74 AnimSets) and
  from the script event `PFGEFMOD`. The type byte names the impact (slam
  regular/medium/heavy, half slam on the mat, stomp, rattle, barrier). This is
  the choke point for "they hit the mat".
- Moves mark their impact frames with the **camera-shake logic event `SETSCSK`**
  (2,663 uses in move scripts, often next to "Impact frame" or "LANDING
  SEQUENCE" comments). Its execute method `0x825A4EA0` takes the script's pawn
  and a shake size, so it can scale rumble.
- Finisher and extreme-signature wind-up: the attack-state enter method
  `0x824E2DE0` stores the move's combat type in `pawn+0x76D` and branches on 5
  and 15. Match end: `0x82AE7C48` AttemptMatchEnd, success path `0x82AE8388`.
  Reversal: the `SETIASC` category-6 branch `0x8259A6CC`.
- Pin count and kickout run in UnrealScript (`GamePinMiniGameBuff`). No native
  choke point was found.
- Every function named here is already in the generated function table
  (`generated/default/wweallstars_init.cpp`), so no manifest seed is needed.
  All hooks are `[[entrypoint.midasm_hook]]`s.

## How a script or notify reaches its pawn (proven)

Logic events (the 8-byte-tag script commands built by the factories) all have
the same layout: vtable +0x08 is the description, +0x10 the tag, and +0x44
`Execute(event r3, node r4)`. The vtable table was enumerated from the image
(153 classes). Every execute body finds its pawn the same way:
`lwz r11,0x34(r4)` / `lwz r3,0x44(r11)` / `bl 0x82436528` (a cast to the game
pawn class; null means not a game pawn). Examples: VIBRCONT `0x825E34EC`,
PFGEFMOD `0x82598B20`, APLYDMG `0x825BF60C`, SETIASC `0x8259A550`. The
AnimNotify `Notify(this r3, node r4)` methods use the same `node+0x34 → +0x44`
path (`0x82B33908`, `0x82B33948`). So `node+0x34` is the skeletal mesh
component and `+0x44` is its owner actor (inferred names).

**The other wrestler** (proven by `0x82ADDB58` GetTargetPawn(pawn, kind) and
`SD_AnimStateConcretePuppetMaster.ETargetTypes`): kind 0 LastHitVictim =
`pawn+0x938`, kind 1 CurrentTarget = `pawn+0x81C`, kind 5 LastAttacker =
`pawn+0x95C`. The game casts `+0x81C` to a pawn at `0x82AE3F60`. For "rumble
whoever is involved", rumble the event's pawn and its partner: `+0x81C`, or
`+0x938`/`+0x95C` when that is null. Rumble the partner more lightly.
`RumbleTarget` (gate bit plus manager vtable) then drops AI wrestlers.

Package tag counts (all in `CombinedGlobalCharacters.xxx`, decompressed):
SETSCSK 2663, EXEC_ROP (rope shake) 2440, APLY_DMG 1087, SETINTP
("animation rate until player hits ground") 1621, TIMESCLE 948, SETIASC 397,
SETCGSK (cage shake) 93, PFGEFMOD 88, PAG0AUDO (grunt) 37, ENDMACH 19, COMMPLEV
14, VIBRCONT 0.

## 1. Body-to-mat impacts

### A. `0x82B337B8` PlayGroundEvent(actor r3, type r4): central choke point

- **What** (proven): it traces down from the actor's location (`+0xE8..+0xF0`,
  Z minus a constant, `0x82B337C8`–`0x82B33880`, line check `0x827E0110` on
  GWorld `0x83721860`). It then plays the FMOD ground event for the surface
  material it hit (`0x82BF9098` at `0x82B338F4`). r4 is the event type.
- **Callers** (proven, complete): `0x82B3391C` (`b`, tail call) in
  `0x82B33900`, the `AnimNotify_FmodGroundEvent` notify (last slot of the
  vtable at `0x821BEC00`, `+0xAC`; it reads the type with `lbz r4,0x34(r3)`);
  and `0x82598B48` in PFGEFMOD's execute `0x82598B10`, which casts the pawn
  first and passes `clrlwi r4,(event+0xC),24`. Its only data reference is its
  .pdata entry.
- **Type enum** (proven, `Game.u` `AnimNotify_FmodGroundEvent.EAnimNotifyEventType`):
  0 Walk, 1 Shuffle, 2 Run, 3 Steps, 4 Stomp_Reg, 5 Stomp_Med, 6 Stomp_Heavy,
  7 Slam_Reg, 8 Slam_Med, 9 Slam_Heavy, 10 Rattle, 11 Barrier,
  12 Barrier_Impact, 13 HalfSlam_Mat.
- **How often** (proven, notify `EventType` byte at export data +0x18, across
  all AnimSets): 0: 13344, 1: 175, 2: 3014, 4: 3242, 5: 139, 6: 291, 7: 394,
  8: 529, 9: 1351, 10: 3948, 11: 2, 12: 25, 13: 972. Walking fires several
  times a second, so the hook must filter on type first. Slams (7, 8, 9, 13)
  fire about once or twice per slam move. PFGEFMOD scripts are commented
  "AUDIO Slam" with type 8.
- **Hook**: midasm at `0x82B337C4` (`lis r10,-0x7dfa`, the first instruction
  after `stwu`). Registers: `r1, r13, fpscr, r3, r4`. r3 and r4 are not
  written before `0x82B33848`/`0x82B33850`. Return void, so the sound still
  plays.
- **Per-pawn gate**: none in the function. On the notify path r3 is any
  skeletal mesh owner (wrestler, weapon, cinema actor), so the hook must cast
  it with `0x82436528` on a side context, as PFGEFMOD does, before reading
  `+0x200`/`+0x6FC`. After that, apply `RumbleTarget`.
- **Which wrestler**: the one whose animation carries the notify, usually
  the body that lands (inferred). Rumble it at full tier and its partner more
  lightly. "Stronger wins" absorbs the double fire when both animations carry a
  slam notify.
- **Suggested tiers**: 9 Slam_Heavy and 13 HalfSlam_Mat strong, 8 medium,
  7 light, 6 Stomp_Heavy and 12 Barrier_Impact light. Skip 0–5, 10 and 11
  until the owner tries them. Rattle (10, 3948 notifies) is probably ring and
  rope rattle (inferred).
- Confidence: **proven** (function, callers, enum, counts). The mapping from
  notify to wrestler is **inferred**.

- **Implemented** (pro vibration 03, 2026-10-04): `WweMatImpact`
  (`src/vibration/enhanced_hooks.cpp`, manifest `[[entrypoint.midasm_hook]]`
  at `0x82B337C4`). Confirmed in the generated code: the hook call sits right
  after the `stwu`, so side calls run below the function's own frame, and
  codegen emits `ctx.fpscr.disableFlushMode()` at the next float instruction
  (`lfs f11,0xf0(r3)`), so the side calls' FPSCR cannot leak into it. The
  filter (`src/vibration/mat_impact.h`) runs before any guest call. The
  partner (`+0x81C`, `+0x938`, `+0x95C`) is cast with `0x82436528` as well
  before `RumbleTarget` reads it, and is skipped when it is the same pawn.
  Not yet confirmed in a match: which wrestler's animation carries the notify,
  and whether replays fire it (owner check, pro-vibration ticket 04).

### B. `0x825A4EA0` camera-shake logic event (`SETSCSK`)

- **What** (proven): the execute method of "Creates a Camera Screen Shake"
  (vtable `0x82076DD0`, tag getter `0x82587A18` = `\0SETSCSK`, constructor
  `0x82587978`). It reads event `+0xC` duration (f1, `0x825A5108`), `+0x10`
  size (scales the offset amplitudes, `0x825A50E4`–`0x825A5104`) and `+0x14`
  frequency (scales the rates). The field names come from the property strings
  at `0x82080488`. It adds the shake to the camera's modifier
  (`0x8259D248`/`0x825913A0`, each called only from here). It never reads r4,
  so the shake itself is global.
- **How often** (proven): 2663 uses in move scripts (`TB_Ropes` 464,
  `Legends_Signatures` 396, `Grabs` 340, `Game2` 213, `Cage` 194, `Reactions`
  194, `Ground` 130, `Attacks_Aerial` 129, ...). Most common (duration, size,
  frequency): (1,2,1) ×196, (1,1,1) ×136, (1,3,1) ×124, (0.7,1.1,1) ×121,
  (0.2,0.6,0.8) ×101, (1,4,1) ×74. Comments next to them: "LANDING SEQUENCE"
  ×81, "Bounce the guy when he hits the ground" ×61, "Impact frame" ×25,
  "Good Hard RopeShaker value" ×19, "Finisher EventHorizon flag" ×14.
- **Hook**: midasm at `0x825A4EB8` (after the prologue, before
  `mr r29,r3`). Registers: `r1, r13, fpscr, r3, r4`. The pawn is
  `r4+0x34 → +0x44`, cast with `0x82436528`. Size `+0x10` gives the strength
  directly (≤1 light, 2–2.5 medium, ≥3 heavy).
- **Gate**: none per pawn. It fires for whichever script runs it, on the
  attacker side (Grabs, Signatures) and the victim side (Reactions, Ground).
  Rumble the pawn and its partner.
- Confidence: event identity and parameters **proven**. That r4 is the
  script's node is **inferred** from every sibling execute; the dispatcher was
  not found.
- Compared with A: B covers moves whose impact frame has a shake but no slam
  notify (strikes into ropes, aerial landings), and it carries a designer
  strength. A is closer to the physical "body on mat" moment. Both together
  double-fire on many slams; "stronger wins" handles that.

### C. Secondary impact sources (not recommended first)

- `0x825847D8` EXEC_ROP "Execute Rope Shake" (2440 uses): rope impacts. Same
  node-to-pawn pattern. **Inferred** value.
- `0x82B342B8` PlayGrunt(audio r3, pawn r4, type r5), from the
  `AnimNotify_FmodGruntEvent` notify `0x82B33928` (3955 notifies) and PAG0AUDO
  `0x82598BB4`. Enum `EAnimNotifyGruntEventType`: 1 Fall, 4 Impact_Regular,
  5 Impact_Heavy, 6 Impact_Heavy_AlwaysPlay, 9 Effort_Slam. Impact grunts
  overlap the damage pulse. **Proven** wiring.
- `0x82597028` SETCGSK cage shake (93) and `0x8259B6D8` SENDCASH: cage
  matches only.

## 2. Match moments

### D. Finisher and extreme-signature start (wind-up): `0x824E2EB0`

- **What** (proven): `0x824E2DE0(state r3→r23, pawn r4)` is an attack-state
  enter method. It is referenced from 6 AnimState vtables (`0x8203FBCC`,
  ...), and the reaction path calls it (`0x82559204`, ...). It casts the pawn
  (`0x824E2E10` → r29) and copies the state's combat type:
  `lbz r8,0x43(r23)` / `stb r8,0x76d(r29)` (`0x824E2E6C`–`0x824E2E78`). On 5
  (FINISHER) it bumps stat 0x1F (`0x824E2EA0`) and branches to `0x824E2EB0`.
  On 15 (EXTREME_SIGNATURE) it falls through to `0x824E2EB0`. Both then call
  `0x824DFDF0(0x835E5258, 0)`.
- **Hook**: midasm at `0x824E2EB0` (a branch target). Registers:
  `r1, r13, fpscr, r29, r23`. Read the type with `lbz r23+0x43`, because the
  `bl` at `0x824E2EA0` clobbers r8. Attacker = r29, victim = `r29+0x81C`.
- **How often**: once per attack state with type 5 or 15. A finisher made of
  several states may enter more than once (**inferred**), so debounce
  (for example 1 s per pawn). This is a few times per match.
- Confidence: **proven** (site, registers, types).

### E. Match end (pin, submission, KO, ...): `0x82AE8388`

- **What** (proven): `0x82AE7C48` AttemptMatchEnd(manager r3→r21,
  pawn r4→r28, winType r5→r22). It returns at once if `manager+0x54` is set
  (`0x82AE7C5C`), plays `SFX_UI_MatchEnd` (`0x82AE7CA8`, `0x824DB3C0`), then
  switches on `EMatchWinTypes` (1 Pin, 2 Submission, 4 Tables, ... 13 Knockout;
  table `0x82AE7D04`). Win cases set r23 = `GetTargetPawn(r28, 5)`, the last
  attacker, that is the winner. Only `0x82AE8388` returns 1: it stores the
  winner at `manager+0x130` and `r28+0x76F` at `manager+0xDD`. Failed attempts
  return 0 at `0x82AE8378`.
- **Callers**: `0x82AD86A0` (pawn method `0x82AD8678(pawn, winType)`), the
  table-break paths `0x82597F60`/`0x82597FD8`/`0x82598038` (type 4), and
  scripts through ENDMACH ("Try to win the match", in finisher and submission
  scripts).
- **Hook**: midasm at `0x82AE8388`. Registers: `r1, r13, fpscr, r21, r23,
  r28`. Rumble every local pad: a long one for the winner (r23), a heavy
  decaying one for the loser (r28).
- **How often**: once per match (**inferred**; the attempt can repeat, but
  the success path should not).
- Confidence: **proven** (site and success path). The loser/winner roles are
  **inferred** from `GetTargetPawn(...,5)`.

### F. Reversal: `0x8259A6CC`

- **What** (proven): SETIASC "Increment the Stat count for an attack
  category" (execute `0x8259A540`, 397 uses). It stores the category at
  `pawn+0x770` and switches on category−1. Category 6 (`0x8259A6CC`)
  increments `(pawn+0x19B0)+0xBC`. That counter is the numerator of
  `GetSuccessRate_Reversals` (native `0x82A8E720` → vtable+0x404
  `0x82AD8A88`: `0xBC / (0xBC + 0xB4 + 0xA4)`). So category 6 = the pawn
  landed a reversal. Uses of category 6: 142 (`Game2` 88, `Grabs` 45).
- **Hook**: midasm at `0x8259A6CC` (a branch target). Registers: `r1, r13,
  fpscr, r3` (the reversing pawn, already cast). Partner `r3+0x81C`.
- Confidence: counter **proven**. "Category 6 = reversal" is **inferred**
  from the success-rate formula. Counters (not reversals) have no separate
  counter here.
- **Update (ticket 01):** category 6 is `eREVERSAL` (proven by enum) and
  category 5 is `eCOUNTER`, with its own counter `+0xB8`. See
  `controller-vibration.md`, "Full-coverage vibration research (ticket 01)",
  section 4.

### G. Pin count and kickout: no native choke point (not found)

- The pin is a buff, `GamePinMiniGameBuff` (UnrealScript in `GameBuffs.u`:
  `Tick`, `OnAdd`, `OnRemove`, `RecieveEvent`). Its defaults hold sub-objects
  `PinCount1/2/3`, `PinCount{1,2,3}Stinger`/`Commentary`, `PinKickOut*` and
  `PinMatchOver*`. The interface natives (`GameThreeCountInterface`
  Init/Tick/OnRemove, native table `0x8353E028`–`0x8353E038`) point at shared
  generic thunks (`0x82A66060`/`0x82A66298`/`0x82A66380`), not count logic.
- Leads, all **inferred**: (a) the FMOD event-play natives used by
  `GameSoundCueInterface.PlaySound`/`GameFModCommentaryEventInterface.PlaySound`,
  filtered by event name; (b) the pin cover's body drop already fires ground
  notifies (A); (c) a pin win reaches E with win type 1.
- The commentary types `PINCOUNT1..3` (104–106), `PINESCAPED` (36/37) and
  `REVERSAL` (24) exist (`GameCommentarySystem.EGameCommentaryType`, 142
  values). Commentary is played through data tables (`0x82B41B70`/`0x82B451B0`)
  and is throttled, so it is not a reliable rumble source.

## Ranked recommendation

1. **`0x82B337C4`, ground event (A).** It is one site for every slam, drop,
   landing and half slam on the mat, on whichever wrestler's animation carries
   it. Filter types 7/8/9/13 (plus 6/12 light), cast with `0x82436528`, rumble
   the pawn and its partner (`+0x81C`). This answers "rumble on the mat
   impact, ours or the opponent's" most directly.
2. **`0x825A4EB8`, camera shake (B).** It is the designers' own "impact
   frame" marker, with a strength (`+0x10` size). It covers rope, aerial and
   strike impacts that have no slam notify. Ship it after A and let "stronger
   wins" merge the overlaps.
3. **`0x824E2EB0` finisher/signature wind-up (D) and `0x82AE8388` match end
   (E).** Both are rare, proven, single-site big moments with both pawns in
   registers. Reversal (F, `0x8259A6CC`) is a cheap fourth.

Risks to check in the owner's match: replays (`SD_Replay`) and cinematics may
replay notifies and script events, so a hook may need a "match in progress"
check. Footstep-type notifies are frequent, so filter on the type before any
guest call.
