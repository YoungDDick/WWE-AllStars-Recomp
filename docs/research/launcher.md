# Launcher: technology, features, branding, icon and packaging

Date 2026-10-04. Question: the core game is mostly done, and the owner wants a
production-grade launcher that looks like an official per-game launcher. Which
stack, which features, what art may it show, how should the game and launcher
get their icons, and how do we ship it? No launcher code is written here.

Tags: **[P]** primary source (linked in Sources), **[S]** this repo, the
SVR2011-Recomp repo or the ReXGlue source (file:line), **[I]** my inference, to
be confirmed by the owner.

Paths: `SVR:` means `D:\Projects\SVR2011-RECOMP`. `SDK:` means
`.tools/rexglue-source` (`f5337cdc` plus this repo's `patches/`). `pc:` means
https://github.com/KaikoClanworth1/wwe-svr2011-pc at `main`.

## Summary and recommendation

- **Plans already made:**
  - ROADMAP M5 already names an **Avalonia launcher**. Its jobs are extract
    and verify, shader pre-conversion, GPU selection and DLC/TU install
    (`docs/ROADMAP.md:51`) [S].
  - ADR-0010 and ADR-0012 give the launcher DLC install and shader conversion
    (`docs/adr/0010-dlc.md:7`, `docs/adr/0012-shader-delivery.md:11`) [S].
  - ADR-0006 makes Linux/Deck a target, so the launcher must be portable
    (`docs/adr/0006-platforms.md:7,11`) [S].
  - No ADR records the launcher stack yet [S].
- **Recommended stack 1: Avalonia 11+/.NET with Native AOT, as a separate
  launcher process.** Why:
  - It is MIT-licensed [P][avalonia-gh] and runs on Windows, macOS and Linux
    [P][avalonia-gh].
  - It can draw custom chrome (`ExtendClientAreaToDecorationsHint`)
    [P][avalonia-window] and use Mica, with Acrylic/Blur fallback, through
    `TransparencyLevelHint` [P][avalonia-tl].
  - Native AOT gives faster startup and needs no installed .NET runtime
    [P][avalonia-aot].
  - The owner already wrote a C# (WPF) launcher for SVR2011, about 1,535 lines.
    Its disc reader, key-art reader and settings model port almost directly
    (`SVR:launcher/portable/*.cs`) [S].
  - Xenia Manager is a BSD-3-licensed Avalonia app that covers much of the same
    ground: installing Xbox 360 content, saves, settings UI and shortcuts
    [P][xm-readme].
  - Costs:
    - C# cannot link the C++ settings registry, so the game should export it
      (see "Config hand-off").
    - Video backgrounds need a third-party player [I].
- **Recommended stack 2: an in-process front end in the game exe.** It reuses
  the game's SDL3 + Vulkan + Dear ImGui 1.92.5 stack (SDK `thirdparty/imgui`,
  `thirdparty/sdl3`) [S], optionally with RmlUi for HTML/CSS-style layout
  (MIT, Vulkan backend, animations, transitions, blur filters, controller
  navigation) [P][rmlui].
  - This is what the best recomps do:
    - Zelda64Recomp's launcher is an RmlUi screen with Select ROM / Start game
      / Controls / Settings / Mods / Exit (`assets/launcher.rml` L38–61)
      [P][z64].
    - Unleashed Recompiled has an in-process ImGui installer wizard: language,
      intro, game+update, DLC, space check, install, result
      (`ui/installer_wizard.cpp` L128–138) [P][ur].
  - Pros:
    - One exe.
    - Controller-first, which suits Steam Deck Game Mode.
    - Shares `src/settings/registry.h` directly.
  - Con: Dear ImGui's own FAQ says it "is NOT designed to create a user
    interface for games" and that skinning is limited [P][imgui-faq].
    Unleashed reaches console quality only with custom textures and fonts [P][ur].
- **Suggested split [I]:** Avalonia handles first-run setup and the home
  screen: ISO extract and verify, DLC/TU, shader conversion, Play, news and
  updates. The existing in-game PC SETTINGS page (F4, `wweallstars_app.h:69-75`)
  [S] stays the in-game settings surface. The launcher shows the same settings
  because both read one schema.
- **Rejected for this project:**
  - Electron: bundles Chromium plus Node [P][electron].
  - WinUI 3: Windows-only (conflicts with ADR-0006), and it cannot be a
    single-file exe [P][winappsdk-sc].
  - Qt 6: LGPL obligations; Qt Quick 3D and Qt Quick Timeline are GPL-only
    [P][qt-lic].
  - Ultralight: proprietary; the free tier is for indies under $100K
    [P][ultralight].
  - Flutter: Dart; ships `flutter_windows.dll`, a `data/` folder and VC++ DLLs
    [P][flutter-win].
  - Tauri 2 is a viable runner-up. On Windows it uses WebView2 (preinstalled
    on Windows 10 1803+ [P][tauri-prereq]). On Linux it needs WebKitGTK 4.1
    [P][tauri-prereq], so the Deck would render through a different engine [I].
  - Slint: its royalty-free licence requires visible attribution [P][slint-lic].
- **Icon:** the game exe has **no icon resource today**: no `.rc`, no `.ico`
  (`CMakeLists.txt:66-67`) [S]. Adding one `.rc` fixes Explorer, the taskbar
  and the game window together, because SDL3 picks the exe's first icon group
  (SDK `thirdparty/sdl3/src/video/windows/SDL_windowsevents.c:2797-2798`) [S].
  The only official icon art on the disc is a **64×64** title tile, too small
  for the 256 px Windows needs [P][ms-icon]. See the Icon section.

## 1. Local findings

### 1.1 How the game is built and launched today

