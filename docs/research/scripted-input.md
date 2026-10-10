# Scripted input: porting wwe-svr2011-pc's command-file pad

Date: 2026-10-03. Ticket: `.scratch/playable-build/issues/02-port-scripted-input.md`.
Sources:
- wwe-svr2011-pc at `de85161` (v2.0.0), paths prefixed `SVR:`. The author granted
  reuse (`svr2011-pc-port.md` §1).
- ReXGlue source at `.tools/rexglue-source` (`f5337cdc` plus our patches), paths
  prefixed `SDK:`.
- Our generated code at `generated/default` (strict codegen of base v5), paths
  prefixed `GEN:`, and the decrypted image via `scripts/disasm.py`.

## Summary

- SVR2011's scripted input is one `InputDriver` subclass, `ScriptInputDriver`. It
  tails a text file and plays its commands as a virtual pad. It is installed
  through `RuntimeConfig::input_factory`, not by patching `xam_input.cpp`.
- **Timing is wall clock, not frame numbers.** Each step lasts N ms of
  `steady_clock`, and starts at the first guest poll after the previous step
  ends. Frame-number timing has to be added for scenes.
- **All Stars reads both calls.** It imports `XamInputGetState` and
  `XamInputGetKeystrokeEx`. The keystroke loop drops every virtual key above
  0xFF, so pad keystrokes (`VK_PAD_*`) never matter. Gameplay and menu input go
  through `XInputGetState` only.
- **Smallest port:** about 150 lines of `script_input.{h,cpp}` without
  `type/key/touch/drag`, an `OnPreSetup` override in `WweAllStarsApp`, and a
  `session.ps1`-style script. No SDK patch is needed.

## 1. How wwe-svr2011-pc does it

### Command grammar (`SVR: port/src/script_input.h` L7–19, `script_input.cpp` L71–146)

One command per line. The verb is case-insensitive; lines starting with `#` are
ignored (L74). Unknown verbs and buttons are logged and skipped (L47–49, L141–143).

| Command | Effect | Default ms | Source |
|---|---|---|---|
| `press <btns> [ms]` / `hold <btns> <ms>` | hold the buttons, then queue an automatic 120 ms release step so a repeated press registers | 120 | L79–86 |
| `stick <L\|R> <x> <y> <ms> [btns\|-] [rt]` | hold a stick (clamped to ±32767), with optional buttons and right trigger | — | L87–99 |
| `trigger <L\|R> <0-255> <ms>` | hold a trigger | — | L100–106 |
| `wait <ms>` | neutral pad | — | L107–109 |
| `type <text>` | rest of the line typed into the game's keyboard (needs `keyboard_typing`) | 100 | L110–113 |
| `key <BACK\|ENTER\|ESC\|LEFT\|UP\|RIGHT\|DOWN>` | one PC key into the game's keyboard | 100 | L123–140 |
| `touch <x> <y> [ms]`, `drag <x0> <y0> <x1> <y1> [ms]` | on-screen touch controller (Android) | 150 | L114–122 |

`<btns>` is `+`-joined: `A B X Y START BACK LB RB LS RS UP DOWN LEFT RIGHT`
(L22–52). A step holds one full pad state (buttons, triggers, both sticks); a new
step replaces it rather than ORing into it (`Step`, `.h` L53–62).

### Execution model

- **Tailing the file.** `PollFile` runs at most every 100 ms (`.cpp` L20,
  L148–153). It reads only the bytes after `consumed_` and parses whole lines; a
  partly written last line waits (L166–174). A shrunk file resets to the start
  (L160–162). Tests append with `Add-Content` while the game runs
  (`SVR: port/tools/session.ps1` L64–67).
- **Clock.** `CurrentStep` runs inside every `GetDeviceState` call. When the step
  has expired, it pops the next one and sets `step_end_ = now + ms`
  (`.cpp` L185–209). With an empty queue the pad is neutral (L205–206). So a
  step's start depends on when the game polls, and its length is wall time.
  Frame numbers never appear.
