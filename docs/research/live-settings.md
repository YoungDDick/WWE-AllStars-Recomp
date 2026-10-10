# Display settings without a restart

Date 2026-10-10. The owner asked why changing VSync needs a game restart, and
which display settings could apply live. ReXGlue paths (`SDK/`) are relative to
`.tools/rexglue-source` (`f5337cdc` plus `patches/`); other paths are relative
to the repo root. Every claim was read in source. Work items:
`.scratch/live-settings/`.

Status 2026-10-10: built. VSync, Lock to display and Texture filtering now apply
live (tickets 01-03); the launcher says which rows also change in game (04).

## Answer

VSync needs a restart because of how the cvar is flagged. It is not a hard
limit. `present_vsync` is `kRequiresRestart`
(`SDK/src/ui/vulkan/vulkan_presenter.cpp:54-57`). For such a row the in-game
page saves the value and marks it pending, but never sets the cvar
(`src/settings/settings_page.cpp:122-137`). The swapchain reads the cvar only
in `CreateSwapchainForVulkanSurface` (`vulkan_presenter.cpp:1400-1425`).

The SDK can already rebuild the swapchain at runtime.
`Presenter::OnSurfaceResizeFromUIThread()` (`SDK/src/ui/presenter.cpp:435-461`)
goes through `UpdateSurfacePaintConnectionFromUIThread` (`presenter.cpp:1702-1768`)
into `ConnectOrReconnectPaintingToSurfaceFromUIThread`. That path:

- retires the old swapchain, creates a new one with `oldSwapchain`, and
  destroys the old one (`vulkan_presenter.cpp:775-793`);
- re-reads `present_vsync` (`:1400`) and picks present-wait again (`:1452`);
- recomputes the paint mode from the FIFO flag it reports (`:1053`,
  `presenter.cpp:1745`, `:1764-1766`).

The pacing waiter is detached and re-attached for each swapchain
(`vulkan_presenter.cpp:1514-1516`, `presenter.cpp:845-863`).
`patches/rexglue-window-restore.patch:9-13` already calls this path when the
size has not changed. Live Sharpening is the same pattern in practice: a
`kHotReload` cvar plus a change callback (`src/native/native_renderer.cpp:1343-1375`,
`patches/rexglue-live-sharpening.patch`).

**Threading catch.** `OnSurfaceResizeFromUIThread` asserts that no UI drawer is
running (`presenter.cpp:438`). The page applies changes from inside its ImGui
drawer (`settings_page.cpp:172-177`), and `SetFlagByName` runs change callbacks
synchronously (`SDK/src/core/cvar.cpp:329-335`). The callback therefore has to
post the rebuild with `CallInUIThread` (`run_on_ui`,
`src/wweallstars_app.h:138-140`). The wait for in-flight GPU work
(`vulkan_presenter.cpp:1518`) costs about one frame of hitch when the setting
is switched.

## How a setting gets to the game

- **One list:** `PlayerSettings()` in `src/settings/registry.h:57-174`.
- **Restart mark:** derived from the cvar lifecycle. Any lifecycle other than
  `kHotReload`, or `read_at_boot`, shows "restart" (`registry.h:228-237`). The
  JSON export writes it as `next_start` (`src/settings/schema.h:142`).
- **Launcher:** shows only rows marked `applies_at_startup`
  (`launcher/src/Launcher.Core/SettingsSchema.cs:73`) and writes only those keys
  (`SettingsFile.cs:221-227`). VSync, Lock to display and Sharpening are not in
  the launcher.
- **Settings file:** `userdata/wweallstars.settings.toml`, read once at boot
  (`src/wweallstars_app.h:95-97` → `settings_page.cpp:329-343`).
- **Launcher to game:** no IPC and no file watching. The command line carries
  only paths (`LaunchArguments.cs:13-23`).
- **In game:** the settings page is on F4 or both sticks clicked
  (`settings_page.cpp:412`). The SDK cvar overlay is on F6 and shows
  `[live]`/`[restart]` badges (`SDK/src/ui/overlay/settings_overlay.cpp:32-42`).

## Per-setting table

| Setting | Defined | Consumed | Today | Path to live |
|---|---|---|---|---|
| VSync `present_vsync` | `SDK/src/ui/vulkan/vulkan_presenter.cpp:54-57` | swapchain creation `:1400-1425` | restart (`tests/core/test_setting_lifecycles.cpp:31`) | `kHotReload` + callback that posts `OnSurfaceResizeFromUIThread` |
| Display mode `fullscreen` | `SDK/src/ui/window.cpp:34-37` | `SDK/src/ui/rex_app.cpp:355-362` | **live**; the launcher still says "applied when the game starts" (`MainWindow.axaml:231`) | wording only |
| Lock to display `vblank_follows_display` | `SDK/src/graphics/graphics_system.cpp:41-45` | thread starts only if on at init (`:238-241`); value copied to a `const` (`:285`) | restart | always start the thread and read the cvar on each loop |
| Texture filtering `wwe_native_anisotropy` | `src/settings_cvars.cpp:27-31` | sampler creation `src/native/textures.cpp:325`; samplers cached | restart | callback marks the sampler cache dirty; rebuild at a frame boundary |
| Sharpening `wwe_native_cas` | `src/settings_cvars.cpp:16-21` | `native_renderer.cpp:1318-1375` | **live** | done; template |
| Render resolution / supersampling | `settings_cvars.cpp:12-15`, `:22-26` | render target sizes, `textures::Initialize(g.scale)` (`native_renderer.cpp:1410-1421`) | restart | every render target and texture recreated; out of scope |
| Renderer `wwe_native_renderer` | `settings_cvars.cpp:8-11` | `native_renderer.cpp:1369` | restart | not realistic |
| GPU `vulkan_device` | `SDK/src/ui/vulkan/vulkan_provider.cpp:25-26` (`kInitOnly`) | device creation | restart | not realistic |
| `present_effect`, `present_dither` (not for players) | `SDK/src/ui/presenter.cpp:50-53`, `:86-88` | `presenter.cpp:298-317` | restart | `SetGuestOutputPaintConfigFromUIThread` (`:996-1058`) if ever exposed |

There is no player FPS cap. The SDK `vsync` cvar
(`SDK/src/graphics/command_processor.cpp:39`) drives the guest vblank timer, not
display sync, and players never see it.

## Docs that disagree with the code

- `.scratch/launcher/spec.md:52-55` and story 41 (`:124`) call VSync live.
  `.scratch/in-game-settings/spec.md:52-53` says that most changes apply
  immediately.
- `.scratch/smooth-presentation/issues/09-present-mode-setting.md:17-18`
  accepted "or the menu says a restart is needed", and chose it.
- `.scratch/in-game-settings/issues/01-settings-declare-when-they-apply.md:23-24`
  made anything read at swapchain or sampler build time restart-only.
- The schema export shows `vblank_follows_display` with an empty type and
  `next_start:false`, because the GPU plugin is not loaded at export time
  (`src/settings/schema.h:120-123`).

## SVR2011-Recomp

SVR2011-Recomp has no runtime path to borrow. Its launcher passes every display
setting on the command line (`D:\Projects\SVR2011-RECOMP\launcher\portable\Settings.cs:108-135`).
VSync goes through `vulkan_allow_present_mode_*` (`:122-124`). It has no
in-game settings page and no change callbacks, so every change needs a restart.