- **Build:**
  - `scripts/build.ps1` checks `assets/default.xex` SHA-256 `521c2325…6892`
    before anything else (`scripts/build.ps1:16-19`) [S].
  - It builds with clang/clang++ from `.tools/llvm-21.1.8` through Ninja
    (`build.ps1:20-21`, `CMakePresets.json:8-12`) [S].
  - `RelWithDebInfo` "is what ships today" (`build.ps1:7-8`) [S].
  - The exe is a `WIN32` GUI target (`CMakeLists.txt:66-67`) [S]. It is 103 MB
    plus `rexruntimerd.dll`, `rexgpu-xenosrd.dll` and `TracyClientrd.dll` in
    `out/build/win-amd64-relwithdebinfo-vulkan/` [S].
- **Entry:** `REX_DEFINE_APP(wweallstars, WweAllStarsApp::Create)`
  (`src/main.cpp:10`); app hooks are in `src/wweallstars_app.h` [S].
- **Paths:**
  - `game_data_root`, `user_data_root`, `update_data_root`, `cache_root` and
    `metadata_root` are cvars (SDK `src/system/runtime.cpp:31-35`) [S].
  - Without `game_data_root` the runtime shows an error box and stops (SDK
    `src/ui/rex_app.cpp:208-219`) [S].
  - User data defaults to `GetUserFolder()/wweallstars`, and the cache to
    `<user>/cache` (SDK `rex_app.cpp:118-141`) [S].
  - The main config is `<exe dir>/wweallstars.toml` (SDK `rex_app.cpp:149-150`)
    [S].
- **Config layering:**
  1. `wweallstars.toml` loads first (SDK `rex_app.cpp:161-162`) [S].
  2. Then the PC SETTINGS file `wweallstars.settings.toml` loads
     (`src/wweallstars_app.h:41-43`, `src/settings/config_file.h:19-24`) [S].
  3. **Command-line flags win** over both (`wweallstars_app.h:41-42`) [S].
- **Pinned settings:** a setting given on the command line shows as "pinned" in
  the in-game page (`src/settings/registry.h:52-61`) [S]. A launcher that
  passes player settings as flags would therefore lock them in game.
- **Saving:** `SaveSetting` changes one key atomically through a temp file
  and a rename (`config_file.h:104-132`) [S].
- **Today's launch arguments** (scripts):
  - `--gpu_plugin=xenos --vulkan_device=-1 --render_target_path_vulkan=…`
  - `--game_data_root=<repo>/assets --user_data_root=<repo>/userdata/vulkan
    --cache_root=<repo>/cache/vulkan`
  - `--log_file --log_level --log_flush_interval=1`

  Sources: `scripts/smoke.ps1:26-32`, `scripts/session.ps1:78-83` [S]. The dev
  `wweallstars.toml` beside the exe holds absolute repo paths
  (`out/build/.../wweallstars.toml:1-5`) [S].

### 1.2 User-facing settings a launcher could expose

The player-facing schema already exists. It is `PlayerSettings()` in
`src/settings/registry.h:14-50`, which the file comment calls "the one list the
page, the config file and a later launcher read" (`registry.h:1-2`) [S].

| Tab | cvar | Choices | Defined at | Applies |
|---|---|---|---|---|
| DISPLAY | `fullscreen` | Windowed / Fullscreen | SDK `src/ui/window.cpp:37` | live (comment L34-36) |
| DISPLAY | `present_vsync` | On / Off | SDK `src/ui/vulkan/vulkan_presenter.cpp:52` | restart (live planned, [live-settings](live-settings.md)) |
| DISPLAY | `vblank_follows_display` | On / Off | SDK `src/graphics/graphics_system.cpp:41` | restart (live planned, [live-settings](live-settings.md)) |
| GRAPHICS | `wwe_native_renderer` | Native / Emulated (formerly Classic, ADR-0020) | `src/settings_cvars.cpp:7-10` | restart |
| GRAPHICS | `wwe_native_supersample` | On / Off | `settings_cvars.cpp:11-14` | restart |
| GRAPHICS | `wwe_native_cas` | 0 / 0.25 / 0.5 / 1 | `settings_cvars.cpp:16-19` | live (`patches/rexglue-live-sharpening.patch`) |
| GRAPHICS | `wwe_native_resolution_scale` | 1–3 (720p/1440p/2160p) | `settings_cvars.cpp:20-24` | restart |
| GRAPHICS | `wwe_native_anisotropy` | 0, 2–5 (off … 16x) | `settings_cvars.cpp:26-29` | restart |
| AUDIO | `audio_mute` | Off / On | SDK `src/audio/sdl/sdl_audio_driver.cpp:27` | — |
| CONTROLS | `wwe_vibration_mode` | Off / Original / Enhanced | `settings_cvars.cpp:31-34` | live |
| CONTROLS | `wwe_vibration_strength` | 0–100 % | `settings_cvars.cpp:36-39` | live |

SDK cvars that the registry leaves out but a launcher could reasonably expose:

- Window size: `window_width` and `window_height` (SDK `src/ui/window.cpp:24-32`)
  [S].
- `monitor` (SDK `window.cpp:39`) [S].
- GPU choice: `vulkan_device` (SDK `src/ui/vulkan/vulkan_provider.cpp:25`) [S].
  ROADMAP M5 lists "GPU selection".
- Output filter: `present_effect`, one of bilinear/cas/fsr/fsr2/fsr3 (SDK
  `src/ui/presenter.cpp:80-83`) [S].
- `present_letterbox` (SDK `presenter.cpp:37`) [S].
- **Language:** `user_language` (SDK `src/kernel/xam/xam_user.cpp:31`) [S]. The
  disc ships 7 `Coalesced.*` files (`assets/Game/Localization/`) [S], and
  ROADMAP v1.0 wants all 7 [S]. Language is **not** in the registry yet [S].