- **Driver surface.** One device, id `'SCRT'` (L19, L64–69). `GetDeviceState` fills
  `X_INPUT_STATE` and increments `packet_number` on every call (L212–229).
  Capabilities report a full gamepad (L231–249). Vibration succeeds (L251–253).
  `GetDeviceKeystroke` always returns `X_ERROR_EMPTY` (L255–258), so the script
  never produces keystrokes; `type`/`key` go through a separate guest hook.
- **Install.** `OnPreSetup` reads `SVR2011_INPUT_FILE`. If it is set, it replaces the
  whole input system with a bare `InputSystem` containing the script driver and
  the touch driver plus their own `DeviceAssignment` (`SVR: port/src/svr2011_app.cpp`
  L206–215). XInput, SDL and MnK are not added, so a real pad or keyboard
  cannot disturb a test run.
- **`type`/`key`** (`SVR: port/src/keyboard_typing.cpp` L154–172, L176–223)
  queue strokes for a `REX_HOOK_RAW` on the game's own `XInputGetKeystroke`
  wrapper `sub_82905130` and `XInputGetCapabilities` wrapper `sub_82905050`.
  These hooks answer only `XINPUT_FLAG_KEYBOARD` (0x02) queries, which SVR2011's
  on-screen keyboard makes (`keyboard_typing.h` L4–10). They are
  SVR2011-specific addresses.

### Unfocused, off-screen window and capture (`SVR: port/tools/session.ps1`)

- `start`: truncate `runs\input.txt`, set `SVR2011_INPUT_FILE`, and set
  `SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0` / `SDL_WINDOW_ACTIVATE_WHEN_RAISED=0` so the
  SDL window never takes focus (L46–49). Launch with `--log_file --audio_mute=true
  --fullscreen=false` and `-WindowStyle Minimized` (L53–55). Then
  `SetWindowPos(-4000, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)` and
  `ShowWindow(SW_SHOWNOACTIVATE)` (L56–60).
- `input "<cmd>" ...`: append the lines to the file (L64–67).
- `shot <stem>`: `PrintWindow(h, dc, PW_RENDERFULLCONTENT=2)` into a bitmap,
  scaled to at most 1280 px wide, saved as PNG (L69–87). `PrintWindow` renders
  the window itself, so off-screen windows capture fine (`run_test.ps1` L4, L63).
- SVR2011 also sets `SVR2011_NATIVE_WINDOW_POS` for a second native-renderer
  window (L50, L74). Our Native presents through the SDK window via
  `external_frame` (`src/native/native_renderer.h` L1–3), so we have no second
  window.

## 2. ReXGlue's input seam

- Guest calls land in `SDK: src/kernel/xam/xam_input.cpp`:
  - `XamInputGetState_entry` (L95–116). It rejects queries whose low flag byte
    lacks `XINPUT_FLAG_GAMEPAD`, pins user 0xFF or ANY_USER to 0, and calls
    `InputSystem::GetState`.
  - `XamInputGetKeystrokeEx_entry` (L161–184) does the same and calls
    `GetKeystroke`.
- `InputSystem::GetState` asks the `DeviceAssignment` which devices feed the user
  and merges every driver's state (`SDK: src/input/input_system.cpp` L215–252).
  `GetKeystroke` returns the first driver that has a stroke (L295–321).
- The app replaces the input system through `RuntimeConfig::input_factory`
  (`SDK: include/rex/runtime.h` L72; called at `src/system/runtime.cpp` L135–136).
  `ReXApp` sets the default (`CreateDefaultInputSystem`) and then calls the
  virtual `OnPreSetup(config_)` (`SDK: src/ui/rex_app.cpp` L309–315,
  `include/rex/rex_app.h` L99). **So `xam_input.cpp` needs no change.** Its L95 is
  the guest-facing entry, not the place to patch.
- Driver interface: `SDK: include/rex/input/input_driver.h` L30–66. The SDK at
  `f5337cdc` matches SVR2011's (`c94f5ebd`, 0 file changes per `svr2011-pc-port.md`
  §Summary), so `ScriptInputDriver` compiles against it unchanged.
