# Unlock everything

Date 2026-10-11. The owner wants a launcher toggle, "Unlock everything", that
opens every wrestler, arena, attire and other unlockable. This study finds what
the game locks, where it keeps and checks the unlock state, and the simplest
reversible way for the recomp to unlock it. **Every guest address here is a TU2
address** (ADR-0022).

Tools used:
- Ghidra through the MCP server: program `default.xexp` in project
  `ghidra/WWEAllStarsTU2`, opened read-only. Decompiles and xrefs below come
  from it, mostly through `ghidra_eval` with the `DecompInterface`.
- `python scripts/disasm.py` on `analysis/xex/tu2.pe` for the instruction-level
  hook sites.
- Throwaway Python over `analysis/xex/tu2.pe`, `analysis/xex/default.pe` and the
  extracted `assets/Game/Config/Xenon/Cooked/Coalesced.ini` and
  `assets/Game/Localization/Coalesced.int`. Nothing was installed or changed.

A claim marked **confirmed** was read in decompilation, disassembly, game data
or source. **Hypothesis** means plausible but not run or measured.

## Answer

1. **What is locked (confirmed from game data).** The unlockables manager has
   six item types (§2.3): wrestlers (10 characters), venues (3 arenas), attires
   (alternate ring gear, about 45), moves (CAW finishers), intros (entrances),
   and movies (always open). Fantasy Warfare matches are gated separately, by a
   progress counter (§2.5). The localized popup list is `[Unlockables]` in
   `Coalesced.int` (§1.2).
2. **Normal unlocks.** Fantasy Warfare wins unlock characters, Path of
   Champions unlocks attires, created-Superstar wins unlock the arenas (§1.1).
   The game has a **CHEATS** screen in Extras ("It's a secret to everyone...").
   It takes four button codes, **including `UnlockAll`**, read from
   `Coalesced.ini` (§1.3). TU2 still contains the whole `UnlockAll` handler,
   byte-identical to base v5 apart from relocation (§1.4).
3. **Where the state lives (confirmed).** The **save** holds one byte per item
   in byte arrays on the singleton `0x83744E98` (sub-object `+0x68`). At run
   time each item has a **`bUnlocked` bit (`0x80000000` at item `+0x18`)** in
   five lists on the unlockables data object (`[0x8374C164]+0x38`). The UI asks
   through Scaleform `IsItemLocked` (`0x82BE71C8`), which only reads that bit.
   The refresh `0x82BE7560` rebuilds the bits from the save bytes. It also sets
   every bit when the manager flag **`[mgr+0x34] & 0x80000000`** is set (§2).
4. **The game's own `UnlockAll` is mostly in memory only (confirmed).** It sets
   that manager flag and refreshes. The save byte arrays are written in **one
   place only**: the pending-unlock commit `0x82BE72E0`. `UnlockAll` adds
   nothing to the pending list, so the flag never reaches the item bytes.
   **One exception:** it also raises the Fantasy Warfare progress counter
   (`0x82BDAD30` on `[0x83744F48]+0x28`, field `+0xBC`). That counter has a
   dirty flag, so it likely persists (§2.5).
5. **Recommendation (§4).** Add two midasm hooks and a startup bool,
   `wwe_unlock_everything`, default off:
   - **A.** At `0x82BE7570`, in the refresh, while the setting is on: OR
     `0x80000000` into `[r3+0x34]`. This is the game's own flag, so every item
     type opens and the save bytes are never touched.
   - **B.** At `0x82B9C728`, in the Fantasy Warfare menu, right after the
     progress getter: `r3 = max(r3, r23)`, where `r23` is the match count. FW
     matches show as open, and the saved counter stays as it is.

   Turning the setting off and restarting gives back the real save state.
   Nothing is written to the save that the original game would not write.