- `mnk_mode` (SDK `src/input/mnk/mnk_input_driver.cpp:26`) [S].
- `hid_mappings_file` (SDK `src/input/sdl/sdl_input_driver.cpp:23`) [S].
- Hidden plumbing the launcher must set but not show: `gpu_plugin` (SDK
  `rex_app.cpp:53`) and `render_target_path_vulkan` (SDK
  `src/graphics/vulkan/render_target_cache.cpp:40`) [S].

Portability caveat: the in-game page loads fonts from fixed
`C:/Windows/Fonts/impact.ttf` and `segoeuib.ttf` paths
(`src/settings/settings_page.cpp:307-312`) [S]. Under ADR-0006 those need
bundled fonts on Linux [I].

### 1.3 Assets, ISO extraction and verification

- **Extraction tool:** `scripts/inspect_disc.py` reads XDVDFS without mounting
  (four base offsets, `inspect_disc.py:13-35`) [S]. It extracts every file and
  hashes each one with SHA-256 (`inspect_disc.py:92-98,176-199`) [S].
- **Size:** the extracted tree is 5.9 GB: `$SystemUpdate`, `AvatarAssetPack`,
  `default.xex`, `Game/`, `nxeart` and `Splash/` [S].
- **SVR2011 first run:**
  - The launcher matches the disc against a shipped `disc-manifest.tsv`, copies
    into `gamedata.partial`, then marks `gamedata/.svr2011-verified`
    (`SVR:launcher/portable/Launcher.cs:27-45,136-140`) [S].
  - The player README walks through Browse → "Set up game files" → Play
    (`SVR:launcher/portable/README.txt:24-33`) [S].
  - Its CLI checks (`--verify-disc`, `--extract`, `--key-art`,
    `--print-arguments`) make the launcher testable headless
    (`Launcher.cs:111-140`) [S].
- **What first run needs here [I]:**
  1. Pick the ISO.
  2. Detect XDVDFS.
  3. Hash `default.xex` against `521c2325…`.
  4. Verify every file against a manifest committed to the repo (paths and
     hashes only, no data).
  5. Extract to a staging folder, then rename.
  6. Write the setup marker.
  7. Optionally install TU/DLC STFS packages (ADR-0010).
  8. Convert shaders (ADR-0012).
- **Shader gap:**
  - Today shader conversion needs a runtime dump (`--wwe_shader_dump_dir`,
    `src/d3d_census.cpp:45`). It then writes `cache/shader-native/shader_cache.cpp`,
    which **the next build compiles in** (`scripts/convert-shaders.ps1:1-3,14`)
    [S].
  - A shipped launcher cannot rebuild the exe, so ADR-0012 needs a
    runtime-loaded shader pack first [I].
  - `docs/research/svr2011-pc-port.md` row 3 proposes exactly that pack [S].

### 1.4 Reusable UI already in the three codebases

- **SVR2011-Recomp:**
  - WPF launcher built with the in-box .NET Framework `csc.exe`, no SDK needed
    (`SVR:scripts/Build-PortableLauncher.ps1:1-15`) [S].
  - Full art direction in `SVR:DESIGN.md`: a dark fixed theme, key art filling
    the window, one red accent on the primary action, Bahnschrift, no glow or
    blur [S].
  - Player README with SmartScreen and legal text
    (`SVR:launcher/portable/README.txt:34-35,90-94`) [S].
  - Separate observer process so closing the launcher never kills the game
    (`SVR:launcher/README.md:37-41`) [S].
  - The settings model validates enums and versions
    (`SVR:launcher/portable/Settings.cs:12-66`) [S].
  - None of this is Linux-portable (WPF) [I].
- **This repo:** the in-game PC SETTINGS page is a tested pure state machine
  (`src/settings/page_model.h:1-5`) drawn with ImGui
  (`src/settings/settings_page.cpp`) [S].
- **ReXGlue:** a generic ImGui cvar editor (`SettingsDialog`, F6) that saves to
  config (SDK `include/rex/ui/overlay/settings_overlay.h:19-33`,
  `rex_app.cpp:416-422`) [S]. It is a developer overlay, not player UI [I].
- **wwe-svr2011-pc:** 5,204 lines of Win32 C with tabs for Play, Settings,
  Online, Install, DLC, Saves, Paint Tool, Movies and Android, plus self-update
  (`docs/research/svr2011-pc-port.md:347-353`) [S]. Its code may be reused with
  attribution (same doc §1) [S]. Avoid its LGPL `bink_decode.c` under ADR-0007
  (same doc L96-97) [S].

### 1.5 Toolchain constraints

- C++ is clang 21 with Ninja and CMake (`build.ps1:20-21`) [S]. Windows
  resources compile with `llvm-rc.exe`, which ships in `.tools/llvm-21.1.8/bin`
  [S].
- The licence is BSD-3, so no GPL code may be imported
  (`docs/adr/0007-license.md:11,15`) [S]. That rules out copying Zelda64Recomp,
  Unleashed (both GPL-3.0) and RPCS3 (GPL-2.0) code; ideas only [P][gh-api].
- Releases are built by hand, and CI builds only what needs no game data
  (`docs/adr/0014-release-policy.md:7`) [S].

## 2. Technology options

Scores are relative to this project's needs. Size and startup figures that are
not cited must be measured with a prototype [I].

