# Xenia Canary baseline: WWE All Stars (54510866)

Date: 2026-10-02. This is the reference run for checking the static recompilation against an emulator.
Raw logs, screenshots and capture clips are in `analysis/xenia-baseline/` (gitignored).

## Setup

| Item | Value |
|---|---|
| Emulator | Xenia Canary, release tag `02d2cb5` (asset `xenia_canary_windows_.zip`, published 2026-03-24) |
| Build string | `canary_experimental@02d2cb5cc on Mar 24 2026` (release target commitish `925ed98d5dce604b651027c36fb522dc1ff0fa55`) |
| Location | `.tools/xenia-canary/xenia_canary.exe`, with `portable.txt` (config/content/cache stay in that folder) |
| Host | Windows 11, AMD RX 7900 XT, AMD driver 26.9.1 (Vulkan 1.4.349, LLPC) |
| Disc | `WWE All Stars (USA, Europe).iso` (opened read-only by Xenia, not modified) |
| Title | `[54510866 v0.0.0.5] WWE® All Stars™`, Media ID 60CEF505, XEX built 2011-02-23, original PE `Game-Retail.exe`, XDK 2.0.11775.3 libs |

Note: the release asset is named `xenia_canary_windows_.zip` (trailing underscore). `xenia_canary_windows.zip` does not match.

### Runs

| Run | Command-line flags (all others default) | Log |
|---|---|---|
| run1 | `--log_file=… --log_level=2 --gpu=vulkan` | `run1_vulkan.log` (100 KB, ~85 s, killed on title screen) |
| run2 | `--log_level=3 --gpu=vulkan --hid=winkey --keyboard_mode=1 --discord=false` | `run2_vulkan_debug.log` (289 MB, ~6.5 min, into a match) |
| run3 | `--log_level=2 --gpu=d3d12 --hid=winkey --keyboard_mode=1 --discord=false` | `run3_d3d12.log` (0.8 MB, ~4 min, into a match) |

Defaults that matter: `vsync=true`, `framerate_limit=0`, 1280x720 internal, `draw_resolution_scale=1`, `readback_resolve="none"`, `readback_memexport=false`, `mount_cache=true`, `apu=any` (XAudio2), no game patches (`PatchDB: Loaded patches for 0 titles`).

Input: with the default `hid=any` / `keyboard_mode=0`, keyboard input does nothing. A physical Xbox One pad attached through SDL takes slot 0. `--hid=winkey --keyboard_mode=1` turns on the keyboard. Injected keys only register through `keybd_event` while Xenia has focus. `PostMessage(WM_KEYDOWN)` does not work. Default binds: Start=`X`, A=`;` (VK 0xBA), B=`'` (0xDE), D-pad=`W/A/S/D`.

## How far it got

**It is fully playable through a match on both Vulkan and D3D12.** There were no crashes, hangs or fatal errors, and every run was stopped by killing the process.

| Stage | Screenshot(s) |
|---|---|
| THQ San Diego logo FMV (about 10 s in) | `run2_t010s.png` |
| Title "PRESS START" (about 25 s in) | `run1_t080s.png`, `run2_t025s.png`, `run3_t040s.png` |
| Autosave notice (after Start) | `run2_after_start_4.png`, `run3_s2.png` |
| Main menu (Exhibition / Xbox LIVE / Path of Champions / Fantasy Warfare / CAS) | `run2_menu_2.png`, `run3_menu_main.png` |
| Exhibition → Standard → 1 vs 1 | `run2_menu_3.png`, `run2_menu_4.png` |
| Controller assignment | `run2_menu_5.png`, `run2_menu_6.png` |
| Character select (Hogan vs Cena) | `run2_menu_7.png`, `run3_menu_chars.png` |
| Arena select (SmackDown) | `run2_menu_9.png` |
| Arena fly-in / entrances | `run2_match_10.png`, `run2_match_12.png`, `run3_match_load.png` |
| In-match gameplay (HUD, crowd, ring, shadows) | `run2_match_16.png`, `run2_match_18.png`, `run3_match_2.png` |
| 10 s gameplay clips | `run2_gameplay_10s.mp4`, `run3_gameplay_10s.mp4` |

Side effects of the runs (all inside `.tools/xenia-canary/content/`): Xenia created a profile `E030000060FF33CA` (gamertag "shaneomac") when the game requested sign-in. The game then created `STORY.SAV` (20 KB) and `GameTrackedStats.SAV` (1 MB) for title 54510866, so later runs skip first-boot paths.

