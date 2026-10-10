#include <catch_amalgamated.hpp>

#include <filesystem>
#include <string>

#include <rex/cvar.h>

#include "settings/config_file.h"
#include "settings/page_model.h"
#include "settings/registry.h"
#include "settings/schema.h"
#include "settings/text_rules.h"

using wwe::settings::PageModel;
using wwe::settings::PlayerSettings;
using wwe::settings::SchemaJson;
using wwe::settings::Setting;
using wwe::settings::TextRule;
using wwe::settings::TextRuleError;
using wwe::settings::WithLifecycles;

namespace {

const Setting* Find(const std::vector<Setting>& settings, const std::string& name) {
  for (const Setting& setting : settings) {
    if (setting.name == name) return &setting;
  }
  return nullptr;
}

std::string Line(const std::string& json, const std::string& key) {
  const size_t at = json.find("{\"key\": \"" + key + "\"");
  return at == std::string::npos ? std::string() : json.substr(at, json.find('\n', at) - at);
}

bool Has(const std::string& line, const std::string& text) {
  return line.find(text) != std::string::npos;
}

}  // namespace

TEST_CASE("a player name follows the Xbox LIVE gamertag rule") {
  // Launcher-online spec: 1 to 15 characters, letters, digits and spaces, first a letter.
  for (const char* name : {"A", "Owner", "Big Show 2", "abcdefghijklmno", "x1 y2"}) {
    INFO(name);
    CHECK(TextRuleError(TextRule::kGamertag, name).empty());
  }
  for (const char* name : {"", "abcdefghijklmnop", "1Up", " Owner", "Owner ", "Own_er", "Own-er",
                           "Ownér", "Own\ter"}) {
    INFO(name);
    CHECK_FALSE(TextRuleError(TextRule::kGamertag, name).empty());
  }
}

TEST_CASE("a refused player name says why") {
  CHECK(TextRuleError(TextRule::kGamertag, "") == "Type a player name.");
  CHECK(TextRuleError(TextRule::kGamertag, "abcdefghijklmnop") ==
        "A player name has at most 15 characters.");
  CHECK(TextRuleError(TextRule::kGamertag, "1Up") == "A player name starts with a letter.");
  CHECK(TextRuleError(TextRule::kGamertag, "Own_er") ==
        "A player name has only letters, digits and spaces.");
  CHECK(TextRuleError(TextRule::kGamertag, "Owner ") == "A player name does not end with a space.");
}

TEST_CASE("the player name is a text row on ONLINE, after Online play") {
  const std::vector<Setting> settings = WithLifecycles(PlayerSettings());
  const Setting* name = Find(settings, "wwe_online_name");
  REQUIRE(name);
  CHECK(name->tab == "ONLINE");
  CHECK(name->label == "Player name");
  CHECK(name->text == TextRule::kGamertag);
  CHECK(name->max_length == 15);
  CHECK(name->choices.empty());
  CHECK(name->startup);
  CHECK(name->restart);
  CHECK(name - 1 == Find(settings, "wwe_online"));
}

TEST_CASE("the schema export prints a text row's kind, length and rule, in format 2") {
  const std::string json = SchemaJson(WithLifecycles(PlayerSettings()));
  CHECK(Has(json, "\"format\": 2,"));
  const std::string line = Line(json, "wwe_online_name");
  INFO(line);
  CHECK(Has(line, "\"tab\": \"ONLINE\", \"type\": \"string\", \"default\": \"\", "
                  "\"applies_at_startup\": true, \"native_only\": false, \"next_start\": true, "
                  "\"kind\": \"text\", \"max_length\": 15, \"rule\": \"gamertag\", "
                  "\"choices\": []"));
  // Choice rows carry none of the text fields.
  CHECK_FALSE(Has(Line(json, "wwe_online"), "\"kind\""));
  CHECK_FALSE(Has(Line(json, "wwe_online"), "\"rule\""));
}

TEST_CASE("the in-game page shows a text row read-only, pointing to the launcher") {
  std::vector<Setting> settings = {
      {"wwe_online", "Online play", "ONLINE", {{"false", "Off"}, {"true", "On"}}, "Online."},
      {.name = "wwe_online_name",
       .label = "Player name",
       .tab = "ONLINE",
       .help = "The name.",
       .text = TextRule::kGamertag,
       .max_length = 15},
  };
  settings[1].restart = true;
  PageModel page(settings);
  page.Open({"true", "Owner"}, 0);
  page.Update({}, 200);
  page.Update({PageModel::kDown, 0, 0}, 300);
  page.Update({}, 310);
  REQUIRE(page.row() == 1);
  CHECK(page.ValueLabel(1) == "Owner");
  CHECK(page.Update({PageModel::kRight, 0, 0}, 320).empty());
  page.Update({}, 330);
  CHECK(page.Update({PageModel::kA, 0, 0}, 340).empty());
  CHECK(page.ValueLabel(1) == "Owner");
  CHECK(page.ApplyNote(1) == "Change in the launcher.");
  CHECK_FALSE(page.needs_restart(1));
}

