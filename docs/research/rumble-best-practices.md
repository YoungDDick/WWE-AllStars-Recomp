# Rumble best practices for Enhanced vibration

Date 2026-10-04. Question: the Enhanced hit pulses work but feel dated. What do
first-party guidelines say makes rumble feel modern, what can our output path
do, and what should change? Current design: `.scratch/enhanced-vibration/spec.md`,
`docs/research/controller-vibration.md`, `src/vibration/`.

Tags: **[P]** primary source (linked), **[S]** this repo's source or disassembly,
**[I]** my inference, to be confirmed by the owner's playtest.

## Why ours feels dated (diagnosis)

- Every pulse drives both motors with the **same amplitude** (`(strength × 2) &
  0xFE` on left and right, `src/vibration/pulse_tiers.h`) [S]. Microsoft says the
  two motors are different: "setting these properties to the same value doesn't
  produce the same vibration", the left is "stronger … at a lower frequency" and
  the right "gentler … at a higher frequency" [P][ms-wgi]. With equal values the
  heavy left motor dominates. Every hit then feels like the same thud, only
  louder or longer [I].
- The envelopes are **flat** (constant, 80–180 ms) or a plain linear fade [S].
  None has a sharp onset followed by a different body. Apple builds custom
  haptics from two parts: "transient events are brief and compact, often feeling
  like taps or impulses" and "continuous events feel like sustained vibrations"
  [P][apple-hig]. A flat 80–180 ms block is neither: too long for a tap, too
  short and featureless for weight [I].
- Android's design guidance calls long one-shot vibrations "legacy" and
  "buzzy". It says "given the choice of buzzy haptics or no haptics … choose no
  haptics" [P][android-principles]. That guidance is written for phone
  actuators; applying it to gamepad ERM motors is [I].

## 1. Principles (cited)

| Principle | What the source says | Source |
|---|---|---|
| Motor roles | XInput: "The left motor is the low-frequency rumble motor. The right motor is the high-frequency rumble motor. The two motors are not the same, and they create different vibration effects." | [P][xinput-vib] |
| Motor roles, feel | WGI: the left motor gives "rough, high-amplitude vibration", the right "gentler, more subtle vibration". The motors are "rigidly connected by the gamepad body", so players don't feel them separately, and orchestrating them gives "a wider, more expressive range of sensations". | [P][ms-wgi] |
| Transient vs. continuous | Haptics are made of transient events (taps, impulses) and continuous events (sustained). Each has an intensity and a sharpness, from "soft, rounded, or organic" to "crisp, precise, or mechanical". | [P][apple-hig] |
| Vary with context | "A collision or a hit" should feel very different from subtle events, and a jump from a tree stronger than a jump in place. | [P][apple-hig] |
| Clear, not buzzy | Prefer "crisp and clean sensations associated with a discrete event" and avoid "noisy … vibrations that leave an after effect". A good click lasts "10 to 20 milliseconds". | [P][android-principles] |
| Intensity hierarchy | "Haptic effects applied to very frequent events … should be very subtle". More important events should be stronger. | [P][android-principles] |
| Avoid fatigue | "Avoid overusing haptics … can become tiresome when it plays frequently." "Less is more. Too much vibration can be annoying and even numbing to the hands." | [P][apple-hig], [P][android-principles] |
| Consistency, cause and effect | Build "a clear, causal relationship between each haptic and the action that causes it"; don't reuse a pattern for the opposite meaning. | [P][apple-hig] |
| Sync with audio and visuals | Match "the intensity and sharpness of a haptic with the intensity and sharpness of the animation it accompanies". A misaligned haptic "can be a bit unsettling". | [P][apple-hig], [P][android-principles] |
| Per-motor authoring | Unreal Engine's Force Feedback Effect asset has channels, each with its own intensity curve over time and its own choice of motors (Left Large, Left Small, Right Large, Right Small). Duration comes from the last curve key. | [P][ue-ffe] |
| First-party envelope shapes | Microsoft's vibration sample uses millisecond-scale segments. Recoil: trigger 1.0 for 20 ms, 10 ms off, then both body motors 1.0 for 90 ms. Heartbeat: 25 ms at 0.2, 200 ms gap, 25 ms. Footsteps: 25 ms taps at 0.3. | [P][ms-sample] |
| Strength setting | Players "should have the ability to: turn off the feedback; adjust the strength of the feedback". Example sliders go from 0 to 100. | [P][xag110] |
| Never the only cue | "Haptic feedback shouldn't be the only method of conveying information." Players with sensory-processing conditions or chronic pain may find it "painful". | [P][xag110] |
| Optional | "Make haptics optional. Let people turn off or mute haptics." | [P][apple-hig] |

