# Base roster and the Exhibition select-screen route

Ticket: `.scratch/playable-build/issues/09-base-roster-route.md`. Researched 2026-10-03.

## Answer

- The base roster has **30 wrestlers**: 15 Legends and 15 Superstars. Together they have **65 attires**.
- The select grid has 16 columns and 2 rows (32 slots). Slots (col 7, row 0) and (col 8, row 0) are the Created Superstar slot and the Downloaded slot.
- 20 wrestlers are open on a fresh profile. 10 are locked, and every alternate attire is locked.
- One pad code at the main menu unlocks everything for the session. The code is Left, Y, Down, Left, Y, X, Left, X, Y, Down, Right, X, Left, Up, X, Right.
- On the select screen, the D-pad moves the cursor, X cycles to the next unlocked attire, A locks the pick in, and Y toggles the class icon.
- Arena for the sweep: `Arena_001` ("SMACKDOWN", the modern set). It is first in the arena list and open without unlocks.

## Sources (all primary, read-only)

| Fact | Source |
|---|---|
| Names, attire packages, legend flag | `GameCharacterData` objects `CharacterData_1xx/2xx` in `CookedXenon/CombinedGlobalCharacters.xxx` (`CharacterName`, `CodeName`, `CharacterCode`, `bLS_Legend`, `CostumesArray[].Model`) |
| Grid order | `LVSCHR_*` enum: UnrealScript enum in `Game.xxx`, and the identical native string table at `0x821EF7D8` in `default.pe` |
| Slot → enum index, cursor movement, buttons | AS2 bytecode of the `fe_characterSelect` GFx movie inside `UI_SHELL.xxx` (`GetCharacterInSlot`, `GetThumbnailTarget`, the move function, `onKeyDown`, `maxCols=16`, `maxRows=2`) |
| Locked wrestlers, locked alternate attires and arenas | `Localization/Coalesced.int` → `Unlockables.int` (the `EIC_*` list); `CoachingTips.int` ("alternate ring gear … unlocked by completing Path of Champions") |
| Cheat codes | `Config/Xenon/Cooked/Coalesced.ini` → `Xe-Game.ini` `[Game.GameCheatCodeManager]`. The main-menu GFx movie `fe_main` forwards the D-pad and X/Y as `FSCommand:UI_Input key=FaceUp/FaceDown/FaceLeft/FaceRight/ButtonX/ButtonY` |
| Menu orders | `Xe-Game.ini` Runbot tables (`RunbotGameMode`, `RunbotExhibitionType`, `RunbotExhibitionStyle`), `ArenaUITable` |
| Visual check | `analysis/codex-select-fixed5.png`. This is a runtime capture of the 1v1 select screen. The portraits match the enum order slot for slot, and all 10 `?` slots are exactly the 10 locked wrestlers |

How we read it: the packages are UE3 (licensee 0x25, version 0x18E), big-endian, with LZO1X chunks. `Game.xxx` and `CombinedGlobalCharacters.xxx` are whole-file compressed with a little-endian chunk header. A throwaway LZO + name/export/property parser and an AS2 disassembler did the reading. They live in the session scratchpad and are not committed. No game data is in this repo.

## Grid

`col` and `row` are 0-based, with (0,0) at the top left. `idx` is the `LVSCHR_*` enum value. Slot rule (from `GetCharacterInSlot`): in row 0, idx = col for col < 7 and col − 2 for col > 8. In row 1, idx = 14 + col. Legends fill the left (gold) side and Superstars the right (blue).

| row \ col | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Snuka* | Perfect* | Austin | Steamboat | Savage | Bret | **Hogan (P1)** | CAP | DLC | **Cena (P2)** | Punk | Rey | Kofi | Undertaker | Edge* | Kane* |
| 1 | HBK* | Slaughter* | Eddie* | Piper | Roberts | Warrior | Andre | Rock | HHH | Big Show | Sheamus | Morrison | Orton | Miz* | Swagger* | McIntyre* |

`*` = locked on a fresh profile. `CAP` shows a darkened silhouette when the profile has no Created Superstars. `DLC` shows the WWE logo. The P1 (blue) cursor starts on Hogan and the P2 (red) cursor on Cena. This is seen in one capture. Whether the cursor keeps the last pick across matches is unverified.

## Cursor rules (fe_characterSelect AS2)