`run2_t040s.png` and `run2_t055s.png` are 2560x1440 because the Xenia window briefly went fullscreen during run2. The log shows 2560x1440 and 2560x1349 swapchain recreations. This is a harness artefact, not a game behaviour.

## Frame rate

- The window title shows no FPS counter in this build, and no FPS overlay was used.
- Proxy from the run2 debug log: `VdGetSystemCommandBuffer` was called 23,042 times and `VdRetrainEDRAM` 23,043 times. Both are called once per D3D present. Over about 380 s of emulation that is **about 60 presents/s**, so the game ran at full speed with vsync even with debug logging on.
- Screen capture (gdigrab, about 50 fps capture ceiling): run2 gameplay had 490 of 499 captured frames unique. That is consistent with at least 50 fps of distinct frames, limited by the capture. No visible slowdowns in menus, FMV or the match.
- For an exact number, use RTSS or PresentMon on a future run. RTSS is installed at `D:\tools\RivaTuner Statistics Server` but was not running.

## Unimplemented imports

The title imports 119 xam exports (14 unimplemented in Xenia) and 177 xboxkrnl exports (16 unimplemented). Xenia marks unimplemented imports with `!!` in the module dump (see `run1_vulkan.log`).

### Imports flagged unimplemented (static, 30)

| Module | Export (ordinal) | Called at runtime? |
|---|---|---|
| xam | XNetLogonGetTitleID (0x136) | no |
| xam | XamShowFriendsUI (0x2BF), XamShowPlayersUI (0x2C8), XamShowMessagesUI (0x2C0), XamShowGamerCardUIForXUID (0x2D5), XamShowPlayerReviewUI (0x2C6), XamShowGameInviteUI (0x2CD), XamShowFriendRequestUI (0x2CE) | no (LIVE UI) |
| xam | NetDll_WSAGetOverlappedResult (0x10), NetDll_XNetQosLookup (0x46), NetDll_XNetGetConnectStatus (0x42), NetDll_XNetConnect (0x41), NetDll_XNetUnregisterInAddr (0x3F), NetDll_XNetServerToInAddr (0x3A) | no (networking was never started beyond `XNetStartup`/`WSAStartup`) |
| xboxkrnl | IoInvalidDeviceRequest (0x41), IoCompleteRequest (0x35), IoCheckShareAccess (0x34), IoSetShareAccess (0x47), IoRemoveShareAccess (0x45), ObIsTitleObject (0x109) | no (title-side device driver code, likely the XMount/utility-drive or STFS helper) |
| xboxkrnl | **IoDismountVolumeByFileHandle (0x3C)** | **yes, 2 calls** |
| xboxkrnl | IoDismountVolume (0x3B) | no |
| xboxkrnl | XeKeysConsoleSignatureVerification (0x257), StfsCreateDevice (0x259), StfsControlDevice (0x25A) | no |
| xboxkrnl | RtlCaptureContext (0x119), RtlUnwind (0x147), `__C_specific_handler` (0x1A5) | no (SEH; only reached on exceptions) |
| xboxkrnl | RmcDeviceRequest (0x2E1), MicDeviceRequest (0x2B1) | no |

### Unimplemented or stub behaviour hit at runtime (deduplicated, run2 counts)

| Count | Log line | Context |
|---|---|---|
| 2 | `!> undefined extern call to 83500404 IoDismountVolumeByFileHandle` | Boot. The game opens `\Device\Harddisk0\partition0`, then `\Device\Harddisk0\Cache1`, queries the volume (`NtQueryVolumeInformationFile` class 4, which hits `Stub XFileFsDeviceInformation!`), issues IOCTLs 0x70000 and 0x74004, writes 4x 0x1000 bytes at offsets 0/0x1000/0x2000/0, then dismounts. This is the XDK utility-drive (cache partition) format/mount sequence. Xenia returns 0 and the game carries on. |
| 2 | `w> Stub XFileFsDeviceInformation!` | Same sequence as above |
| 4 | `w> ERROR: fn export referenced GetProcAddressByOrdinal(048C(XamVoiceSetMicArrayIdleUsers)) is not implemented` / `XexGetProcedureAddress ordinal 1164 (0x48C) in 'xam' not found!` | Resolved dynamically with `XexGetModuleHandle("xam.xex")` + `XexGetProcedureAddress(…, 0x48C)` right after `XamUserReadProfileSettings(FFFE07D1, …)`. The game handles a NULL result. |
| 2 | `w> NtSetInformationFile ignoring alloc` | File allocation-size hint, harmless |
| 1 | `w> User … doesn't have profile GPD!` | First sign-in on a fresh profile |
| 33 | `!> XmaContext N: There is no bits to copy!` | XMA decoder edge case (11 each on 3 contexts). No audible problem was checked, because audio was not captured. |
| ~180k | `A> XmaContext N: Output ring buffer empty, invalidating output` | Debug-level XMA chatter, normal |