| Option | Visual ceiling | Runtime and size | Windows integration | Shares C++ config | Linux/Deck | Licence | Notes |
|---|---|---|---|---|---|---|---|
| **Dear ImGui in the game exe** (SDL3+Vulkan, already linked) | Medium. "Not designed to create a user interface for games"; limited skinning [P][imgui-faq]. Unleashed shows that custom textures and fonts get far [P][ur] | Nothing extra; already in the 103 MB exe [S] | SDL window; icon from exe resource [S] | **Direct** (`registry.h`) | Same as the game | MIT [P][gh-api] | ISO setup must run before the runtime starts [I] |
| **RmlUi in the game exe** | High: HTML/CSS layout, animations and transitions, gradients, blur and drop-shadow filters, controller navigation [P][rmlui] | Small C++ library plus FreeType (Zelda64 also uses lunasvg) [P][z64] | as above | Direct | Same as the game | MIT [P][rmlui] | Zelda64Recomp's launcher and menus [P][z64] |
| **Win32 + Direct2D / WPF** | Medium–high | WPF needs .NET Framework (in box); Win32 is tiny | Native | No | **No** | — | SVR2011 today (WPF) and wwe-svr2011-pc (Win32 C) [S] |
| **WinUI 3** | High (Mica, custom title bar) [P][winui-title] | Windows App SDK runtime; "cannot produce a single-file EXE" [P][winappsdk-sc] | Best | No | **No** (ADR-0006) | MIT | Colour customisation is ignored on Windows 10 [P][winui-title] |
| **Qt 6 (QML)** | High | Several large DLLs [I] | Good | Yes (C++) | Yes | LGPLv3/GPL/commercial; Quick 3D and Timeline are GPL-only [P][qt-lic]; Quick is LGPLv3 [P][qt-quick] | LGPL relinking obligations for a BSD project [I] |
| **Electron** | Very high (full Chromium) | Embeds Chromium and Node.js [P][electron] | Custom title bar via `titleBarStyle`/`titleBarOverlay` [P][electron-tb] | No | Yes | MIT | Heaviest option [I] |
| **Tauri 2** (WebView2/WebKitGTK) | High (HTML/CSS) | Small binary [P][tauri-size]; WebView2 preinstalled on Win10 1803+ [P][tauri-prereq], Evergreen on Win11 [P][wv2-dist]. Fixed runtime adds over 250 MB [P][wv2-dist] | `decorations:false` plus `data-tauri-drag-region` [P][tauri-window] | No (Rust) | WebKitGTK 4.1 [P][tauri-prereq] | MIT/Apache | Built-in signed updater [P][tauri-upd] |
| **Ultralight** | High (HTML, GPU) | Small [I] | Embeds in own window | Yes (C++) | Yes | Proprietary; free tier < $100K revenue [P][ultralight] | Closed source clashes with an OSS release [I] |
| **Avalonia/.NET** | High: custom chrome [P][avalonia-window], Mica/Acrylic [P][avalonia-tl], XAML animations | Native AOT, no runtime install [P][avalonia-aot] | Good | No (export a schema) | Yes [P][avalonia-gh] | MIT [P][avalonia-gh] | **ROADMAP choice**; precedent in Xenia Manager (BSD-3) [P][xm-readme] |
| **Flutter Desktop** | High | exe + DLLs + `data/` + VC++ DLLs [P][flutter-win] | Runner is Win32 C++ [P][flutter-win] | No (Dart) | Yes [P][flutter-desk] | BSD | Custom title bar needs third-party packages [I] |
| **Slint** | Medium–high | Small native [I] | Good | Yes (C++ API) | Yes | GPLv3 / royalty-free with attribution / commercial [P][slint-lic] | Attribution badge in UI [P][slint-lic] |

Why Avalonia over Tauri here [I]:

- The owner's existing launcher logic is C#.
- Xenia Manager proves the domain in Avalonia.
- One rendering engine on Windows and Linux.
- No Rust toolchain added to a C++/PowerShell/Python repo.

Tauri would win if the owner wanted web-style motion and video backgrounds
written in CSS.

## 3. What polished launchers contain

First-party publisher launchers (Rockstar, EA app, Ubisoft Connect) are
closed-source store clients, so they are not surveyed here. The feature list
below comes from open-source per-game recomp and port launchers.

| Project | Stack | Features | Config hand-off |
|---|---|---|---|
| Zelda64Recomp | RmlUi in-process [P][z64] | Select ROM → Start game; Controls, Settings (general, graphics, sound, controls, mods, debug), Mods, Exit (`assets/launcher.rml` L38–61, `assets/config_menu/*.rml`); portable mode via `portable.txt` (README L135) [P][z64] | In-process |
| Unleashed Recompiled | ImGui in-process [P][ur] | Installer wizard: language, intro, game+TU, DLC, space check, install, success/fail (`installer_wizard.cpp` L128–138); integrity checks; reopen from the title screen for DLC (README L86–88); update check against the GitHub releases API (`install/update_checker.cpp` L18–19) [P][ur] | In-process |
| RPCS3 | Qt [P][gh-api] | Welcome dialog, settings dialog, updater that checks the SHA-256 of the download (`rpcs3qt/update_manager.cpp` L116, L578) [P][rpcs3] | Config files |
| Xenia Manager | Avalonia, .NET 10, framework-dependent (`XeniaManager.csproj`) [P][xm] | One-click setup, emulator auto-update, DLC/TU installer, patches, per-game profiles, settings UI that adapts to the config file, Steam shortcuts with artwork, save import/export (README L30–41) [P][xm-readme] | Writes the emulator's TOML |
| wwe-svr2011-pc | Win32 C | Play/Settings/Online/Install/DLC/Saves/Movies tabs, self-update, CLI capture mode [S] | Writes `svr2011.toml`, shared with in-game settings [S] |
| SVR2011-Recomp | WPF | Key-art home, settings panel, setup, logs, session folders [S] | Settings JSON → CLI arguments (`Launcher.cs:46-63`) [S] |

### Feature checklist

**MVP (M5, first public binaries):**

- [ ] Home screen: key art from the player's disc, the game logo, one primary
      **Play** button, status line.