6. **Main risk.** While the setting is on, **new real unlocks are not
   recorded**. The post-match award pass `0x82BE7A00` skips items whose bit is
   already set (§3.1). Fixed by ticket 03 (§4.3). There is no achievement
   for unlocking things, and the online peers' rosters are a hypothesis (§3.3).

## 1. What the game locks and how it opens

### 1.1 Normal unlock conditions (web sources)

The Xbox 360 cheat pages agree on these (GameRevolution, Supercheats, CheatCC,
Cheat Happens):

- **Characters.** Each is unlocked by winning a Fantasy Warfare match: Drew
  McIntyre (vs Roddy Piper), Eddie Guerrero (vs Rey Mysterio), Edge (vs Bret
  Hart), Jack Swagger (vs Sgt. Slaughter), Jimmy Snuka (vs Kane), Kane (vs
  Jimmy Snuka), Mr. Perfect (vs The Miz), Sgt. Slaughter (vs Jack Swagger),
  Shawn Michaels (vs The Undertaker), The Miz (vs Mr. Perfect) [gr360].
- **Arenas.** All-Stars Arena: win one match with a created Superstar.
  Classic SmackDown: finish any Path of Champions with a created Superstar.
  SummerSlam: win 10 matches with a created Superstar [gr360].
- **Attires.** Finishing Path of Champions with a Legend or Superstar unlocks
  their alternate attire. Some have a second or third attire
  [supercheats-pc][cheatcc]. The in-game tips agree:
  `CT_13_xbox360_Description` reads "Complete Path of Champions with your
  favorite Legend or Superstar to unlock alternate ring gear for them."
  (`Coalesced.int`).
- **Cheat codes.** Austin & Punk attires L,L,R,R,U,D,U,D. Roberts & Orton
  U,D,L,R,U,U,D,D. Savage & Morrison D,L,U,R,R,U,L,D. Unlock all:
  Left,Y,Down,Left,Y,X,Left,X,Y,Down,Right,X,Left,Up,X,Right [gr360].
  Cheat Happens adds: "Note: This code is disabled in newer game updates."
  [ch360]. §1.4 finds no such change in TU2's code.
- **DLC** characters (`title-updates-and-dlc.md`) are licensed content, not
  unlockables, and are out of scope here.

### 1.2 The unlockables list in game data (confirmed)

`assets/Game/Localization/Coalesced.int`, section `[Unlockables]`, holds the
popup text for every unlockable. The header is `HeaderName = YOU HAVE
UNLOCKED:`. It lists:

- 10 characters (`EIC_DrewMcIntyre` … `EIC_TheMiz`);
- 3 arenas (`Arena_003` All Stars, `Arena_005` Classic SmackDown,
  `Arena_006` SummerSlam);
- about 45 attires (`EIC_<Name>_AA1`, `_AA2`, `_AA3`);
- 30 finishers (`EIC_RKO`, …);
- 30 entrances (`EIC_<Name>_Entrance`);
- 15 Fantasy Warfare matches (`EIC_FWTitle_*`);
- `All = Everything!`, the `UnlockAll` popup (§1.3).

The same file has the Extras entries `Cheatcodes = CHEATS` /
`Cheatcodes_Desc = It's a secret to everyone...` and `UnlockablesList =
UNLOCKABLES`. **So the codes go into a CHEATS screen in Extras.** The cheat
sites say "at the main menu" (hypothesis: older wording, or the screen
is reached from there).

### 1.3 The cheat codes are config data (confirmed)

`assets/Game/Config/Xenon/Cooked/Coalesced.ini`, section
`[Game.GameCheatCodeManager]`, lists six codes as `Face*`/`Button*` arrays:

- `ColdestSnake` = U,D,L,R,U,U,D,D (Roberts & Orton);
- `MostCharismatic` = L,L,R,R,U,D,U,D (Austin & Punk);
- `SuperiorLifestyle` = D,L,U,R,R,U,L,D (Savage & Morrison);
- `UnlockAll` = L,Y,D,L,Y,X,L,X,Y,D,R,X,L,U,X,R;
- `AlmostUnlockAll`, `AwardAlmostAllStoryBadges`: developer codes.