No `Unimplemented` GPU opcodes, no shader translation failures, no Vulkan/D3D12 validation errors, no guest exceptions, no `KeBugCheck`.

## Kernel/XAM calls observed (run2, debug level)

The full histogram is in `analysis/xenia-baseline/kernel_calls_run2.txt` (102 distinct exports). Xenia does not log high-frequency exports at this level (`log_high_frequency_kernel_calls=false`), so `VdSwap`, `KeWaitForSingleObject`, `NtReadFile`, `KeDelayExecutionThread` and similar are missing from the counts.

Top calls: XamInputGetKeystrokeEx 1,067,379 · XamUserGetDeviceContext 297,514 · KeTlsSetValue 85,252 · NtReleaseMutant 54,089 · VdRetrainEDRAM 23,043 · VdGetSystemCommandBuffer 23,042 · XamInputGetCapabilities 21,308 · XamInputSetState 21,304 · MmGetPhysicalAddress 16,047 · NtAllocateVirtualMemory 13,901 · NtFreeVirtualMemory 10,785 · NtClose 7,286 · NtCreateMutant 7,100 · XMsgInProcessCall 3,726 · XMPGetStatus 3,339 · XGetGameRegion 1,375 · ExGetXConfigSetting 1,374.

Notable one-off calls: XamShowSigninUI, XamShowDeviceSelectorUI, XamContentCreateEx (x2), XamContentCreateEnumerator (x5), XamUserCreateAchievementEnumerator, XeKeysConsolePrivateKeySign, XeCryptSha, XexLoadImage, XexGetModuleSection, NetDll_XNetStartup, NetDll_WSAStartup (x2), XAudioRegisterRenderDriverClient, the full Vd* init sequence (VdInitializeEngines, VdInitializeRingBuffer, VdEnableRingBufferRPtrWriteBack, VdSetGraphicsInterruptCallback, VdInitializeScalerCommandBuffer, VdSetDisplayMode, VdPersistDisplay, VdRetrainEDRAMWorker…).

Threads: the main thread plus 27 guest threads were created by the time of the match (`XThread::Execute thid 6..28`). The title uses `ExCreateThread` (26 calls) with `KeSetAffinityThread` (26) and `KeSetBasePriorityThread` (40).

## Module and load info

- `Loading module GAME:\default.xex` → `\Device\Cdrom0\Default.xex`, module flags `XEX_MODULE_TITLE`, image flags `XEX_IMAGE_XGD2_MEDIA_ONLY`.
- Load address 0x82000000, image size 0x018C0000, entry point **0x8241A8E0**, export table 0x83500B14, encryption Normal / compression Basic, data size 22,118,400.
- Sections: RODATA 0x82000000–0x82410000, CODE starting at 0x82410000 (per-64 KB section list in the log). Resource `54510866` at 0x83820000–0x838BD39B (XDBF/SPA).
- TLS: 64 slots, no raw TLS data. Default stack 256 KB. System flags `XEX_SYSTEM_USES_GAME_VOICE_CHANNEL | XEX_SYSTEM_PAL50_INCOMPATIBLE`.
- Header import libraries: `xam.xex` 238 records, `xboxkrnl.exe` 341 records, both min version 2.0.12625.0. These counts include thunk and variable records. The resolved function count is 119 xam / 177 kernel.
- Engine: Unreal Engine 3 layout (`\Game\CookedXenon`, `\Game\Config\Xenon\Cooked`, `\Game\Localization`, `\Game\Movies`, `\Splash`).

### Log line patterns (for writing parsers)

```
i> <tid8> Title name: WWE® All Stars™
i> <tid8> Module \Device\Cdrom0\Default.xex:            # followed by XEX header dump
 xam - 119 imports / xboxkrnl - 177 imports            # import table header
   F <thunk-slot> <thunk-addr> <ord-hex> (<ord-dec>) [!!] <Name>   # function import; "!!" = unimplemented in Xenia
   V <slot>                <ord-hex> (<ord-dec>)       <Name>      # variable import
d> <tid8> <Export>(<args…>)                            # kernel call trace (log_level>=3)
!> <tid8> undefined extern call to <thunk-addr> <Name> # unimplemented import actually called
w> <tid8> ERROR: fn export referenced GetProcAddressByOrdinal(<ord>(<Name>)) is not implemented
K> <tid8> XThread::Execute thid N (handle=…, '<name>', native=…)
F> <tid8> DiscImageDevice::ResolvePath(<path>)         # VFS lookups
G> / A>                                                # GPU texture/cache, XMA audio debug chatter
```

