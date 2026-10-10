# OPTIONS menu (GFx) and a PC SETTINGS row

Ticket: `.scratch/in-game-settings/issues/06-research-options-menu-row.md`.
Date 2026-10-03. Addresses are base XEX v5. Disassembly is from
`python scripts/disasm.py` on `analysis/xex/default.pe`. Movie offsets are
from a throwaway SWF tag parser and AS2 disassembler in the session
scratchpad (not committed). No game data is in this repo.

## Answer

- The rows are hard-coded in the movie's ActionScript. `fe_options` builds
  `menuContents = new Array("Sound", "GamePlay", "Controls")` in its frame-1
  `DoAction`, and the menu creates one row per array entry. Nothing native
  fills the list.
- Each label and the description line come from one native call per string:
  AS `Localize(key, "Options")` → `ExternalInterface.call("Localize",
  "Options", key, "Screens")` → UE3 `Localize` at `0x826005F8`. The
  description uses `key + "_Desc"` and is looked up again on every Up/Down.
- A selection never reaches native code as a row id. A runs
  `playAudio("SFX_UI_Menu_Select")`, which sends
  `FSCommand:Call_Kismet Event=SFX_UI_Menu_Select`, and then AS `Select()`
  compares the key with `"Sound"`, `"GamePlay"` and `"Controls"` and calls
  `LoadSubMenu("../UI_SHELL/options_*.swf")`. A key that matches none of
  them does nothing.
- B is handled in AS (`LoadSubMenu("../UI_SHELL/fe_main.swf")`) and does not
  depend on the rows.
- **Verdict: a safe hook exists.** It needs three guest hooks and no stubs:
  1. Patch the `fe_options` action bytes when GFx reads them
     (`0x83131340`). The patch adds a 4th key, `"Options"`, and keeps the
     length the same.
  2. Answer `Localize("Options", "Options"/"Options_Desc", "Screens")` in the
     UE3 `Localize` hook (`0x826005F8`) with "PC SETTINGS" and a description.
  3. In the FSCommand callback (`0x825CB1D0`), open the page on
     `Call_Kismet Event=SFX_UI_Menu_Select` while the selected OPTIONS row is
     ours. The selected row is known from the last `*_Desc` lookup.

## How the movie is stored

- `assets/Game/CookedXenon/UI_SHELL.xxx` is a UE3 package (`9E2A83C1`,
  licensee 0x25, version 0x18E). It is not compressed. The movies sit in it
  as plain `GFX\x08` streams (GFx 2.x, no `CFX`/zlib), held by
  `SD_UIRawData` objects (class string `USD_UIRawData` at `0x82007CC0`).
- `fe_options` starts at file offset `0x30024` and is `0x1DCC` bytes long.
  Its ExporterInfo name is `fe_options`. Only one copy exists in `assets/`.
- Shared parts come from `fe_templates.xxx`: `VerticalMenu` at `0x50D3B`
  (class `SD_UI.Core.components.Menus.DynamicContentMenu`) and
  `DynamicMenuItem` at `0x1A102`. The root movie that defines `playAudio`
  and `LoadSubMenu` is `interface_Loader` in `UI_LOADER.xxx` (`0x569`).

## fe_options ActionScript (frame 1 DoAction, 4587 bytes)

The movie offsets below count from the start of the movie. The action buffer
starts at movie offset `0x3C`.

| Movie off | What it does |
|---|---|
| `0x05EF` | `function Localize(StringID, Section, Package="Screens")`: `flash.external.ExternalInterface.call("Localize", Section, StringID, Package)`. If the call returns null, it shows `<StringID@Section@Package>`. |
| `0x0A08` / `0x0A7D` | `Up()` / `Down()`: `curFocus.menu.menuLogic.Up()`/`Down()`, then `descTxt.text = Localize(menuContents[getCurrentSelection()] + "_Desc", "Options")` |
| `0x0AF4`–`0x0B4D` | `GoToSoundOptions` / `GoToGameplayOptions` / `GoToControlsList` → `LoadSubMenu("../UI_SHELL/options_sound.swf" / "options_gameplay.swf" / "controls_main.swf")` |
| `0x0B78` | `Select(i)`: `strictEquals` on `menuContents[i]` against `"Sound"`, `"GamePlay"`, `"Controls"`. The default branch (`0x0BC8`) jumps to the end of the function. |
| `0x0C31` | `menuLocalizationGroup = "Options"` |
| `0x0D3F` | `menuContents = new Array("Sound","GamePlay","Controls")` (push of 3 strings, count 3, `"Array"`, `newObject`, `setVariable`) |
| `0x0D7A`–`0x0D8B` | three `push`+`trace` pairs (dead in release; 18 bytes) |
| `0x0EC4` | menu init callback: `menuLogic.InitializeRows(menuContents.length)`, then `SetContent("../fe_templates/DynamicMenuItem.swf", i)` for each entry. `pxGutter=-65`, `nScrollTolleranceDown=5`. |
| `0x0D91` | "finished" callback: header `Localize("HeaderName")`, the description for the current row, then per row `item<i>.Initialize(Localize(key))` and `item<i>.SetIcon(key)` |
| `0x0FF9` | `onKeyDown`: Up/Down → `Up()`/`Down()`. B → `playAudio("SFX_UI_Menu_Cancel")`, `LoadSubMenu(fe_main.swf)`. A → `playAudio("SFX_UI_Menu_Select")`, `Select(getCurrentSelection())`. |