- [ ] First-run setup:
  - pick the ISO;
  - verify XEX v5 and the full file manifest;
  - extract with progress and plain errors;
  - resume or restart on failure;
  - "not modified" guarantee as in Unleashed [P][ur].
- [ ] Settings: the `registry.h` schema (Display / Graphics / Audio /
      Controls) plus language and GPU selection.
- [ ] Config hand-off: write `wweallstars.settings.toml`. Pass only paths and
      hidden plumbing on the command line (§1.1 pinning).
- [ ] Logs: per-session folder and an "Open logs" button (SVR2011 pattern) [S].
- [ ] Shader preparation step (ADR-0012), once the runtime-pack gap is closed.
- [ ] Legal and about screen: not affiliated, no game data included,
      third-party licences.
- [ ] Headless CLI for tests (`--verify`, `--extract`, `--print-arguments`), as
      SVR2011 does [S].

**Polished (post-M5):**

- [ ] Custom chrome with Mica/Acrylic on Windows 11 [P][avalonia-tl], and
      motion such as Ken Burns on the key art [I].
- [ ] Update check, then self-update with a SHA-256 check (RPCS3 pattern)
      [P][rpcs3] or Velopack deltas [P][velopack].
- [ ] DLC/TU manager (ADR-0010) and a save backup/import (wwe-svr2011-pc) [S].
- [ ] News or changelog panel from GitHub release notes [I].
- [ ] Controller navigation and a Big Picture style mode (Xenia Manager has a
      `BigScreen` project) [P][xm].
- [ ] "Add to Steam" shortcut with artwork (Xenia Manager) [P][xm-readme].
- [ ] Achievements viewer (the SDK already writes unlock state,
      `userdata/vulkan/achievements/54510866.toml`) [S].
- [ ] Repair (re-verify) and uninstall-data actions [I].

## 4. Branding and legal notes

I am not a lawyer; this records practice, not advice.

- **Practice in comparable projects:**
  - Zelda64Recomp: "This repository and its releases do not contain game
    assets. The original game is required" (README L10) [P][z64].
  - Unleashed: "This project does not include any game assets" (README L9)
    [P][ur].
  - SVR2011's player README says "not affiliated with or endorsed by THQ,
    Yuke's, WWE or Microsoft" (`SVR:launcher/portable/README.txt:90-94`) [S].
  - SVR2011's DESIGN reads the art "at runtime from `gamedata/nxeart` on the
    player's own disc. The repository and the player zip never contain it"
    (`SVR:DESIGN.md:8-11`) [S].
- **Art available in our extracted assets,** to load at runtime only, never in
  Git (CLAUDE.md hard rule):

  | File | What | Size |
  |---|---|---|
  | `assets/nxeart` → `nxebg.jpg` | Dashboard background: Cena and Hogan with the WWE All Stars logo, as an STFS PIRS package. SVR2011's `KeyArt.Read` parses the same layout (`SVR:launcher/portable/KeyArt.cs:20-55`) [S] | 1920×1080 [S] |
  | `assets/nxeart` → `nxeslot.jpg` | Dashboard slot art, same subjects | 420×320 [S] |
  | `assets/nxeart` header at 0x571A, and the XEX XDBF title image | Title tile (split Cena/Hogan face); both copies are byte-identical (SHA-256 prefix `892e032676ca`) | 64×64 PNG [S] |
  | `assets/Splash/Splash.bmp`, `Game_Load.bmp` | Logo plus the full legal/credits screen (WWE, THQ, Epic, Scaleform, Bink, FMOD, Havok) | 1280×720 [S] |
  | `assets/Splash/LoadingIcons.bmp` | Loading spinner strip | 64×1024 [S] |
  | `assets/Game/Movies/ATTRACT_VIDEO.bik`, `BACKGROUND.bik`, `ARENA_00x.bik` | Bink 1 video, a candidate animated background | 6–152 MB [S] |

- **Bink video:**
  - Animated backgrounds from `.bik` need a Bink 1 decoder.
  - The SDK's FFmpeg copy has `bink.c` in its source tree, but the decoder
    list enables XMA only (SDK `thirdparty/ffmpeg-overlay/codec_list.c:14-15`,
    `thirdparty/CMakeLists.txt:407`) [S].
  - FFmpeg's Bink is LGPL, which the SVR port note says to avoid
    (`svr2011-pc-port.md:96-97`) [S].
  - So the MVP should use the static `nxebg.jpg` with code-driven motion [I].
- **The legal screen:** `Splash.bmp` carries the rights holders' own notices
  [S]. Showing it unmodified in an About screen, from the player's files, is a
  low-risk way to credit them [I].
- **Before the art loads** (first run, before setup) the launcher has no game
  art. Use an original typographic screen, as SVR2011 does
  (`SVR:DESIGN.md:11`) [S].

## 5. App icon (game exe and launcher)

### 5.1 Current state

- **This repo:**
  - No `.rc` and no `.ico` exist outside `.tools/`, `out/` and `assets/`.
  - `add_executable(wweallstars WIN32 ${WWEALLSTARS_SOURCES})` lists only
    `.cpp` files (`CMakeLists.txt:66-67`) [S].
  - So `wweallstars.exe` shows the generic Windows exe icon [S].
- **SVR2011-Recomp:**
  - No `.rc` either [S].
  - Its packaging builds the launcher icon by extracting `svr2011.exe`'s
    associated icon, which is the default one (`SVR:scripts/package-portable.ps1:68-74`)
    [S].
  - Its desktop shortcut points at the game exe's icon 0
    (`SVR:scripts/install-launcher.ps1:11`) [S].