TEST_CASE("an empty text row shows Not set") {
  std::vector<Setting> settings = {{.name = "wwe_online_name",
                                    .label = "Player name",
                                    .tab = "ONLINE",
                                    .help = "The name.",
                                    .text = TextRule::kGamertag,
                                    .max_length = 15}};
  PageModel page(settings);
  page.Open({""}, 0);
  CHECK(page.ValueLabel(0) == "Not set");
}

TEST_CASE("a player name round-trips through the settings file") {
  const auto path = std::filesystem::temp_directory_path() / "wwe_test_text_settings.toml";
  std::filesystem::remove(path);
  using wwe::settings::SaveSetting;
  using wwe::settings::TomlLiteral;
  REQUIRE(SaveSetting(path, "wwe_online_name", TomlLiteral("wwe_online_name", "Big Show 2")));
  // Text the rules refuse still survives the file: quotes and backslashes are escaped.
  REQUIRE(SaveSetting(path, "wwe_online_server",
                      TomlLiteral("wwe_online_server", R"(a "b" \c d)")));
  CHECK(wwe::settings::ConfigError(path).empty());
  rex::cvar::LoadConfig(path);
  CHECK(rex::cvar::GetFlagByName("wwe_online_name") == "Big Show 2");
  CHECK(rex::cvar::GetFlagByName("wwe_online_server") == R"(a "b" \c d)");
  rex::cvar::ResetToDefault("wwe_online_name");
  rex::cvar::ResetToDefault("wwe_online_server");
  std::filesystem::remove(path);
}

TEST_CASE("online play on without a valid name signs in with the profile name and warns") {
  using wwe::settings::OnlineNameWarning;
  CHECK(OnlineNameWarning(true, "Owner").empty());
  CHECK(OnlineNameWarning(false, "").empty());
  CHECK(OnlineNameWarning(false, "1Up").empty());
  CHECK(OnlineNameWarning(true, "") ==
        "online play is on without a player name: signing in with the profile's name. Choose "
        "one in the launcher's Settings > ONLINE.");
  CHECK(OnlineNameWarning(true, "1Up") ==
        "the player name \"1Up\" is refused (A player name starts with a letter.): signing in "
        "with the profile's name. Choose one in the launcher's Settings > ONLINE.");
}

TEST_CASE("a game server is a host, host:port, or an http(s) URL") {
  for (const char* server :
       {"allstars.komplexaci.cz", "127.0.0.1", "localhost", "192.168.1.5:36000", "my-server.lan:1",
        "https://allstars.komplexaci.cz", "http://10.0.0.2:36000", "https://example.com/api/",
        "HTTPS://Example.com:443"}) {
    INFO(server);
    CHECK(TextRuleError(TextRule::kServerAddress, server).empty());
  }
  for (const char* server : {"", "my server", " allstars.komplexaci.cz", "ftp://example.com",
                             "https://", "https:///api", "host:", "host:0", "host:65536",
                             "host:abc", "ho_st", "http://ho st", "host/path", "user@host",
                             "allstars.komplexaci.cz\t"}) {
    INFO(server);
    CHECK_FALSE(TextRuleError(TextRule::kServerAddress, server).empty());
  }
}

TEST_CASE("a refused game server says why") {
  CHECK(TextRuleError(TextRule::kServerAddress, "") == "Type a game server address.");
  CHECK(TextRuleError(TextRule::kServerAddress, "my server") ==
        "A server address has no spaces.");
  CHECK(TextRuleError(TextRule::kServerAddress, "ftp://example.com") ==
        "A server URL starts with http:// or https://.");
  CHECK(TextRuleError(TextRule::kServerAddress, "host:abc") ==
        "A server address is a host name, host:port (port 1 to 65535), or an http(s) URL.");
}

TEST_CASE("the game server is a text row on ONLINE with the owner's server by default") {
  const std::vector<Setting> settings = WithLifecycles(PlayerSettings());
  const Setting* server = Find(settings, "wwe_online_server");
  REQUIRE(server);
  CHECK(server->tab == "ONLINE");
  CHECK(server->label == "Game server");
  CHECK(server->text == TextRule::kServerAddress);
  CHECK(server->startup);
  CHECK(server->restart);
  CHECK(server - 1 == Find(settings, "wwe_online_name"));
  const std::string line = Line(SchemaJson(settings), "wwe_online_server");
  INFO(line);
  CHECK(Has(line, "\"default\": \"https://allstars.komplexaci.cz\""));
  CHECK(Has(line, "\"kind\": \"text\", \"max_length\": 253, \"rule\": \"server_address\""));
  CHECK(TextRuleError(TextRule::kServerAddress,
                      rex::cvar::GetFlagInfo("wwe_online_server")->default_value)
            .empty());
}

TEST_CASE("online play on with a server address the rule refuses is logged at startup") {
  using wwe::settings::OnlineServerWarning;
  CHECK(OnlineServerWarning(true, "https://allstars.komplexaci.cz").empty());
  CHECK(OnlineServerWarning(false, "my server").empty());
  CHECK(OnlineServerWarning(true, "my server") ==
        "the game server \"my server\" is refused (A server address has no spaces.): online play "
        "may not sign in. Fix it in the launcher's Settings > ONLINE.");
}
