#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "startup_paths.h"

namespace fs = std::filesystem;
using wwe::startup::CheckSetup;
using wwe::startup::Folders;
using wwe::startup::ResolveStartup;
using wwe::startup::PlumbingToSet;
using wwe::startup::SessionLogPath;
using wwe::startup::SetupStatus;
using wwe::startup::Startup;

namespace {

// A fresh, empty folder under the temp folder, removed at the end of the test.
struct TempFolder {
  TempFolder() {
    std::random_device random;
    path = fs::temp_directory_path() / ("wwe-startup-" + std::to_string(random()));
    fs::create_directories(path);
  }
  ~TempFolder() {
    std::error_code error;
    fs::remove_all(path, error);
  }
  fs::path path;
};

void WriteFile(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

constexpr const char* kManifest = "54510866-60cef505-00000005";

}  // namespace

TEST_CASE("a game data folder with the launcher's marker for this manifest is set up") {
  TempFolder gamedata;
  WriteFile(gamedata.path / ".wweallstars-setup", std::string("manifest_version=") + kManifest + "\n");
  CHECK(CheckSetup(gamedata.path, kManifest) == SetupStatus::kDone);
}

TEST_CASE("a game data folder without the marker is not set up") {
  TempFolder gamedata;
  WriteFile(gamedata.path / "default.xex", "xex");
  CHECK(CheckSetup(gamedata.path, kManifest) == SetupStatus::kMissing);
  CHECK(CheckSetup(gamedata.path / "absent", kManifest) == SetupStatus::kMissing);
}

TEST_CASE("a marker for another disc manifest does not count as set up") {
  TempFolder gamedata;
  WriteFile(gamedata.path / ".wweallstars-setup", "manifest_version=54510866-60cef505-00000004\n");
  CHECK(CheckSetup(gamedata.path, kManifest) == SetupStatus::kWrongManifest);
  WriteFile(gamedata.path / ".wweallstars-setup", "something else\n");
  CHECK(CheckSetup(gamedata.path, kManifest) == SetupStatus::kWrongManifest);
}

TEST_CASE("a marker written with Windows line ends still counts") {
  TempFolder gamedata;
  WriteFile(gamedata.path / ".wweallstars-setup", std::string("manifest_version=") + kManifest + "\r\n");
  CHECK(CheckSetup(gamedata.path, kManifest) == SetupStatus::kDone);
}

TEST_CASE("folders given on the command line win and are not checked for the marker") {
  // Developer scripts (session.ps1, smoke.ps1) start the game on assets/, which has no marker.
  TempFolder root;
  WriteFile(root.path / "gamedata" / ".wweallstars-setup", std::string("manifest_version=") + kManifest);
  const Folders given{root.path / "assets", root.path / "userdata/vulkan", root.path / "cache/vulkan"};
  const Startup startup = ResolveStartup(given, root.path / "game", kManifest);
  CHECK_FALSE(startup.portable);
  CHECK_FALSE(startup.needs_setup);
  CHECK(startup.folders.game_data_root == root.path / "assets");
  CHECK(startup.folders.user_data_root == root.path / "userdata/vulkan");
  CHECK(startup.folders.cache_root == root.path / "cache/vulkan");
}

TEST_CASE("a game data folder on the command line keeps the SDK's other defaults") {
  TempFolder root;
  const Startup startup = ResolveStartup({root.path / "assets", {}, {}}, root.path / "game", kManifest);
  CHECK_FALSE(startup.portable);
  CHECK(startup.folders.user_data_root.empty());
  CHECK(startup.folders.cache_root.empty());
}

TEST_CASE("started with no folders after setup, the game uses the portable layout") {
  TempFolder root;
  WriteFile(root.path / "gamedata" / ".wweallstars-setup", std::string("manifest_version=") + kManifest);
  const Startup startup = ResolveStartup({}, root.path / "game", kManifest);
  CHECK(startup.portable);
  CHECK_FALSE(startup.needs_setup);
  CHECK(startup.folders.game_data_root == root.path / "gamedata");
  CHECK(startup.folders.user_data_root == root.path / "userdata");
  CHECK(startup.folders.cache_root == root.path / "cache");
  CHECK(startup.logs_folder == root.path / "logs");
}

TEST_CASE("a game data folder on the command line leaves the log to the command line") {
  // The launcher and the developer scripts pass --log_file themselves.
  TempFolder root;
  CHECK(ResolveStartup({root.path / "assets", {}, {}}, root.path / "game", kManifest)
            .logs_folder.empty());
}

TEST_CASE("a direct start's session log is named the way the launcher names its own") {
  // PortableLayout.NewSessionLogPath: logs/<yyyyMMdd-HHmmss>[-n]/runtime.log, so the
  // launcher's Open logs and exit report find it.
  TempFolder logs;
  CHECK(SessionLogPath(logs.path, "20261004-153000") ==
        logs.path / "20261004-153000" / "runtime.log");
  fs::create_directories(logs.path / "20261004-153000");
  CHECK(SessionLogPath(logs.path, "20261004-153000") ==
        logs.path / "20261004-153000-2" / "runtime.log");
  fs::create_directories(logs.path / "20261004-153000-2");
  CHECK(SessionLogPath(logs.path, "20261004-153000") ==
        logs.path / "20261004-153000-3" / "runtime.log");
}

TEST_CASE("started with no folders before setup, the game asks for the launcher") {
  TempFolder root;
  CHECK(ResolveStartup({}, root.path / "game", kManifest).needs_setup);
  WriteFile(root.path / "gamedata" / "default.xex", "xex");
  CHECK(ResolveStartup({}, root.path / "game", kManifest).needs_setup);
  WriteFile(root.path / "gamedata" / ".wweallstars-setup", "manifest_version=other");
  CHECK(ResolveStartup({}, root.path / "game", kManifest).needs_setup);
}

TEST_CASE("the portable layout uses the game data folder chosen at setup") {
  TempFolder root;
  // "Hráč data": the launcher's System.Text.Json escapes backslashes and non-ASCII.
  const fs::path chosen = root.path / "elsewhere" / fs::path(u8"Hráč data");
  WriteFile(chosen / ".wweallstars-setup", std::string("manifest_version=") + kManifest);
  std::string json;
  for (char c : (root.path / "elsewhere").make_preferred().string()) {
    json += c == '\\' ? std::string("\\\\") : std::string(1, c);
  }
  json += fs::path::preferred_separator == '\\' ? "\\\\" : "/";
  json += "Hr\\u00E1\\u010D data";
  WriteFile(root.path / "userdata" / "launcher-state.json",
            "{\n  \"gameDataDirectory\": \"" + json + "\"\n}");
  const Startup startup = ResolveStartup({}, root.path / "game", kManifest);
  CHECK(startup.portable);
  CHECK_FALSE(startup.needs_setup);
  CHECK(startup.folders.game_data_root == chosen);
  CHECK(startup.folders.user_data_root == root.path / "userdata");
}

TEST_CASE("an unreadable launcher state falls back to the default game data folder") {
  TempFolder root;
  WriteFile(root.path / "gamedata" / ".wweallstars-setup", std::string("manifest_version=") + kManifest);
  WriteFile(root.path / "userdata" / "launcher-state.json", "{ not json");
  CHECK(ResolveStartup({}, root.path / "game", kManifest).folders.game_data_root ==
        root.path / "gamedata");
  WriteFile(root.path / "userdata" / "launcher-state.json", "{\"gameDataDirectory\": null}");
  CHECK(ResolveStartup({}, root.path / "game", kManifest).folders.game_data_root ==
        root.path / "gamedata");
}

TEST_CASE("user data and cache folders on the command line still win in the portable layout") {
  TempFolder root;
  WriteFile(root.path / "gamedata" / ".wweallstars-setup", std::string("manifest_version=") + kManifest);
  const Startup startup =
      ResolveStartup({{}, root.path / "saves", root.path / "shaders"}, root.path / "game", kManifest);
  CHECK(startup.portable);
  CHECK(startup.folders.game_data_root == root.path / "gamedata");
  CHECK(startup.folders.user_data_root == root.path / "saves");
  CHECK(startup.folders.cache_root == root.path / "shaders");
}

TEST_CASE("the portable layout mounts Title Update 2 from updatedata next to gamedata") {
  TempFolder root;
  WriteFile(root.path / "gamedata" / ".wweallstars-setup", std::string("manifest_version=") + kManifest);
  CHECK(ResolveStartup({}, root.path / "game", kManifest).folders.update_data_root ==
        root.path / "updatedata");
  // Next to a game data folder chosen at setup, as the launcher puts it.
  WriteFile(root.path / "userdata" / "launcher-state.json",
            "{\"gameDataDirectory\": \"" + (root.path / "D" / "WWE" / "gamedata").generic_string() + "\"}");
  WriteFile(root.path / "D" / "WWE" / "gamedata" / ".wweallstars-setup",
            std::string("manifest_version=") + kManifest);
  CHECK(ResolveStartup({}, root.path / "game", kManifest).folders.update_data_root ==
        root.path / "D" / "WWE" / "updatedata");
  // A remembered path ending in a separator still puts it next to, not inside, gamedata.
  WriteFile(root.path / "userdata" / "launcher-state.json",
            "{\"gameDataDirectory\": \"" + (root.path / "D" / "WWE" / "gamedata").generic_string() + "/\"}");
  CHECK(ResolveStartup({}, root.path / "game", kManifest).folders.update_data_root ==
        root.path / "D" / "WWE" / "updatedata");
  // A folder on the command line still wins.
  CHECK(ResolveStartup({.update_data_root = root.path / "tu2"}, root.path / "game", kManifest)
            .folders.update_data_root == root.path / "tu2");
}

TEST_CASE("an update data folder on the command line is passed through as given") {
  TempFolder root;
  CHECK(ResolveStartup({.game_data_root = root.path / "assets", .update_data_root = root.path / "updatedata"},
                       root.path / "game", kManifest)
            .folders.update_data_root == root.path / "updatedata");
  CHECK(ResolveStartup({root.path / "assets", {}, {}}, root.path / "game", kManifest)
            .folders.update_data_root.empty());
}

TEST_CASE("a portable start loads the GPU backend the launcher would pass") {
  // Launcher ticket 25: the release ships no wweallstars.toml, so without these a
  // direct start runs with no GPU emulation (LaunchArguments.cs passes the same).
  TempFolder root;
  WriteFile(root.path / "gamedata" / ".wweallstars-setup", std::string("manifest_version=") + kManifest);
  const Startup startup = ResolveStartup({}, root.path / "game", kManifest);
  const auto none = [](std::string_view) { return std::string(); };
  const auto flags = PlumbingToSet(startup, none);
  REQUIRE(flags.size() == 2);
  CHECK(flags[0].first == "gpu_plugin");
  CHECK(flags[0].second == "xenos");
  CHECK(flags[1].first == "render_target_path_vulkan");
  CHECK(flags[1].second == "fsi");
}

TEST_CASE("a portable start keeps a GPU flag the command line or config gave") {
  TempFolder root;
  const Startup startup = ResolveStartup({}, root.path / "game", kManifest);
  const auto given = [](std::string_view name) {
    return name == "gpu_plugin" ? std::string("xenos-debug") : std::string();
  };
  const auto flags = PlumbingToSet(startup, given);
  REQUIRE(flags.size() == 1);
  CHECK(flags[0].first == "render_target_path_vulkan");
}

TEST_CASE("a start with folders on the command line sets no GPU flags") {
  TempFolder root;
  const Startup startup = ResolveStartup({root.path / "assets", {}, {}}, root.path / "game", kManifest);
  CHECK(PlumbingToSet(startup, [](std::string_view) { return std::string(); }).empty());
}

TEST_CASE("this build expects the committed disc manifest's version") {
  CHECK(std::string(WWE_DISC_MANIFEST_VERSION) == kManifest);
}