- **ReXGlue:**
  - `Window::SetIcon(buffer,size)` takes `.ico` bytes (SDK
    `include/rex/ui/window.h:284-287`).
  - The base `LoadAndApplyIcon` is a no-op (`window.h:527-530`), and no
    platform window overrides it (grep of SDK `src/`, `include/`) [S].
  - On Windows, SDL3 registers its window class with `LoadIcon` from a hint or
    otherwise **the exe's first `RT_GROUP_ICON`** (SDK
    `thirdparty/sdl3/src/video/windows/SDL_windowsevents.c:2788-2798`). It
    leaves the small icon NULL so Windows picks a DPI-appropriate size
    (L2746-2748) [S].
  - **So embedding an icon resource is enough for Explorer, the taskbar,
    Alt+Tab and the title bar; no runtime code is needed** [S].
- **ReXGlue metadata trap:**
  - `rexglue init achievements` writes the XDBF title image to
    `metadata/icons/title.png` (SDK
    `src/rexglue/commands/init_command.cpp:518-523`) [S].
  - Our generated CMake **embeds `metadata/icons` into the exe if that folder
    exists** (`generated/rexglue.cmake:103-107`) [S].
  - Running that command would put game art into released binaries. Keep
    `metadata/` out of the build or out of Git [I].

### 5.2 Windows icon requirements

- **Sizes:**
  - "Apps should have, at the bare minimum: 16x16, 24x24, 32x32, 48x48, and
    256x256 … by providing a 256px icon, ensures Windows should only ever
    scale your icon down, never up" [P][ms-icon].
  - Windows 11 sizes by scale factor [P][ms-icon]:

    | Surface | 100% | 150% | 200% | 400% |
    |---|---|---|---|---|
    | Title bar, tray, context menu | 16 | 24 | 32 | 64 |
    | Taskbar, search, Start all apps | 24 | 36 | 48 | 96 |
    | Start pins | 32 | 48 | 64 | 256 |

  - Windows picks the exact size if present, else scales the next larger size
    down [P][ms-icon].
  - The older Win32 guide also lists 20/30/36/40 px variants: 40 for crisp
    Alt+Tab, and the 125%/150% steps [P][ms-vis-icons].
- **PNG compression:** "only the 256x256 pixel image should be compressed",
  meaning PNG inside the ICO, to keep the file small [P][ms-vis-icons].
- **Embedding:** use an `ICON` statement in a `.rc` (`nameID ICON filename`)
  [P][ms-rc-icon]. Precedents:
  - Zelda64Recomp `icons/app.rc`: six `ICON` entries from 512 down to 16,
    added with `target_sources(... app.rc)` (`CMakeLists.txt` L293) [P][z64].
  - Unleashed `res/win32/res.rc.template`: `IDI_ICON1 ICON` plus
    `VERSIONINFO`, configured into the build (`CMakeLists.txt` L285-289)
    [P][ur].
  - Here, `llvm-rc` from `.tools/llvm-21.1.8/bin` compiles the `.rc` [S].
  - Add a `VERSIONINFO` block in the same file so Explorer shows the product
    name and version (Unleashed pattern) [P][ur].
- **Runtime icon change:** `WM_SETICON` sets the big icon (Alt+Tab) and the
  small icon (title bar) [P][ms-seticon]. That is only needed for an icon
  generated after build (5.3, option C).
- **Signing:** editing resources in a signed exe after install breaks the
  signature ("Do not modify signed files") [P][ms-smartscreen-rep].

### 5.3 Branding options for the icon

The only official icon-sized art is the **64×64** title tile. It sits in the
XEX's XDBF block (file offset `0x1820000` of `analysis/xex/default.pe`, image
section, title ID; SDK `src/system/util/xdbf_utils.cpp:114-115`) and in the
`nxeart` STFS header at `0x571A` [S]. Upscaling 64 → 256 breaks Microsoft's
"never up" guidance [P][ms-icon] and would look soft [I].