- The labels are in `Localization/Coalesced.int` (`Screens` package): the
  `[Options]` section has `HeaderName`, `Sound`, `Sound_Desc` ("Customize
  your audio levels."), `GamePlay`, `GamePlay_Desc`, `Controls` and
  `Controls_Desc`. It has no `Options` key.
- Rows are not limited to 3. `DynamicContentMenu.InitializeRows(n)` calls
  `AddRow` n−1 times, and `AddRow` does `item0.duplicateMovieClip("item"+k)`
  (VerticalMenu `0x1A1A`, `0x1B1B`). `fe_main` uses the same template, the
  same gutter (−65), the same scroll tolerances (5) and the same divider
  band (y 101–456). It shows 7 rows. So a 4th row stays inside the
  no-scroll range.
- `DynamicMenuItem.SetIcon(name)` calls `Icon.gotoAndStop(name)`. The Icon
  sprite has an `Options` frame label (the main-menu OPTIONS icon). An
  unknown label leaves the Icon on its first frame.

## Native side

| Address | Role | Evidence |
|---|---|---|
| `0x825C5328` | `GFxExternalInterface` callback (`this, movieView, const char* method, const GFxValue* args, n`) | Compares the method against the wide names at `0x8208D91C`–`0x8208DAF8` (`GetRegion` … `Localize`). The `Localize` branch (`0x825C535C`) needs 3 string args (GFxValue stride 0x10, type 4 at +0, ptr at +8). It calls `0x826005F8(out, args[0], args[1], args[2], 0, 0)`, converts the result to UTF-8, and returns it through `movieView->vtbl[0x9C]` (SetExternalInterfaceRetVal). |
| `0x826005F8` | UE3 `Localize(Section, Key, Package, LangExt, bOptional)` returning `FString` by value (r3 = return slot) | Caller at `0x825C5440`. After the call, the temp at `r31+0x50` is assigned (`0x824367F8`) and destroyed (`0x82459AF8`). |
| `0x824137B0` | `FString(const TCHAR*)` constructor | Computes length+1, allocates through `0x824133F8` (sets Data/Num/Max, GMalloc at `0x83701478`), and copies. |
| `0x825CB1D0` | `GFxFSCommandHandler::Callback(this, movieView, const char* cmd, const char* args)` | Converts `cmd` to FString (`0x82437EB8`) and parses `args` (`0x825CADC8`). It special-cases `UIState` (`0x8208F258`), looks the handler up in the map at `engine+0xB0` (`0x82C133B0`) and calls `handler->vtbl[0](handler, …, parsedArgs)` at `0x825CB308`. |
| `0x825C83F8` | `RegisterFSCommand(engine, name, handler)` | Called at `0x82AF94B8`/`0x82AF94E0`/`0x82AF9514` with `Call_Kismet` (`0x82200B94`) and `Set_Game_Option` (`0x82200B84`). The `Call_Kismet` handler vtable is `0x821FF690` → `0x82AFB560`, which reads `Event=`. |
| `0x83132D00` | GFx `DoActionLoader` (string `tag %d: DoActionLoader` at `0x8230A9FC`) | Creates the action object (`0x8312EBF8`) and calls `0x831329D0(action, stream)`. |
| `0x831329D0` | `GASDoAction::Read` | Allocates a `GASActionBufferData` (0x20 bytes) at `action+4` and calls `0x83131340`. |
| `0x83131340` | `GASActionBufferData::Read(this, stream)` | Copies action records byte by byte into the growable array at `this+0x10` (data ptr) / `this+0x14` (count) until an End (0) action. The same layout is read back at `0x83132A78`. DoInitAction (`0x83132DA0`) uses it too. |

AS `playAudio(e)` (in `interface_Loader`, `UI_LOADER.xxx` movie offset
`0x21A1`) is `getURL("FSCommand:Call_Kismet", "Event=" + e)`. So every A
press in a menu reaches `0x825CB1D0` as `cmd="Call_Kismet"`,
`args="Event=SFX_UI_Menu_Select"`, in the same action run and before
`Select()`.

## The movie patch (length-preserving)

The patch goes in the `fe_options` frame-1 action buffer, at buffer offset
`0xD03` (movie `0xD3F`, UI_SHELL file offset `0x30D63`), and is 77 bytes
long. Constant-pool indices (the pool is unchanged): `menuContents` 0x23,
`Options` 0x35, `Controls` 0x2D, `GamePlay` 0x2C, `Sound` 0x2B, `Array` 0x1A,
`"+++…"` 0x48.

- The original layout is the `push` record (`96 0F 00`, 15 bytes of values,
  18 bytes in all), then `newObject setVariable` (`40 1D`), then 39 unchanged
  bytes (pxGutter, desc_suffix, `playAudio("SFX_UI_Transition_10")`), then
  the 18 bytes of trace pairs.
- The new layout:
  - `96 11 00 08 23 08 35 08 2D 08 2C 08 2B 07 04 00 00 00 08 1A` pushes
    "menuContents", "Options", "Controls", "GamePlay", "Sound", 4, "Array".
    The 4th element is pushed first, so the array becomes
    `[Sound, GamePlay, Controls, Options]` and indices 0–2 do not change.
  - `40 1D`.
  - The 39 original bytes, copied from the buffer itself.
  - Filler: (`96 02 00 08 48 4C 26 26`) × 2, which is push "+++", dup, trace,
    trace, written twice. It leaves the stack balanced.
- Checks done on the patched buffer: the length is the same (77 → 77), the
  re-disassembly is clean, and every branch target in the 4587-byte buffer
  still falls on an instruction start. No branch targets the region, and no
  function body spans it. The function sizes and the DoAction tag length are
  unchanged.
- The key `"Options"` was chosen because it is already in the pool, so it
  costs 2 bytes, and because `SetIcon("Options")` shows the OPTIONS gear
  icon. `Select()` matches no branch for it, so A on it does nothing in AS. A
  new inline key such as `"PCSettings"` (12 bytes) also fits: drop the second
  trace pair, keep one `push`/`trace` pair (6 bytes) as filler. That row
  would have no icon.

## Candidate approaches, ranked

1. **Recommended: patch the action buffer at GFx parse, and add the
   Localize and FSCommand hooks.**
   - The patch applies to the copy GFx parses, whatever file opener loaded
     it. It costs one `count == 4587` compare per action buffer, plus a
     77-byte compare on a match.
   - B, Up/Down and rows 0–2 run the original code with the original
     indices.
   - Things that could still break:
     - A 4th row may not fit the panel. The evidence says it fits, but a
       screenshot must confirm it.
     - If `Localize` is not hooked, the label shows UE3's
       missing-localization marker. Ship the two hooks together.
     - GFx caches the parsed MovieDef, so the patch must be in place before
       the first OPTIONS visit. A hook at parse time always is.
2. **Rejected: native list fill.** There is none. The array is an AS
   literal. The only other way would be GFx `SetVariable`/`Invoke` on a path
   under the loader clip, run between the frame script and the async init
   callback. That timing is a race, and the path is unknown.
3. **Rejected on its own: FSCommand intercept only.** It catches A, but
   it cannot add a row. It is used inside approach 1.
4. **Rejected: patch the UI_SHELL file bytes on read.** It would work,
   since the package is uncompressed and the region is at file offset
   `0x30D63`, but it needs an SDK file-system patch. The parse-time guest
   hook is local to the port.

## Plan for ticket 07

All three are `REX_HOOK_RAW` hooks, in the style of `src/game_tick_log.cpp`.
All three functions already exist in `generated/default`'s
`PPCFuncMappings`, so no manifest seed is needed.

1. **`sub_83131340` (GASActionBufferData::Read).** Call the original first.
   Then, with `p = this`:
   - If `be32(p+0x14) == 4587` and the 77 bytes at `be32(p+0x10)+0xD03`
     equal the original layout, rewrite them in place as shown above.
   - Check the expected record bytes (`96 0F 00 08 23 08 2D 08 2C 08 2B 07 03
     00 00 00 08 1A 40 1D`, then the three `96 02 00 08 4x 26` pairs at the
     end) before writing.
   - Log once.
2. **`sub_826005F8` (Localize).**
   - Read r4/r5/r6 as big-endian UTF-16 (Section/Key/Package).
   - When Section=`Options`, Package=`Screens` and Key is `Options` or
     `Options_Desc`, do not call the original. Construct the return
     FString with `sub_824137B0(r3, text)`, where `text` is our UTF-16BE
     string in guest memory (allocate it once). Set `r3` to the slot and
     return.
   - For every call whose Key ends in `_Desc`, record
     `options_row_selected = (Section == "Options" && Key == "Options_Desc")`.
3. **`sub_825CB1D0` (FSCommand callback).**
   - Read `r5`/`r6` as C strings. If cmd is `Call_Kismet`, args is
     `Event=SFX_UI_Menu_Select` and `options_row_selected` is set, post an
     "open page" request (an atomic flag). The call can come from a guest
     UI thread (`SD_UIThread`), not the ImGui thread.
   - Always call the original, so the select sound plays.
   - The page model already waits for the A release and holds input until B
     is released, so the game does not act on either press.
4. Checks for the session:
   - a screenshot of OPTIONS with 4 rows;
   - the description line on each row;
   - A on PC SETTINGS opens the page and the game stays on OPTIONS;
   - Sound, Gameplay and Controls each open their screen;
   - B returns to the main menu;
   - after returning from a submenu, OPTIONS still shows 4 rows.

Open points, to be checked at runtime in 07:

- whether the 4th row fits inside the lower divider (y 456);
- the exact text of UE3's missing-key marker (it does not matter once the
  hook is in);
- whether any other screen looks up `Options`/`Options_Desc` in section
  `Options` (none found statically).
