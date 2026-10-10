// The in-game PC SETTINGS page: F4 opens it over the game; the controller
// drives it while the game reads an idle pad.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rex::input {
class InputSystem;
}
namespace rex::ui {
class ImGuiDrawer;
}

struct ImFontAtlas;

namespace wwe::settings {

// Adds the page's fonts (Impact for headings, Segoe UI Bold for help text,
// when Windows has them). Call from OnConfigureFonts.
void AddFonts(ImFontAtlas* atlas);

// Loads the page's settings file (wweallstars.settings.toml in user_data_root)
// over the main config and logs the main config's parse error, which the SDK cannot log
// itself (it loads before logging starts). Call once logging is up and before
// anything reads the settings (OnPostInitLogging).
void LoadSettingsFile(const std::filesystem::path& config_path,
                      const std::filesystem::path& user_data_root);

// What the GPU and language rows offer on this PC and disc (registry.h
// WithRuntimeChoices).
struct RuntimeChoiceSources {
  std::vector<std::string> gpus;          // in vulkan_device order
  std::filesystem::path game_data_root;  // where the language files are looked for
  std::string suggested_preset;          // for the GPU in use, named in the preset's help
};

// Registers the F4 bind, the controller combo (both sticks clicked) and the
// guest input hold. Changes are saved to the settings file in user_data_root. run_on_ui runs a
// function on the UI thread (the combo is seen on a guest thread). Call once
// the ImGui drawer and the input system exist.
void Configure(rex::ui::ImGuiDrawer* drawer, rex::input::InputSystem* input,
               const std::filesystem::path& user_data_root,
               std::function<void(std::function<void()>)> run_on_ui,
               RuntimeChoiceSources sources);

// A game started without the launcher's setup: on its first start (no
// Graphics preset member saved or in the launch options, registry.h
// IsFirstSetup) sets and saves the preset suggested for the GPU it opened,
// before Native reads the settings. Never in scene runs. Call after
// LoadSettingsFile, once the Vulkan device exists.
void ApplySuggestedPresetOnFirstStart(const std::string& suggested);

// Opens the page from any thread (the game's OPTIONS row is selected on a
// guest thread). Holds the game's input from now until the page has closed and
// the pad is released. Does nothing where the page is off (scene runs).
void RequestOpen();

}  // namespace wwe::settings