Sony's DualSense and Nintendo's HD Rumble design documents are behind partner
portals; I found no public primary design text, so nothing here relies on them.
GDC has run talks on the subject (e.g. [gdc-2024], [gdc-spiderman2]), but only
the abstracts are public; I did not use their content.

## 2. What our output path can do

Chain: guest `XamInputSetState` (two `u16` motor speeds) → `InputSystem::SetState`
→ SDL3 `SDL_RumbleGamepad` (default backend, `input_backend = "sdl"`) or
`XInputSetState` (`.tools/rexglue-source/src/input/`) [S].

- **SDL version**: vendored `thirdparty/sdl3` is a submodule at `8bf3b72`
  (2026-03-14, `SDL_version.h` says 3.5.0) [S].
- **Two motors, 16 bits, per call.** `SDL_RumbleGamepad(low, high, duration)`:
  low = left motor, high = right motor. "Each call … cancels any previous rumble
  effect", and it needs `SDL_UpdateJoysticks`/event processing [P][sdl-h].
  Duration is capped at `SDL_MAX_RUMBLE_DURATION_MS` = 0xFFFF; SDL resends the
  current rumble every `SDL_RUMBLE_RESEND_MS` = 2000 ms, and an identical value
  pair only extends the expiration [P][sdl-joy]. Our driver arms 0xFFFF ms each
  call, so the guest's per-frame values are the envelope [S]
  (`sdl_input_driver.cpp:244-250`).
- **Update rate**: the game calls `ApplyForceFeedback` once per frame, ~60/s [S].
  The shortest step is one frame (16.7 ms), which matches the 20–25 ms segments
  in Microsoft's sample [P][ms-sample]. Per-pad limits in SDL's drivers:
  - Switch Pro (HIDAPI): at most one write per `RUMBLE_WRITE_FREQUENCY_MS` = 30 ms.
    Inside that window SDL keeps the strongest pending value, so a one-frame peak
    is not lost [P][sdl-switch]. SDL drives it at fixed frequencies (high band
    `0x74`, low band `0x3D`) and only scales amplitude. The comment notes that
    some pads treat any non-zero value as on/off [P][sdl-switch].
  - Xbox One/Series (HIDAPI): it scales to 0–100 (`/655`), and a new packet waits
    until the previous one is acknowledged (`rumble_state`) [P][sdl-xboxone].
  - DualSense (HIDAPI): it takes 8 bits (`>> 8`) and uses Sony's **rumble
    emulation** on the voice-coil actuators. On firmware ≥ 2.24 (and DS Edge) it
    uses "improved rumble emulation"; older firmware halves the values "to match
    Xbox controllers". Audio haptics are disabled while rumbling
    [P][sdl-ps5]. So DualSense users feel an emulation of two ERM motors, not HD
    haptics [I from P]. DualShock 4: 8 bits, same mapping [P][sdl-ps4].
  - Windows Xbox pads via RawInput: SDL prefers `XInputSetState` for body rumble
    ("allows rumble in the background") and switches to WGI while triggers are
    rumbling [P][sdl-rawinput].
- **Trigger rumble**: `SDL_RumbleGamepadTriggers` is "currently only supported on
  Xbox One gamepads" [P][sdl-h]. In source it works through WGI/RawInput or
  HIDAPI Xbox One; PS4/PS5/Switch return unsupported [P][sdl-rawinput],
  [P][sdl-xboxone], [P][sdl-ps5]. `XINPUT_VIBRATION` has only two fields, so the
  `xinput` backend cannot do it [P][xinput-vib]. GameInput has four
  (`lowFrequency`, `highFrequency`, `leftTrigger`, `rightTrigger`) [P][gi-rumble].
  The guest request carries two values, so trigger rumble would need a host-side
  side channel [S].