Prefixes: `i` info, `w` warning, `!` error, `d` debug, `K` kernel/thread, `F` filesystem, `G` GPU, `A` APU.

## Graphical issues seen in screenshots

- None obvious on either backend. Menus, FMV, the character-select hex-grid background, the arena crowd, HUD, ring ropes, dynamic shadows and the depth-of-field arena fly-in all look correct at 1280x720.
- `run2_match_12.png` (entrance close-up, Vulkan) shows a hard-edged white silhouette fringe around Hogan. This could be the intended "All Stars" stylised rim/outline pass or an EDRAM resolve/edge issue. I did not check it against real hardware or against D3D12 on the same frame, so treat it as unconfirmed.
- Vulkan and D3D12 looked the same in comparable menu and gameplay shots.

## Recommendations for the recomp runtime

1. **Cache partition (utility drive) format.** Implement `IoDismountVolumeByFileHandle` and `IoDismountVolume` as success no-ops. Back `\Device\Harddisk0\partition0` and `\Device\Harddisk0\Cache1` with a host folder or null device. Make `NtQueryVolumeInformationFile(FileFsDeviceInformation=4)` return a plausible disk device type, and handle IOCTLs 0x70000 (get drive geometry) and 0x74004 (get partition info). Accept the 4 KB header writes. Xenia's stubs are enough for the game to continue, so this oracle shows that a "return success" level is enough.
2. **Dynamic import of `XamVoiceSetMicArrayIdleUsers` (xam ordinal 0x48C).** Either return NULL from `XexGetProcedureAddress` like Xenia, or provide a trivial stub returning 0. The game tolerates NULL.
3. **Input polling is very heavy**: `XamInputGetKeystrokeEx` runs at about 2,800 calls/s and `XamUserGetDeviceContext` at about 780 calls/s. Keep these cheap, with no locks or allocations and no logging in the hot path.
4. **Presents.** The `VdGetSystemCommandBuffer` + `VdRetrainEDRAM` pair runs once per frame at 60 Hz. Pace the host present to vsync and don't slow these calls down.
5. **SEH exports** (`RtlCaptureContext`, `RtlUnwind`, `__C_specific_handler`) are imported but were never hit in this session. Provide implementations or traps that log loudly, because UE3 uses `__try` in some loaders and error paths.
6. **LIVE, net and STFS exports** (XamShow*UI, NetDll_XNet* connect/QoS, StfsCreateDevice/StfsControlDevice, XeKeysConsoleSignatureVerification) are not reached offline. Stub them to return an error such as `X_ERROR_FUNCTION_FAILED` or `WSAENETDOWN`, and log once.
7. **Mic/RMC device requests** (`MicDeviceRequest`, `RmcDeviceRequest`) were not hit. Stub them to report "no device".
8. **Profile and saves.** The title signs in, reads profile settings (`FFFE07D1` dashboard settings), enumerates and creates content (`STORY.SAV`, `GameTrackedStats.SAV`), and shows an autosave notice. The runtime needs a signed-in profile and a writable content store before the main menu works. Xenia auto-created a profile when `XamShowSigninUI` was called.
9. **Oracle use.** For per-call comparisons, rerun with `--log_level=3` and optionally `--log_high_frequency_kernel_calls=true` to also get `VdSwap`, waits and file reads. Expect very large logs: 289 MB for about 6.5 minutes without high-frequency calls.

## Reproduce

```
cd .tools\xenia-canary
xenia_canary.exe --log_file=..\..\analysis\xenia-baseline\runN.log --log_level=2 --gpu=vulkan ^
    --hid=winkey --keyboard_mode=1 --discord=false "..\..\WWE All Stars (USA, Europe).iso"
```

Key path to a match: wait about 25 s → `X` (Start) → wait until the autosave dialog has loaded → `;` (A) → `;` Exhibition → `;` Standard → `;` 1 vs 1 → `A` (D-pad left, move pad to P1) → `;` → `;` `;` (characters) → `;` (arena) → `;` several times to skip entrances.