The executable reads only the first four. The constructor `0x82C1A4C8` calls
`GConfig->GetArray("Game.GameCheatCodeManager", <key>, …)` four times, through
the vtable of `[0x8370148C]`:

```
(**(code **)*DAT_8370148c)(DAT_8370148c, L"Game.GameCheatCodeManager"@0x82240814,
                           L"ColdestSnake"@0x822407F8, this+0x14, 0x8370B540);
... L"MostCharismatic" -> this+0x20, L"SuperiorLifestyle" -> this+0x2C, L"UnlockAll" -> this+0x38
```

The cheat path (confirmed, TU2):

- `0x82C1B3F8` is a native that takes a `key` parameter (`L"key"` at
  `0x82240B88`). It is referenced from the native table at `0x82240670`. It
  passes each key to `0x82C1AFB0`.
- `0x82C1AFB0` pushes the key into a ring buffer (`this+4`, index
  `this+0x10`). It then tests the three attire codes through `0x82C1AD88`:
  `(10,0x12)`, `(0x13,0xE)`, `(0x1B,3)`, all of type 4 (ATTIRE). After that it
  tests `UnlockAll` with the matcher `0x82C1A8A8`.
- `0x82C1AD88` (attire code) works only if one of the pair is still locked
  (`0x82BE6798`). It then appends that character's attire entries to the
  pending list (`0x82AFC278`), shows the popup (`0x82C1A9E0`), and commits
  (`0x82BE72E0`, then `0x82BE78E8`). **The attire codes therefore write the
  save, like a real unlock.**

### 1.4 `UnlockAll` in TU2 (confirmed)

The match branch of `0x82C1AFB0`:

```c
uVar4 = DAT_83744f48; if (!uVar4) uVar4 = DAT_83744f48 = FUN_82af5898();  // 0xC24-byte object
FUN_82af5908();
uVar1 = FUN_82af5ec0();                    // number of FWMatch%02d objects (0x821FFBE0)
FUN_82bdad30(uVar4 + 0x28, uVar1);         // FW progress = all; sets dirty +0xC0
... popup "Unlockables"/"All" (0x82139B34 = L"All") ...
*(uint *)(iVar2 + 0x34) |= 0x80000000;     // iVar2 = unlockables manager (0x82BE6288)
FUN_82be7560(iVar2);                        // refresh all lists
FUN_82be72e0(iVar2);  FUN_82be78e8(iVar2);  // commit pending (none added), clear it
```

TU2 did not change this code. A masked byte compare of TU2 against base v5
(`analysis/xex/default.pe`) finds `0x82C1AFB0` = base `0x82C17E20`, 162/162
words. The same holds for `0x82C1A8A8` = `0x82C17718` (65/65), `0x82BE6DB0` =
`0x82BE37D8` (54/54) and `0x82BE6328` = `0x82BE2E58` (107/107). The "disabled
in newer updates" note [ch360] therefore does not come from a TU2 code change.
Hypothesis: it refers to the PS3 version, or it is wrong. Nobody has entered
the code in this recomp yet. That is the cheapest first check (§5).

Xenia Canary's patch file for title `54510866` has only "Skip Audio Loop Stuck
Code" and no unlock patch [xenia-patch].

## 2. Where the unlock state lives

### 2.1 Objects (confirmed)