- Up, Down, Left and Right call `Move(dx, dy)`. Columns and rows both wrap.
- Move keeps stepping in the same direction past these slots: locked or unselectable wrestlers, CAP/DLC slots with no entries, and wrestlers another player has already locked in. If it gets back to the start, the cursor stays put. When the Runbot test harness is active, it does not skip.
- Every move resets `curCostume` to 0, which is RING GEAR 1.
- `X` calls `GetNextUnlockedCostume`, a native call through ExternalInterface. When no alternate is unlocked it plays the cancel sound and does nothing.
- `A` locks the pick in. A then fails on locked, unselectable or already-chosen wrestlers.
- After P1 locks in, the same pad takes over the next unassigned CPU cursor (`PostLockProcessing` → `PushAssignment`). When every player is locked, the screen moves on to arena select by itself after 1.5 s (`setTimeout(Proceed, 1500)`).
- `B` cancels the last selection or backs out. `Y` toggles the class icon. `Back` opens help.

## Unlocks

- Locked by default (`Unlockables.int`): McIntyre, Guerrero, Edge, Swagger, Kane, Mr. Perfect, Sgt. Slaughter, Shawn Michaels, Snuka, The Miz. Every alternate attire is locked too (`EIC_*_AA1`, `_AA2`), and so are three arenas: `Arena_003` All Stars, `Arena_005` classic SmackDown and `Arena_006` SummerSlam.
- `UnlockAll` cheat (`Xe-Game.ini`): FaceLeft, ButtonY, FaceDown, FaceLeft, ButtonY, ButtonX, FaceLeft, ButtonX, ButtonY, FaceDown, FaceRight, ButtonX, FaceLeft, FaceUp, ButtonX, FaceRight. On the pad that is Left, Y, Down, Left, Y, X, Left, X, Y, Down, Right, X, Left, Up, X, Right. Enter it on the **main menu**: `fe_main` forwards these keys to the cheat manager. A secondary source (GameFAQs-style cheat lists) gives the same sequence. That source is unverified, but the ini is primary.
  - The two Downs and one Up also move the main-menu cursor, so it ends one item below where it started. Press Up once to get back to EXHIBITION.
  - An "YOU HAVE UNLOCKED: Everything!" popup (`Unlockables.int` `All`, `UnlockablePopup`) probably appears. If it does, the scene must dismiss it with A before pressing A again on the menu. This is unverified, so capture a frame and check.
- Other switches (not needed): `[Game.GameFrontEndManager] UnlockAll=false`, `[Game.GameUnlockablesManager] UnlockAllItems=false` and `RunbotUnlockAllItems=true` (applies under Runbot only). Changing them would mean overriding the on-disc ini at runtime. Whether the game honours such an override is unverified.
- Storage: `GameUnlockablesManager` keeps the unlock state, and it is saved to the profile's `.SAV` files. `default.pe` has the strings `GameTrackedStats.SAV`, `STORY.SAV` and `%s.SAV`. Which file holds the unlocks is unverified. Entering the cheat on every boot avoids any dependency on a save device.

## Route to the select screen (1 vs 1, one pad)

1. Title (`fe_start`): press Start. The press-start screen is not part of this ticket.
2. Main menu: enter the cheat, press Up once, then press A on EXHIBITION. EXHIBITION is item 0 in the `RunbotGameMode` order and is highlighted in the `codex-match-options.png` capture.
3. `fe_exhibition`: A on STANDARD (item 0).
4. `ex_matchStyle`: A on 1 vs. 1 (item 0 per `RunbotExhibitionStyle`). Both menus remember their last position within a session (`fe_exhibitionLastPosition`, `ex_matchStyleLastPosition`), so repeating the route lands on the same items.
5. `fe_controllerSelect`: Left moves the pad into the team-1 (blue) socket. `AssignControllerToSocket` then calls `Accept`, so the pad is locked in. Press A: `CheckReadyToProceed` passes because the locked-in count equals the physical pad count. If the "ARE YOU SURE?" popup appears, press A again.
6. Select screen: P1 pick (route below), then P2 pick, then automatic move to arena select after 1.5 s.
7. `ex_arenaSelect` is a horizontal list in `ArenaUITable` order: `Arena_001` SMACKDOWN, `Arena_004` RAW, `Arena_002` WRESTLEMANIA, `Arena_003` ALL STARS ARENA, `Arena_006` SUMMERSLAM, `Arena_005` SMACKDOWN! (classic). Left/Right move along it and A picks. Assume the cursor starts on item 0, and check that the banner reads "SMACKDOWN" (not "SMACKDOWN!"). Both points are unverified.