- **UE3 waveform (guest) capabilities.** Sample functions in `0x829EB418` (switch
  on the byte, `cmplwi r10,6`), verified by disassembly [S]:
  0 constant, 1 linear increasing (`t/d`), 2 linear decreasing (`1 − t/d`),
  3 `sin(t/d · π/2)` (constant `0x8206488C` = 1.5708), 4 `sin((t/d + 1) · π/2)`
  (cosine fall), 5 `sin(t/d · π)` (`0x8207079C` = 3.1416, a bump), 6 noise
  (LCG `×0x0BB38435 + 0x3619636B` at `0x829EB5E8`–`0x829EB620`, random 0..amp
  each frame). Each sample sets **left and right amplitude and function
  separately** (`0x82B51660`–`0x82B51680`) [S]. The amplitude cap is 100
  (= strength 50); above it the 16-bit result wraps (`clrlwi`/`lhz`) [S].
  Multi-sample waveforms already work: the critical double pulse appends samples
  with `TArray::AddItem` `0x82508B30` [S].

## 3. Recommendations for this game

### 3.1 Give each tier a two-part shape on two motors (highest value)

Build each hit as a **short transient** (a "crack" on the right, high-frequency
motor), then a **decaying body** (weight on the left, low-frequency motor). That
is two samples per waveform, with left and right set independently. Starting
values (motor fraction / ms; the table stays owner-tunable) [I, shaped by the
principles above]:

| Tier | Sample 1: impact (constant, 2 frames ≈ 33 ms) | Sample 2: body | Intent |
|---|---|---|---|
| light (strike) | R 0.55, L 0.10 | none | a crisp tap, no thud |
| strong (strong/charged strike, aerial) | R 0.85, L 0.35 | L 0.45 → 0, fn 2, 100 ms; R 0 | a crack plus a short thump |
| grab (throws, slams) | R 0.60, L 0.60 | L 0.70 → 0, fn 4 (cosine fall), 220 ms | the weight of hitting the mat |
| weapon | R 1.0, L 0.30 | R 0.35 noise (fn 6), 60 ms; L 0.30 → 0, fn 2, 100 ms | a metallic rattle |
| big moment (finisher, extreme sig.) | R 1.0, L 1.0, 50 ms | L 1.0 → 0, fn 4, 550 ms; R 0.3 → 0, fn 2, 200 ms | the only full-left event |
| critical health | keep the double pulse, but put it on the **left only**, ~0.5, 2 × 70 ms, gap 120 ms | | a heartbeat; Microsoft's sample uses the same shape [P][ms-sample] |

Rules behind it:

- **Hierarchy**: only big moments reach full left motor. Light hits never touch
  the left motor much. Hits are the game's most frequent event, so they should
  be the subtlest [P][android-principles].
- **Transients ≥ 2 frames.** `0x829293C8` advances `CurrentSample` by elapsed
  time. A sample shorter than one frame may be skipped or seen for only one
  frame [I, from S]. Confirm in the vibration trace that sample 1 produces
  exactly two non-zero frames.
- **Fades over flats**: use fn 2 or fn 4 for bodies, never long constants (the
  Android "legacy long vibration" rule [P][android-principles]).
- **Stronger wins** should compare the *peak of the remaining samples per motor*
  (`PlayingStrength` today takes one strength) [S]. A light right-motor tap
  should not cancel a heavy left-motor body. Better still, let a light tap
  replace the waveform only if the playing body's remaining left amplitude is
  below the tap's [I].

### 3.2 Avoid fatigue in combos

Light strikes come in strings. Proposal: within ~150 ms of the last hit pulse,
scale a repeat hit of the same tier to ~60 % and skip its body sample. Any
higher-tier hit resets this. Apple's "tiresome when it plays frequently" and
Android's "numbing" warnings are the basis [P][apple-hig],
[P][android-principles]. The numbers are [I].

### 3.3 Strength slider (do it)

Add a **Vibration strength** 0–100 % row under the CONTROLS tab, next to the
mode (XAG 110 asks for both off and strength [P][xag110]). Apply it at the host
seam (`InputSystem::SetState`, where Off already lives), so it scales the game's
own aerial pulse too and the ledger still records what the game asked for [S].
Use a perceptual curve (e.g. `value²`) so the low end stays usable [I].

### 3.4 Keep the envelope in the guest; don't drive one from the host

- Guest waveforms pause with the game, use the game's frame time and already
  pass the ledger [S]. One frame (16.7 ms) is fine enough for the 20–33 ms
  segments first-party samples use [P][ms-sample].
- A host thread at 200+ Hz would gain little. Switch writes are throttled to
  30 ms, Xbox One HIDAPI waits for each acknowledgement [P][sdl-switch],
  [P][sdl-xboxone], and the host would lose pause sync [I]. Revisit only if
  the trace shows transients smearing on a specific pad.

### 3.5 New events (research needed, ranked)