| What | Where | Evidence |
|------|-------|----------|
| Unlockables manager (UObject; `UGameUnlockablesManager`, hypothesis from the name string at `0x821E9028`) | `[0x8374C164]`, getter `0x82BE6288` (constructs on first use via `0x8262AB70`) | decompile of `0x82BE6288` |
| Unlock-all flag | `[mgr+0x34] & 0x80000000` | `0x82BE6DB0` and its 4 siblings, `0x82C1AFB0` |
| Unlockables data (`UGameUnlockables`, hypothesis) | `[mgr+0x38]`, ensured by `0x82BE6710` | `0x82BE6710` |
| Item lists (TArray: data, count) | data `+0x4C/+0x50` WRESTLER, `+0x58/+0x5C` VENUES, `+0x64/+0x68` ATTIRE, `+0x70/+0x74` MOVES, `+0x7C/+0x80` INTROS | §2.3 |
| Item record | stride `0x38`: name FString `+0x0C/+0x10`, `bUnlocked` = bit `0x80000000` of `+0x18`, id `+0x20`, unlock conditions `+0x2C/+0x30` | `0x82BE64D8`, `0x82BE67E0`, `0x82BE7A00` |
| Pending ("newly unlocked") list | data `+0x40/+0x44/+0x48`, stride `0x38` | `0x82BE7A00`, `0x82BE72E0` |
| Game save object | `[0x83744E98]`, `0x3BF28` bytes (`0x82BCC900`); unlock bytes in sub-object `+0x68`: `+0x08` WRESTLER, `+0x14` VENUES, `+0x44` INTROS, and the others; dirty `+0x50` | setters `0x82BC87B8`, `0x82BC8788`, `0x82BC8838`, … |
| Fantasy Warfare progress | `[0x83744F48]+0x28`, field `+0xBC`, dirty `+0xC0`; get `0x82BDAD28` (`lwz r3,0xbc(r3); blr`), set `0x82BDAD30` | §2.5 |

### 2.2 Refresh from the save (confirmed)

`0x82BE7560` ensures the data object (`0x82BE6710`) and then calls five
per-list refreshers: `0x82BE6DB0` VENUES, `0x82BE6CA8` WRESTLER, `0x82BE6EB8`
ATTIRE, `0x82BE6FC0` MOVES and `0x82BE70C8` INTROS. All five have the same
shape:

```c
// 0x82BE6DB0 (VENUES list, save bytes at sub +0x14)
if (FUN_82bc8778(save + 0x68, item->id) == 1 || (mgr->flags34 & 0x80000000) == 0x80000000)
    item->flags18 |= 0x80000000;
```

```
82BE6E30  bl 0x82bc8778          ; save byte for item id
82BE6E34  cmpwi cr6,r3,1 ; beq 0x82be6e4c
82BE6E3C  lwz r11,52(r30)         ; [mgr+0x34]
82BE6E40  rlwinm r10,r11,0,0,0 ; cmplw cr6,r10,r26 (0x80000000) ; bne 0x82be6e68
82BE6E4C  ... item+0x18 |= 0x80000000
```

The refresh only ever sets bits. It never clears one. It has three callers:

- the save-load completion `0x82BCDC38`, registered as a callback at
  `0x82BCF4FC`. It copies the loaded data in, then
  `FUN_82be6710(); FUN_82be7560(mgr)`;
- the front-end setup `0x82B96898` (at `0x82B96C18`, followed by
  `0x82BE78F8`). Which screen this is remains a hypothesis;
- the `UnlockAll` cheat.

The reset `0x82BE6328` clears every bit. It leaves 6 default MOVES set
(indices 1, 2, 0xB, 0x10, 0x11, 0x1A) and **does not touch `[mgr+0x34]`**.
`0x82BCDBF8` calls it, and so does the sign-in change handler `0x82B4A190`.

### 2.3 Type names and the UI check (confirmed)

The static initialiser `0x83455F50` fills the type-name table `0x83743D78`
(6 FStrings, stride `0xC`). Index 0 is `EFC_WRESTLER`, then `EFC_VENUES`,
`EFC_MOVES`, `EFC_MOVIES`, `EFC_ATTIRE` and `EFC_INTROS` (strings at
`0x821F841C` down to `0x821F83E0`).