- Assignment: a non-synthetic device takes the lowest free ordinal
  (`input_system.cpp` L113–138; `DeviceInfo::synthetic` defaults to false,
  `include/rex/input/device.h` L25–31). The SDK's `SlotAssignment` maps ordinal N
  to user N (`include/rex/input/device_assignment.h` L31–39). With the script as
  the only driver it becomes user 0. SVR2011's `CreateControllerWatch` is not
  needed.
- **Why today's harness needs focus.** `MnkInputDriver::GetDeviceState` returns a
  neutral pad unless `has_focus_ && is_active()`
  (`SDK: src/input/mnk/mnk_input_driver.cpp` L267–273). The keyboard
  script of the time (removed in ticket 11) therefore bounced focus through the taskbar before `keybd_event`. A script driver ignores focus because it never calls `is_active()`,
  and the SDK only wires the active callback to the overlays
  (`rex_app.cpp` L252–260).

## 3. Does All Stars read `XInputGetKeystroke`?

**Yes, but only for keyboard keys. Pad input is `XInputGetState` only.**

- Imports (GEN: `wweallstars_init.cpp` L109029–109032): `XamInputGetCapabilities`
  `0x834FF984`, `XamInputGetState` `0x834FF994`, `XamInputSetState` `0x834FF9A4`,
  `XamInputGetKeystrokeEx` `0x834FF9B4`. There is no plain `XamInputGetKeystroke`.
- XDK wrappers:
  - `sub_8241A358` is `XInputGetState(user, state)`. It forces flags = 1
    (gamepad) and tail-calls the import (GEN: `wweallstars_recomp.119.cpp` L59–68).
  - `sub_8241A430` is `XInputGetKeystroke(user, flags, ks)`. It turns user 0xFF
    into ANY_USER (`oris r4,0x4000`) and calls `XamInputGetKeystrokeEx`
    (GEN: `wweallstars_recomp.123.cpp` L59–92).
- Only one function calls the keystroke wrapper: `sub_82884258`, the per-frame
  input poll, called from `wweallstars_recomp.201.cpp` L6998. For each user
  `r29 = 0..3` (`175.cpp` L9853, L10850–10854) it:
  - calls `XInputGetState` through `sub_82419D20` → `sub_8241A358`
    (`175.cpp` L9935–9939; `220.cpp` L46–50);
  - then drains keystrokes with `flags = 0xFF` (XINPUT_FLAG_ANYDEVICE). The
    calls are at `0x82884804`–`0x8288480C` and `0x828849A0`–`0x828849A8`
    (`175.cpp` L10609–10617, L10834–10842; checked with
    `scripts/disasm.py 82884800`).
- **The VK filter:** `lhz r11,0x58(r31); cmplwi r11,0xFF; bgt next` at
  `0x82884818`–`0x82884820` (`175.cpp` L10623–10626). Every stroke with
  `virtual_key > 0xFF` is skipped. `VK_PAD_*` codes are 0x5800 and above, so pad
  keystrokes from the XInput or MnK drivers do nothing (MnK enqueues `VK_PAD_*`,
  `mnk_input_driver.cpp` L388–396). Keys at or below 0xFF are looked up in a key
  table and sent on as key events (`sub_824EFDA8`, `175.cpp` L10708–10753) and
  characters (`sub_824F0AC8` when `unicode` at +0x5A is nonzero, L10770–10784). This matches
  UE3's USB-keyboard path. Whether any All Stars screen binds keyboard keys is
  **unverified**.