| Option | How | Pros | Cons |
|---|---|---|---|
| **A. Original fan-made icon, committed** (BSD) | Commissioned or drawn mark, for example a star/ring motif in the All Stars palette, no WWE logo or likeness; full ICO set | Legally clean; crisp at 256; works before setup; can be signed | Not "official-looking" [I] |
| B. Built from the developer's disc at build time | wwe-svr2011-pc `tools/make_icon.py` reads `nxeart` 0x571A, resizes to 16–256 and the build embeds it only if the file exists (`pc:port/src/svr2011.rc` L1-10, `pc:port/tools/make_icon.py` L1-40) | Exact game tile | Released binaries then **contain game art**, which conflicts with this repo's "no game data" rule [S]; 4× upscale |
| C. Built from the player's disc at setup | Launcher writes `userdata/icon.ico` from the tile or a crop of `nxebg.jpg`. Points the shortcut's `IconLocation` at it (SVR2011 already sets `IconLocation`, `SVR:scripts/install-launcher.ps1:11`). Game sets it with `WM_SETICON` | Official look, nothing shipped | Exe in Explorer still shows A or the generic icon; needs runtime code (ReXGlue's `SetIcon` is a no-op today) [S] |

Recommendation [I]:

- Ship **A** as the embedded icon of both exes, with one shared `.ico` and
  `VERSIONINFO`.
- Optionally layer **C** on shortcuts after setup, if the owner wants the game
  tile.
- What others ship:
  - Zelda64Recomp ships a stylised redraw of the Majora's Mask motif
    (`icons/512.png`) [P][z64].
  - Unleashed ships a Sonic close-up `game_icon.ico` in its separate
    `UnleashedRecompResources` repo, which reports no licence [P][ur-res].
  - wwe-svr2011-pc uses option B [P][pc].

## 6. Config hand-off design [I]

- The launcher writes **only** `wweallstars.settings.toml`, with the same keys
  and the same atomic write as `config_file.h:104-132` [S]. Then the in-game
  page and the launcher always agree.
- The launcher passes `--game_data_root`, `--user_data_root`, `--cache_root`,
  `--log_file` and plumbing flags as arguments, never player settings. Player
  settings would show as pinned in game (`registry.h:59`) [S].
- For C# to stay in step with `registry.h`, add a headless
  `wweallstars.exe --print-settings-schema` that dumps `PlayerSettings()` as
  JSON. The launcher builds its settings tabs from that dump, as Xenia
  Manager's "Dynamic Xenia Settings UI" does [P][xm-readme]. SVR2011 instead
  keeps two copies in step with a test (`SVR:launcher/portable/Settings.cs:1-2`)
  [S].
- Portable vs per-user data: the SDK default is the user folder
  (`rex_app.cpp:118-125`) [S]. SVR2011 is portable (folder next to the exe)
  [S], and Zelda64 supports both via `portable.txt` [P][z64]. This is an owner
  decision.

## 7. Packaging and distribution

- **Shape:**
  - The SVR2011 precedent is a portable zip: launcher exe plus `game\`
    folder, `gamedata\`, `userdata\`, `cache\`, `logs\`
    (`SVR:launcher/portable/README.txt:80-87`) [S]. No installer, no UAC.
  - Velopack (MIT [P][gh-api]) adds an installer, self-update and delta
    packages, with SDKs for C#, C++ and Rust, on Windows, macOS and Linux
    [P][velopack].
  - Tauri's updater requires signed update manifests, and that "cannot be
    disabled" [P][tauri-upd].
- **SmartScreen:**
  - Unsigned files get "Windows protected your PC" and need "Run anyway".
  - "EV certificates no longer bypass SmartScreen". Reputation builds over
    "several weeks and hundreds of clean installs".
  - Unsigned files restart reputation with every version.
  - On Windows 11, Smart App Control "will block execution of unsigned files
    unless the file has a positive reputation".

  Source for all four points: [P][ms-smartscreen-rep].
- **Signing options:**
  - Microsoft Artifact Signing (formerly Trusted Signing) costs from
    $9.99/month [P][ms-smartscreen-rep]. Public Trust for **individuals is US
    and Canada only**; organisations can also be in the EU and other listed
    countries [P][artifact-quickstart].
  - SignPath Foundation is free for OSS. It requires an OSI licence for all
    components, no proprietary parts, and binaries that are "a valid,
    automated build" of the repo [P][signpath-terms].
  - The game exe cannot be built in CI, because codegen needs the XEX
    (ADR-0014) [S]. So at most the launcher could qualify [I].
- **Auto-update hand-off:** update the launcher and game together from GitHub
  Releases (`/releases/latest`, as Unleashed checks [P][ur]). Verify a SHA-256
  as RPCS3 does [P][rpcs3]. Keep `userdata/` and `gamedata/` untouched, as
  wwe-svr2011-pc's updater does [S].
- **Steam Deck:**
  - wwe-svr2011-pc runs its launcher under Proton for setup. In Game Mode it
    starts the game exe directly (pc README L143-145) [P][pc].
  - The launcher must therefore never be required to start the game [I].

## 8. Open questions for the owner

**Decided by the owner, 2026-10-04 (questions 1–4):**
Avalonia hybrid (separate Avalonia launcher, in-game F4 page stays);
All Stars visual style with disc art read at runtime; original fan-made icon
in both exes plus the game tile on shortcuts after setup; portable zip.

**Decided by the owner, 2026-10-04 (questions 5–9):**
unsigned v1 with README SmartScreen guidance; converted shaders move to a
runtime pack built by the launcher at setup (ADR-0012); the launcher shows
startup-time settings only, and `user_language` and `vulkan_device` join the
registry; v1 has an update check against GitHub Releases (link only, no
self-update, no news panel); the game exe started without setup shows "run
the launcher once" and exits.

1. **Stack:** confirm Avalonia (ROADMAP) or switch to an in-process
   RmlUi/ImGui front end, or use the hybrid in §Summary. Record the choice as
   an ADR.
2. **Look:** reuse SVR2011's art direction (dark arena, key art, one accent),
   or something closer to the in-game All Stars style (blue/gold burst)?
   Static art with motion, or invest in a Bink decoder for video backgrounds?
3. **Icon:** an original fan-made icon (A), the game tile on shortcuts only
   (C), or both? Is there a designer, or should a first draft be generated
   programmatically?
4. **Distribution:** portable zip (as SVR2011), Velopack installer, or both?
   Per-user or portable data folder?
5. **Signing:** where is the signer legally based? Artifact Signing for
   individuals is US/Canada only. Is "unsigned plus README guidance"
   acceptable for v1?
6. **Shader pack:** accept moving converted shaders from a compiled-in `.cpp`
   to a runtime pack, so the launcher can run XenosRecomp at setup as
   ADR-0012 assumes?
7. **Settings ownership:** should the launcher show every registry setting,
   or only startup-time ones (renderer, scale, display), leaving live ones to
   the F4 page? Add `user_language` and `vulkan_device` to the registry?
8. **Updates and news:** self-update in v1? A news panel fed from GitHub
   release notes?
9. **Game Mode:** should `wweallstars.exe` run first-time setup itself when
   started without the launcher (Deck Game Mode), or just show "run the
   launcher once"?

## Sources

- [avalonia-gh]: https://github.com/AvaloniaUI/Avalonia (readme L9; licence MIT via `gh api repos/AvaloniaUI/Avalonia`)
- [avalonia-window]: https://docs.avaloniaui.net/docs/reference/controls/window
- [avalonia-tl]: https://api-docs.avaloniaui.net/docs/P_Avalonia_Controls_TopLevel_TransparencyLevelHint and https://api-docs.avaloniaui.net/docs/T_Avalonia_Controls_WindowTransparencyLevel
- [avalonia-aot]: https://docs.avaloniaui.net/docs/deployment/native-aot
- [xm-readme]: https://github.com/xenia-manager/xenia-manager/blob/main/README.md (Main Features L30–41)
- [xm]: https://github.com/xenia-manager/xenia-manager (`source/XeniaManager/XeniaManager.csproj`, `source/XeniaManager.BigScreen/`; BSD-3-Clause)
- [rmlui]: https://github.com/mikke89/RmlUi (README; MIT)
- [imgui-faq]: https://github.com/ocornut/imgui/blob/master/docs/FAQ.md
- [z64]: https://github.com/Zelda64Recomp/Zelda64Recomp (branch `dev`: `assets/launcher.rml`, `assets/config_menu/`, `icons/app.rc`, `icons/512.png`, `CMakeLists.txt` L293, README L10, L51, L57, L135, L145–147; GPL-3.0)
- [ur]: https://github.com/hedge-dev/UnleashedRecomp (`UnleashedRecomp/ui/installer_wizard.cpp` L128–138, `install/update_checker.cpp` L18–19, `res/win32/res.rc.template`, `UnleashedRecomp/CMakeLists.txt` L285–289, README L9, L75–88; GPL-3.0)
- [ur-res]: https://github.com/hedge-dev/UnleashedRecompResources (`images/game_icon.ico`, `game_icon.png`; no licence reported by the GitHub API)
- [rpcs3]: https://github.com/RPCS3/rpcs3/blob/master/rpcs3/rpcs3qt/update_manager.cpp (L116, L249, L578)
- [pc]: https://github.com/KaikoClanworth1/wwe-svr2011-pc (`port/src/svr2011.rc`, `port/launcher/launcher.rc`, `port/tools/make_icon.py`, README L130, L143–145)
- [gh-api]: licence fields from `gh api repos/<owner>/<repo>` on 2026-10-04 (Zelda64Recomp GPL-3.0, UnleashedRecomp GPL-3.0, rpcs3 GPL-2.0, xenia-manager BSD-3-Clause, Xbox360Toolkit GPL-3.0, imgui MIT, RmlUi MIT, velopack MIT, SDL Zlib)
- [electron]: https://www.electronjs.org/docs/latest/
- [electron-tb]: https://www.electronjs.org/docs/latest/tutorial/custom-title-bar
- [tauri-window]: https://v2.tauri.app/learn/window-customization/
- [tauri-size]: https://v2.tauri.app/concept/size/
- [tauri-upd]: https://v2.tauri.app/plugin/updater/
- [tauri-prereq]: https://v2.tauri.app/start/prerequisites/
- [tauri-wininst]: https://v2.tauri.app/distribute/windows-installer/ (WebView2 install modes: download/embed bootstrapper, offline installer ~127 MB, fixed version ~180 MB)
- [wv2-dist]: https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution
- [qt-lic]: https://doc.qt.io/qt-6/licensing.html
- [qt-quick]: https://doc.qt.io/qt-6/qtquick-index.html (Licenses and Attributions)
- [ultralight]: https://ultralig.ht/pricing/
- [slint-lic]: https://github.com/slint-ui/slint/blob/master/LICENSE.md
- [flutter-desk]: https://docs.flutter.dev/platform-integration/desktop
- [flutter-win]: https://docs.flutter.dev/platform-integration/windows/building
- [winui-title]: https://learn.microsoft.com/en-us/windows/apps/develop/title-bar
- [winappsdk-sc]: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps
- [velopack]: https://github.com/velopack/velopack (README "Features"), https://docs.velopack.io/
- [ms-smartscreen]: https://learn.microsoft.com/en-us/windows/security/operating-system-security/virus-and-threat-protection/microsoft-defender-smartscreen/
- [ms-smartscreen-rep]: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- [artifact-quickstart]: https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart (Prerequisites note)
- [signpath-terms]: https://signpath.org/terms
- [ms-icon]: https://learn.microsoft.com/en-us/windows/apps/design/iconography/app-icon-construction ("Icon scaling")
- [ms-vis-icons]: https://learn.microsoft.com/en-us/windows/win32/uxguide/vis-icons ("Color and saturation", "Size requirements")
- [ms-rc-icon]: https://learn.microsoft.com/en-us/windows/win32/menurc/icon-resource
- [ms-seticon]: https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-seticon
- [S] local: this repo (`CMakeLists.txt`, `CMakePresets.json`, `scripts/build.ps1`, `scripts/smoke.ps1`, `scripts/session.ps1`, `scripts/inspect_disc.py`, `scripts/convert-shaders.ps1`, `src/main.cpp`, `src/wweallstars_app.h`, `src/settings_cvars.cpp`, `src/settings/*.h|cpp`, `src/d3d_census.cpp`, `generated/rexglue.cmake`, `docs/ROADMAP.md`, `docs/adr/0006,0007,0010,0012,0014`, `docs/research/svr2011-pc-port.md`, extracted `assets/`, `analysis/xex/default.pe`); `D:\Projects\SVR2011-RECOMP` (`launcher/`, `DESIGN.md`, `PRODUCT.md`, `scripts/Build-PortableLauncher.ps1`, `scripts/package-portable.ps1`, `scripts/install-launcher.ps1`); `.tools/rexglue-source` (`src/ui/rex_app.cpp`, `src/ui/window.cpp`, `include/rex/ui/window.h`, `src/system/runtime.cpp`, `src/rexglue/commands/init_command.cpp`, `src/system/util/xdbf_utils.cpp`, `thirdparty/sdl3/.../SDL_windowsevents.c`, `thirdparty/ffmpeg-overlay/codec_list.c`, cvar definitions cited inline).