The Scaleform callback dispatcher `0x82B51098` compares the method name
(`0x82C36108`). For `L"IsItemLocked"` (`0x82216964`, at `0x82B51C38`), it
calls `0x82BE71C8(mgr, type, itemName)`. That maps the type name to an index
and dispatches:

| idx | type | lookup | list |
|-----|------|--------|------|
| 0 | EFC_WRESTLER | `0x82BE67E0` | `+0x4C` |
| 1 | EFC_VENUES | `0x82BE68A0` | `+0x58` |
| 2 | EFC_MOVES | `0x82BE6A20` | `+0x70` |
| 3 | EFC_MOVIES | returns 1 | – |
| 4 | EFC_ATTIRE | `0x82BE6960` | `+0x64` |
| 5 | EFC_INTROS | `0x82BE6AD8` | `+0x7C` |

Each lookup returns `item->flags18 >> 31`, or 1 if the item is not found:

```c
// 0x82BE67E0
if (FUN_82c34228(item->name, name) == 0) return items[i].flags18 >> 0x1f;
...
return 1;
```

There is also an id-based version, `0x82BE6798(mgr, type, id)` →
`0x82BE64D8`/`6538`/`65F8`/`6598`/`6658`. Only the attire cheat calls it.
The same dispatcher handles `GetNextUnlockedCostume` (`0x82216890`) and
`SetCurrentCostume`. **So the UI sees exactly the in-memory bits.** Setting
them unlocks the content in the menus. Whether every screen goes through these
natives, or some UnrealScript reads `bUnlocked` directly, is a hypothesis.
Either way, both read the same bit.

### 2.4 Writes to the save's unlock bytes (confirmed)

The six byte setters (`0x82BC87B8`, `0x82BC8788`, `0x82BC8758`, `0x82BC8838`,
`0x82BC87E8`, `0x82BC8818`) have **exactly one caller each, all inside
`0x82BE72E0`**. That function commits the pending list: for each pending entry
it sets save byte = 1 and the item bit. The pending list is filled by:

- the post-match award pass `0x82BE7A00`, called from the results screen
  `0x82AFD6B0` at `0x82AFDC38`, which then commits at `0x82AFDD60`;
- the attire cheat;
- `0x82B98450` and `0x82AFD5C0`, which copy the list for the popups.

**So setting `[mgr+0x34]` or the item bits can never write the save's unlock
bytes.** The bytes stay as the player earned them.

### 2.5 Fantasy Warfare progress (confirmed, with one hypothesis)

The FW menu populate `0x82B9C690` works like this:

```
82B9C6E0  bl 0x82af5ec0        ; count of FWMatch%02d
82B9C6EC  mr r23,r3            ; r23 = match count
82B9C720  addi r3,r11,40       ; [0x83744F48]+0x28
82B9C724  bl 0x82bdad28        ; progress (+0xBC)
82B9C728  lis r11,-32222
82B9C72C  mr r19,r3            ; r19 = progress
...
82B9C888  cmpw cr6,r19,r26     ; r26 = match index
82B9C894  stb r25,248(r31)     ; flag = 0
82B9C898  bgt 0x82b9c8a0
82B9C89C  stb r14,248(r31)     ; flag = 1 (locked) when progress <= index
```

Match *i* shows as locked unless progress > *i*. In mode 3, `0x82BE7A00`
raises progress only upward (`if (get < idx+1) set(idx+1)`). The object at
`0x83744F48` is constructed by `0x82BDC628` on the same base `0x82BCFA70` as
the save object. Hypothesis: it is part of the saved profile, so the
`UnlockAll` write persists.

## 3. Risks and interactions

### 3.1 Progress made while unlocked is not recorded (confirmed by code)

`0x82BE7A00` evaluates an item's conditions only when its bit is clear:

```c
if (((items[i].flags18 & 0x80000000) == 0) && 0 < items[i].conditionCount) { ...
    FUN_82afc278(pending.Add(), &items[i]); items[i].flags18 |= 0x80000000; }
```