- Because `0xFF & XINPUT_FLAG_GAMEPAD` is nonzero, the SDK forwards these queries
  to the drivers (`xam_input.cpp` L167–170). A driver could feed keyboard
  strokes with no guest hook at all, unlike SVR2011's `sub_82905130` hook.
  For pad scripting, `X_ERROR_EMPTY` (SVR2011's behaviour) is correct.

## 4. What exists here today

- A keyboard script (removed in ticket 11): `keybd_event` taps (`Return*3`,
  `wait:<ms>`) with focus bouncing. The keys only reach the game with
  `--mnk_mode=true` (`docs/boot-investigation.md` L51). The pad needs
  `--input_backend=xinput` (L63).
- A menu stepping script (removed in ticket 11): waited for N smoke screenshots,
  sent keys, waited, and returned the newest screenshot. Wall-clock only.
- `scripts/smoke.ps1`: already captures with `PrintWindow(..., 2)` every N seconds
  (L39–65, L76–84). It does not park or de-focus the window, and does not mute.
- `src/`: no input code. `WweAllStarsApp` overrides only `OnPostSetup`,
  `OnShutdown` and `OnPreLaunchModule` (`src/wweallstars_app.h` L20–38).
- **Frame counter:** the `D3DDevice_Present` hook keeps `g_presents`, one per guest
  frame (`src/d3d_census.cpp` L63, L445–457). It already numbers
  `--wwe_native_dump_frame` dumps, so it is the oracle-frame clock.
  `d3d_census.cpp` is always built (`CMakeLists.txt` L13).

## 5. Smallest port for All Stars

Files to port, with attribution to KaikoClanworth1/wwe-svr2011-pc:

| From (SVR) | To (here) | Change |
|---|---|---|
| `port/src/script_input.h`, `.cpp` | `src/script_input.h`, `.cpp` | namespace `wwe`; drop `type`, `key`, `touch`, `drag` and the `keyboard_typing.h`/`touch_controls.h` includes (`.cpp` L12–13, L110–140, L190–204); add frame timing (below) |
| `svr2011_app.cpp` L206–215 | `WweAllStarsApp::OnPreSetup` | gate on a cvar `--wwe_input_script=<path>` (as in `tooling.md` §4) or an env var; build `InputSystem` + `ScriptInputDriver` + `SlotAssignment` |
| `port/tools/session.ps1` | `scripts/session.ps1` | our exe and paths, `smoke.ps1` arguments, `--audio_mute=true`, no `SVR2011_NATIVE_*`; drop `test_guard.ps1` and test saves, or point `--user_data_root` at a test folder |

Not ported: `keyboard_typing.*` (SVR2011 hook addresses; All Stars needs none
for pad play), `touch_controls.*`, `CreateControllerWatch`.

The two keyboard scripts become obsolete once `session.ps1 input` and `shot`
exist (done in ticket 11). `smoke.ps1` can gain `-InputScript` and reuse the same capture.

### Frame-number timing (new; nothing to port)

Scenes are defined in frame numbers (`CONTEXT.md`: Scene, Oracle frame), and
SVR2011 has none. Proposal:

- Expose `uint64_t wwe::PresentCount()` from `d3d_census.cpp` (`g_presents`).
- Durations accept a frame suffix (`press A 6f`, `wait 120f`). Plain numbers stay
  milliseconds, so SVR2011 scripts still parse.
- Add `at <frame>`: a neutral pad until `PresentCount() >= frame`. It anchors a
  scene to absolute frames, so drift does not build up across waits.
- In `CurrentStep`, a frame step ends when `PresentCount() >= start + n` instead of
  at `step_end_`.
- Caveat: `Present` and the input poll run on guest threads, and the poll reads
  the counter with relaxed ordering. A step can land one frame early or late.
  Scenes should capture frames a few frames after the input that leads to them,
  or wait on state (`tooling.md` §4, "until census shows scene X").
- `session.ps1 shot` stays wall-clock. Exact oracle frames come from
  `--wwe_native_dump_frame=N` (`src/native/native_renderer.cpp` L38–42, L1727–1729), not from
  `PrintWindow`.

### Tests (TDD seam)

`ScriptInputDriver` with an injectable clock and frame source is a pure unit.
Feed it lines, advance the clock or frame count, call `GetDeviceState`, and
assert buttons and sticks. It needs neither the guest nor a window.

## Open questions

- Whether any All Stars screen reacts to USB-keyboard keystrokes (VK ≤ 0xFF).
  This decides whether a `key`/`type` verb is worth adding to
  `GetDeviceKeystroke` later.
- Whether the frame counter should count `Present` calls (as now) or the vblank
  the guest waits on, if the game ever presents twice per tick.