## Route on the select screen (after UnlockAll)

Start from P1 on Hogan (6,0). Press Down to reach (6,1), then Left or Right |col − 6| times along row 1, then Up if the target is in row 0. Then press X once per attire index, then A.

Moving along row 1 then going Up avoids the CAP/DLC gap in row 0. Row 1 has no gaps, and a row-0 target is never in column 7 or 8, so the route works whether or not the profile has a CAP.

P2 (opponent): press A to take Cena, ring gear 1. When P1 is Cena, press Right then A to take CM Punk instead, because Cena is already chosen and Move skips him.

Attire index k is reached with X×k. That assumes X steps 0→1→2 in order once all attires are unlocked. The ordering inside `GetNextUnlockedCostume` is native and unverified.

## Scene table

`attires` lists the `CostumesArray` order: index 0 is RING GEAR 1, reached with no X press. Package = `CookedXenon/<name>.xxx`. `_C` packages are the cinematic models (`CineModel`). `route` is the P1 input from Hogan, before the X presses and the A. `lock` = locked on a fresh profile.

| idx | wrestler | codename | CHR | side | lock | col | row | route | attires (k: package) |
|---|---|---|---|---|---|---|---|---|---|
| 0 | Jimmy Snuka | JimmySnuka | 118 | Legend | yes | 0 | 0 | D L6 U | 0:CHR_118_A 1:CHR_118_B |
| 1 | Mr. Perfect | MrPerfect | 113 | Legend | yes | 1 | 0 | D L5 U | 0:CHR_113_A 1:CHR_113_B |
| 2 | Steve Austin | SteveAustin | 106 | Legend | no | 2 | 0 | D L4 U | 0:CHR_106_A 1:CHR_106_B |
| 3 | Ricky Steamboat | RickySteamboat | 112 | Legend | no | 3 | 0 | D L3 U | 0:CHR_112_A 1:CHR_112_B |
| 4 | Randy Savage | RandySavage | 105 | Legend | no | 4 | 0 | D L2 U | 0:CHR_105_A 1:CHR_105_B 2:CHR_105_C |
| 5 | Bret Hart | BrettHart | 107 | Legend | no | 5 | 0 | D L1 U | 0:CHR_107_A 1:CHR_107_B |
| 6 | Hulk Hogan | HulkHogan | 101 | Legend | no | 6 | 0 | (none) | 0:CHR_101_A 1:CHR_101_B 2:CHR_101_C |
| 7 | John Cena | JohnCena | 202 | Superstar | no | 9 | 0 | D R3 U | 0:CHR_202_A 1:CHR_202_B 2:CHR_202_C |
| 8 | CM Punk | CmPunk | 208 | Superstar | no | 10 | 0 | D R4 U | 0:CHR_208_A 1:CHR_208_B |
| 9 | Rey Mysterio | ReyMysterio | 203 | Superstar | no | 11 | 0 | D R5 U | 0:CHR_203_A 1:CHR_203_B 2:CHR_203_C |
| 10 | Kofi Kingston | KofiKingston | 211 | Superstar | no | 12 | 0 | D R6 U | 0:CHR_211_A 1:CHR_211_B |
| 11 | Undertaker | TheUndertaker | 204 | Superstar | no | 13 | 0 | D R7 U | 0:CHR_204_A 1:CHR_204_B |
| 12 | Edge | Edge | 207 | Superstar | yes | 14 | 0 | D R8 U | 0:CHR_207_A 1:CHR_207_B |
| 13 | Kane | Kane | 216 | Superstar | yes | 15 | 0 | D R9 U | 0:CHR_216_A 1:CHR_216_B |
| 14 | Shawn Michaels | ShawnMichaels | 206 | Legend | yes | 0 | 1 | D L6 | 0:CHR_206_A 1:CHR_206_B |
| 15 | Sgt. Slaughter | SgtSlaughter | 119 | Legend | yes | 1 | 1 | D L5 | 0:CHR_119_A 1:CHR_119_B |
| 16 | Eddie Guerrero | EddieGuerrero | 114 | Legend | yes | 2 | 1 | D L4 | 0:CHR_114_A 1:CHR_114_B |
| 17 | Roddy Piper | RoddyPiper | 108 | Legend | no | 3 | 1 | D L3 | 0:CHR_108_A 1:CHR_108_B |
| 18 | Jake Roberts | JakeRoberts | 115 | Legend | no | 4 | 1 | D L2 | 0:CHR_115_A 1:CHR_115_B |
| 19 | Ultimate Warrior | UltimateWarrior | 104 | Legend | no | 5 | 1 | D L1 | 0:CHR_104_A 1:CHR_104_B |
| 20 | Andre the Giant | AndreTheGiant | 102 | Legend | no | 6 | 1 | D | 0:CHR_102_A 1:CHR_102_B 2:CHR_102_C |
| 21 | The Rock | TheRock | 103 | Legend | no | 7 | 1 | D R1 | 0:CHR_103_A 1:CHR_103_B |
| 22 | Triple H | TripleH | 201 | Superstar | no | 8 | 1 | D R2 | 0:CHR_201_A 1:CHR_201_B |
| 23 | Big Show | BigShow | 205 | Superstar | no | 9 | 1 | D R3 | 0:CHR_205_A 1:CHR_205_B |
| 24 | Sheamus | Sheamus | 220 | Superstar | no | 10 | 1 | D R4 | 0:CHR_220_A 1:CHR_220_B |
| 25 | John Morrison | JohnMorrison | 214 | Superstar | no | 11 | 1 | D R5 | 0:CHR_214_A 1:CHR_214_B |
| 26 | Randy Orton | RandyOrton | 213 | Superstar | no | 12 | 1 | D R6 | 0:CHR_213_A 1:CHR_213_B |
| 27 | The Miz | TheMiz | 218 | Superstar | yes | 13 | 1 | D R7 | 0:CHR_218_A 1:CHR_218_B |
| 28 | Jack Swagger | JackSwagger | 224 | Superstar | yes | 14 | 1 | D R8 | 0:CHR_224_A 1:CHR_224_B |
| 29 | Drew McIntyre | DrewMcintyre | 222 | Superstar | yes | 15 | 1 | D R9 | 0:CHR_222_A 1:CHR_222_B |