With every bit set, a Path of Champions finished during an "unlock everything"
session adds nothing to the pending list. Nothing is committed, so the real
unlock is lost. This is not corruption, and the save keeps everything it had.
Hook B leaves the FW progress counter alone, so real FW progress is still
recorded.

### 3.2 The real save

| Approach | Touches the save? | Reversible? |
|----------|-------------------|-------------|
| Hooks A+B (§4) | No. Unlock bytes are written only by the pending commit (§2.4). The FW counter is never set. | Yes: turn off and restart. |
| The game's own `UnlockAll` | FW progress counter, yes (§1.4). Item bytes, no. | Items yes (session only); the FW progress counter no. |
| Poke item bits each tick | No | Yes, after a restart |
| Write the save bytes / a completed save | Yes, permanently | No, unless the save is backed up |

### 3.3 Online play (hypothesis)

All unlockable content is on the disc (§1.2). Each peer picks from its own
roster, and a peer's choice reaches the other as a character or attire id.
No code that checks the remote player's unlocks was found. The online notes
(`docs/research/online-mode-feasibility.md`, `.scratch/online-play/`) do not
mention unlocks. Hypothesis: a peer with everything unlocked can play one
without, since the second peer loads the same packages from its own disc.
This needs a two-instance test with the setting on one side only.

### 3.4 Achievements (web source + hypothesis)

No achievement rewards unlocking items. Two relate to Fantasy Warfare: "Old
School" ("Completed all Fantasy Warfare matches as a WWE Legend") and "Legend
Killer" ("… as a WWE Superstar") [sdh]. With hook B, every FW match can be
picked, and a win likely still counts toward those (hypothesis). The award pass
does not involve achievements.

### 3.5 Scene runs

Scene runs navigate menus by script (`scripts/scene_runner.py`). An unlocked
roster changes the character and arena grids. The setting must be forced off
in scene runs. It is not an image Enhancement, but it belongs in the same
force-off list: `ENHANCEMENTS` in `scripts/scene_runner.py:53-57`, and
`kEnhancements` in `src/native/native_renderer.cpp:1515-1532`.

## 4. Implementation options, ranked

### 4.1 (a) Hooks on the game's own flag. Recommended

Two `[[entrypoint.midasm_hook]]` entries in `wweallstars_manifest.toml`, like
`Wwe60fpsEntrances` (`wweallstars_manifest.toml:445-454`). No codegen seed is
needed, because both functions are already generated.

- **A, `WweUnlockEverything` at `0x82BE7570`** (`mr r31,r3` in the refresh;
  `r3` = manager). Registers `["r3"]`. If `wwe_unlock_everything` is on,
  `StoreU32(base, r3+0x34, LoadU32(base, r3+0x34) | 0x80000000)`. The five
  refreshers that follow then set every bit, exactly as after the cheat, and
  every later refresh (save load, front end) does the same. The reset
  `0x82BE6328` does not clear the flag. It covers every type the game itself
  covers, including attires, CAW moves and entrances.
- **B, `WweUnlockFantasyWarfare` at `0x82B9C728`** (after `bl 0x82bdad28`).
  Registers `["r3", "r23"]`. If on, `r3 = max(r3, r23)`. Only the FW menu sees
  full progress. The saved counter and `0x82BE7A00`'s progress update are left
  alone.

Off means the hooks do nothing. Since the flag is only ever set, never cleared
(§2.2), turning the setting off needs a restart. This is a `startup` row like
`wwe_60fps_entrances` (§5).

Effort: about 30 lines of C++ (a new file, or next to the tick hooks in
`src/game_tick_log.cpp`), 2 manifest entries, 1 cvar, 1 registry row, then a
schema export and a codegen run.

### 4.2 (b) Patch the item bits in memory from the host