1. **Attacker confirm** (your hit lands): right motor only, ~0.25, 2 frames, no
   body. It is lighter than any victim pulse, so roles stay clear (Apple's
   cause-and-effect rule [P][apple-hig]). The attacker is reachable
   (`victim+0x95C`) [S]. Needs the possession gate and `RumbleTarget` check on
   the attacker side [S].
2. **Mat impact** for slams and throws: if the landing frame can be hooked, move
   the grab body (left decay) to the landing, not the grab start. The rumble then
   lands with the impact sound and animation [P][apple-hig] (sync). Hook not
   found yet [S].
3. **Pin count**: one 2-frame right-motor tap per referee count, in time with the
   count sound. A soft left decay on "3" if it is your win or loss [I].
4. **Match end**: one big-moment shape for a KO/pin loss; nothing long for a win
   (avoid reusing the "hurt" pattern for a positive outcome, Apple's
   consistency rule [P][apple-hig]).
5. **Trigger impulses** (Xbox One/Series only, SDL backend): a short right-trigger
   tap on the attacker's landed strong hit. Low priority: it needs a host
   channel beside the two guest values, and it does nothing on PS/Switch/XInput
   [P][sdl-h], [P][xinput-vib].

### 3.6 Platform notes for testing

- Test on an Xbox pad first; the two-motor split is designed for it [P][ms-wgi].
- On DualSense, firmware < 2.24 halves strength [P][sdl-ps5]; tune for current
  firmware.
- Switch Pro uses fixed frequencies; the left/right split may feel less distinct
  there [P][sdl-switch].

## Sources

- [xinput-vib]: https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_vibration
- [ms-wgi]: https://learn.microsoft.com/en-us/windows/uwp/gaming/gamepad-and-vibration (sections "Vibration and impulse triggers overview", "Using the vibration motors")
- [ms-sample]: https://github.com/microsoft/Xbox-ATG-Samples/blob/main/UWPSamples/System/GamepadVibrationUWP/GamepadVibrationUWP.cpp (effect tables, lines 61–77; recoil logic, lines 355–404)
- [gi-rumble]: https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/structs/gameinputrumbleparams
- [xag110]: https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/110
- [apple-hig]: https://developer.apple.com/design/human-interface-guidelines/playing-haptics ("Best practices", "Custom haptics")
- [android-principles]: https://developer.android.com/develop/ui/views/haptics/haptics-principles
- [ue-ffe]: https://dev.epicgames.com/documentation/en-us/unreal-engine/force-feedback-in-unreal-engine
- [sdl-h]: https://github.com/libsdl-org/SDL/blob/8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4/include/SDL3/SDL_gamepad.h#L1513-L1565 (also https://wiki.libsdl.org/SDL3/SDL_RumbleGamepad)
- [sdl-joy]: https://github.com/libsdl-org/SDL/blob/8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4/src/joystick/SDL_joystick.c#L2044-L2085 and `src/joystick/SDL_sysjoystick.h` L237, L241
- [sdl-switch]: https://github.com/libsdl-org/SDL/blob/8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4/src/joystick/hidapi/SDL_hidapi_switch.c (L55; L1690–1716; L1750–1792)
- [sdl-xboxone]: https://github.com/libsdl-org/SDL/blob/8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4/src/joystick/hidapi/SDL_hidapi_xboxone.c#L540-L607
- [sdl-ps5]: https://github.com/libsdl-org/SDL/blob/8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4/src/joystick/hidapi/SDL_hidapi_ps5.c (L434–439; L713–730; L1002–1022)
- [sdl-ps4]: https://github.com/libsdl-org/SDL/blob/8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4/src/joystick/hidapi/SDL_hidapi_ps4.c#L892-L893
- [sdl-rawinput]: https://github.com/libsdl-org/SDL/blob/8bf3b7215ad9fc3deb583c6a3a37c6c67f2e24e4/src/joystick/windows/SDL_rawinputjoystick.c#L1455-L1515
- [gdc-2024]: https://schedule.gdconf.com/session/haptics-a-playful-collaboration-between-audio-and-design/898952 (abstract only)
- [gdc-spiderman2]: https://schedule.gdconf.com/session/audio-summit-haptic-design-and-implementation-of-marvels-spider-man-2/901630 (abstract only)
- [S] local: `.tools/rexglue-source/src/input/sdl/sdl_input_driver.cpp`,
  `src/input/input_system.cpp`, `src/vibration/*.h`; disassembly via
  `python scripts/disasm.py 829EB418 120`.