Totals: 30 wrestlers. 5 have three attires (Hogan, Andre, Savage, Cena, Rey) and 25 have two, for 65 attires. A full sweep at one wrestler per match is 65 matches. Picking P2 from the table as well halves that (keep P1 ≠ P2).

Notes:
- Shawn Michaels is CHR 206 but `bLS_Legend=1`, so he sits on the Legends side.
- `Unlockables.int` also lists `_AA2` entries for Triple H, Undertaker and Orton, and `_AA3`/`3AA` for Cena and Triple H. No costume on disc backs these: `CostumesArray` has 2, 2, 2, 3 and 2 entries. They are probably DLC or cut and are out of scope.

## On disc but not base roster (excluded)

These have a `CharacterData` entry and one attire package on disc, but they are not in the `LVSCHR_*` enum and have no `HUDPortraits` package. They match the 2011 DLC packs in `docs/research/title-update.md`:

- CHR110 Million Dollar Man (`CHR_110_A`)
- CHR212 Ted DiBiase Jr (`CHR_212_A`)
- CHR117 Dusty Rhodes (`CHR_117_A`)
- CHR219 Cody Rhodes (`CHR_219_A`)

CHR109/111/116/120–130/209/210/215/217/221/223/225–230 are data stubs with no package on disc. Among them: Iron Sheik, Ric Flair, Honky Tonk Man, Hawk, Animal, Lawler, Hayes, Batista, Jericho, Mark Henry, Evan Bourne, Ziggler, R-Truth and Matt Hardy.

## Arena recommendation

`Arena_001` (SMACKDOWN, modern set: references `Env_SDModern_Entrance`; character lights in `Arena_001_Chr_Light`).

- Index 0 in `ArenaUITable`, so it needs no input beyond A (assuming the cursor starts on item 0).
- Open without the cheat.
- It is the first arena in the developers' own Runbot arena table.

Lighting quality and occlusion were not compared: no Classic capture of any arena exists yet. If the first Classic match frames show `Arena_001` dark or heavily occluded, the next candidates in list order are `Arena_004` RAW (Right×1) and `Arena_002` WrestleMania (Right×2). Both are also open by default.

## Open (verify on Classic)

- Where the cursors start on later visits to the select screen (whether they reset to Hogan/Cena or keep the last pick).
- Whether the unlock popup appears after the cheat, and which button closes it.
- X attire order after UnlockAll, and whether it wraps back to 0.
- Where the arena-select cursor starts.
- Whether the controller-select confirmation popup appears with one pad.