Every engine tick (the existing `0x824178CC` hook), walk the five lists of
`[[0x8374C164]+0x38]` and set the bits; for FW, as in B. It needs no new
refresh hook, but it duplicates the game's layout in host code, races the
reset for one tick, and has the same §3.1 freeze. Only worth it if A turns
out to miss a path.

### 4.3 Fix for §3.1 (done, ticket 03)

Sketch: hook the entry of `0x82BE7A00` and the return site `0x82AFDC3C`. At
entry, rebuild the bits from the save bytes only: clear all bits, then run the
refreshers with the flag clear. On return, set the flag again and refresh.

What was built (2026-10-11, `src/unlock_everything.cpp`), confirmed in Ghidra
(`WWEAllStarsTU2`) and `disasm.py`:

- **Function wrappers, not midasm hooks.** `REX_HOOK_RAW(sub_82BE7A00)` and
  `REX_HOOK_RAW(sub_82C1AD88)` replace the weak generated symbols (the same
  pattern as `src/d3d_census.cpp`) and call the original through `__imp__`. A
  wrapper has `ctx` and `base`, so it can call the guest's own reset
  `sub_82BE6328` and refresh `sub_82BE7560`. No manifest entry or seed: all
  four functions are already generated.
- **Earned view.** With the setting on, each wrapper clears bit `0x80000000` of
  `[mgr+0x34]`, calls the reset, then the refresh while hook A stands down (a
  thread-local flag), so the bits match the save bytes plus the 6 default
  moves, as after a save load. It then restores the guest argument registers
  `r3`..`r10` and calls the original. Afterwards it calls the refresh again
  (hook A sets the flag, every bit comes back) and restores `r3`, the
  original's result.
- **The reset zeroes two more words** (`stw r10,52(r11)` / `stw r10,56(r8)` at
  `0x82BE6334`/`0x82BE633C`: data `+0x34` and `+0x38`). The commit
  `0x82BE72E0` uses them (`+0x34 += +0x38`, capped at `+0x3C`), so the wrapper
  saves and restores them around the rebuild.
- **The pending list is safe.** Neither the reset nor the refresh touches data
  `+0x40..+0x48`. The refresh only sets bits.
- **Only one caller.** `0x82BE7A00` is called only from the results screen
  `0x82AFD6B0` (`bl` at `0x82AFDC38`, `r3` = manager from `0x82BE6288` at
  `0x82AFDC30`). After it returns, the screen copies the pending list for the
  popup (`0x82AFD5C0`, a plain TArray copy through `0x82AFC278`), commits it
  (`0x82BE72E0` at `0x82AFDD60`) and clears it (`0x82BE78E8`). None of these
  read the item bits, so restoring every bit right after the pass is safe.
  `0x82B98450` is the same kind of copy. The popups therefore list only the
  pending entries, which are the newly earned items.
- **Attire cheat.** `0x82C1AD88` fills the pending list only if one of the two
  attires is locked (`0x82BE6798`, the item bit), so it gets the same wrapper.
  `0x82C1AFB0` calls it three times per key press on the CHEATS screen. Each
  call costs two extra refreshes, which is cheap.
- **Not wrapped.** `UnlockAll` (`0x82C1AFB0`) adds nothing to the pending list.
- The VENUES loop of `0x82BE7A00` does not set the bit of an item it adds
  (only the commit does). This is the game's own behaviour, and it is
  unaffected.

Still to check in play (owner, ticket 03): earn one unlock with the setting on,
see it in the popup, turn the setting off, restart, and find it still unlocked.

### 4.4 (c) Write a completed save. Not recommended

This would set every byte in the save's unlock arrays and the FW counter, then
let the game save. It is permanent, it is not undone by the toggle, and a
mistake could damage the owner's real save.

## 5. How the setting fits the existing path (confirmed)

Everything below follows `wwe_60fps_entrances`, a startup bool:

1. **Cvar:** `REXCVAR_DEFINE_BOOL(wwe_unlock_everything, false, "WWEAllStars",
   …)` in `src/settings_cvars.cpp`, as at `src/settings_cvars.cpp:41`.
2. **Registry row** in `PlayerSettings()` (`src/settings/registry.h:57-174`;
   example at `registry.h:124-126`). The tab is likely GAMEPLAY or a new
   EXTRAS (owner's choice). Mark it `/*startup=*/true` so the launcher shows
   it and the in-game page marks "restart" (`docs/research/live-settings.md:48-64`).
3. **Launcher:** regenerate `launcher/src/Launcher.Core/SettingsSchema.json`
   with `scripts/export-settings-schema.ps1`. The 60 fps row is at
   `SettingsSchema.json:16`. The launcher shows only `applies_at_startup` rows
   and writes them to `userdata/wweallstars.settings.toml`. The game reads that
   file once at boot. The command line carries only paths (`live-settings.md:52-62`,
   `LaunchArguments.cs:13-23`). The launcher tests count schema rows
   (`launcher/tests/Launcher.Tests/SettingsSchemaTests.cs`, changed in
   `fa6f571`).
4. **Hooks** in the manifest, then `pwsh ./scripts/build.ps1 -Action Codegen`,
   then Build.
5. **Scene runs:** add `"wwe_unlock_everything": "false"` to the force-off list
   (§3.5).

## 6. Recommended approach

Use 4.1: hooks A and B behind `wwe_unlock_everything`, a startup setting, off
by default. It reuses the game's own `UnlockAll` flag, so every item type the
cheat covers opens through the game's own code. It never writes the save. It
avoids the one persistent side effect of the real cheat, the FW counter.
Turning it off and restarting gives back exactly the earned state.

First check, before writing code (5 minutes): in the current build, enter
`UnlockAll` on Extras → CHEATS. If "YOU HAVE UNLOCKED: Everything!" appears and
the rosters open, the flag path works as decoded. Use a throwaway profile,
because the cheat raises the FW counter.

## 7. Open questions

1. Does the owner want progress recorded while the setting is on (§4.3), or is
   "unlock everything = a sandbox session" fine?
2. Which tab for the row: GAMEPLAY, or a new EXTRAS/CHEATS tab?
3. Does any screen read unlock state some other way than the item bits or the
   FW getter? Example: the Path of Champions opponent list (`GetNextStoryOpponent`),
   or story badges (`AwardAlmostAllStoryBadges` hints at a separate badge
   state). This needs an owner run through each mode with the setting on.
4. When does `0x82B96898` run? A refresh must follow a sign-in change or "no
   save" start (`0x82BCDCF0` does not refresh). Otherwise the bits stay
   cleared until the next refresh.
5. Online: does a peer with everything unlocked work against a stock peer
   (§3.3)?
6. Is the FW counter really persisted (§2.5)? It matters only if the owner ever
   uses the real cheat.

## Sources

- [gr360] GameRevolution, WWE All Stars Xbox 360 cheats: https://www.gamerevolution.com/?p=50923
- [ch360] Cheat Happens, WWE All Stars Xbox 360 cheats: https://www.cheathappens.com/17062-XBox_360-WWE_All_Stars_cheats
- [supercheats-pc] Supercheats, Xbox 360 (could not be fetched; 403): https://www.supercheats.com/xbox360/wweallstarscheats.htm
- [cheatcc] CheatCC: https://www.cheatcc.com/?p=92391
- [sdh] The SmackDown Hotel, achievements list: https://www.thesmackdownhotel.com/allstars/guides/all-stars-ps3-trophies-xbox-360-achievements-full-list
- [xenia-patch] Xenia Canary game-patches, `54510866 - WWE All Stars.patch.toml`: https://github.com/xenia-canary/game-patches/blob/main/patches/54510866%20-%20WWE%20All%20Stars.patch.toml
