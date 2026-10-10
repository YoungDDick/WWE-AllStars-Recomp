#include <catch_amalgamated.hpp>

#include <rex/kernel/xam/live/identity.h>
#include <rex/kernel/xam/live/xlive_json.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace live = rex::kernel::xam::live;

namespace {
std::filesystem::path TempFolder(const char* name) {
  auto path = std::filesystem::temp_directory_path() / "wwe-online-tests" / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}
}  // namespace

TEST_CASE("a new identity has an online XUID and a locally administered MAC", "[online]") {
  std::mt19937_64 random(7);
  for (int i = 0; i < 100; ++i) {
    const auto identity = live::NewIdentity(random);
    CHECK((identity.xuid >> 48) == 0x0009);
    CHECK((identity.xuid & 0x0000FFFFFFFFFFFFull) != 0);
    CHECK((identity.mac[0] & 0x02) == 0x02);  // locally administered
    CHECK((identity.mac[0] & 0x01) == 0);     // unicast
  }
}

TEST_CASE("the first online start writes the identity and later starts read it", "[online]") {
  const auto folder = TempFolder("survives");
  std::mt19937_64 first_random(1), second_random(2);
  const auto first = live::LoadOrCreateIdentity(folder, first_random);
  CHECK(std::filesystem::exists(folder / "online-identity.json"));
  const auto again = live::LoadOrCreateIdentity(folder, second_random);
  CHECK(again.xuid == first.xuid);
  CHECK(again.mac == first.mac);
}

TEST_CASE("another user data folder gets another identity", "[online]") {
  std::mt19937_64 random(3);
  const auto a = live::LoadOrCreateIdentity(TempFolder("a"), random);
  const auto b = live::LoadOrCreateIdentity(TempFolder("b"), random);
  CHECK(a.xuid != b.xuid);
  CHECK(a.mac != b.mac);
}

TEST_CASE("an unreadable identity file is replaced", "[online]") {
  const auto folder = TempFolder("broken");
  std::ofstream(folder / "online-identity.json") << "{ not json";
  std::mt19937_64 random(4);
  const auto identity = live::LoadOrCreateIdentity(folder, random);
  CHECK((identity.xuid >> 48) == 0x0009);
  std::mt19937_64 other(5);
  CHECK(live::LoadOrCreateIdentity(folder, other).xuid == identity.xuid);
}

TEST_CASE("the server setting becomes the API base URL", "[online]") {
  CHECK(live::ApiBaseUrl("127.0.0.1") == "http://127.0.0.1:36000/");
  CHECK(live::ApiBaseUrl("192.168.1.5:8080") == "http://192.168.1.5:8080/");
  CHECK(live::ApiBaseUrl("https://allstars.example.org") == "https://allstars.example.org/");
  CHECK(live::ApiBaseUrl("http://10.0.0.2:36000/") == "http://10.0.0.2:36000/");
}

TEST_CASE("whoami gives the address the server sees", "[online]") {
  CHECK(live::ParseWhoami(R"({"address":"127.0.0.3"})") == "127.0.0.3");
  CHECK_FALSE(live::ParseWhoami("{}").has_value());
  CHECK_FALSE(live::ParseWhoami("not json").has_value());
}

TEST_CASE("a player is sent the way XWS reads it", "[online]") {
  live::PlayerRecord player;
  player.xuid = 0x0009000000000A01;
  player.gamertag = "Alpha";
  player.host_address = "127.0.0.2";
  player.mac = {0x02, 0xAA, 0xBB, 0xCC, 0x0A, 0x01};
  player.machine_id = 0xFA00000000000A01;
  const auto json = live::PlayerJson(player);
  CHECK(json.find(R"("xuid":"0009000000000A01")") != std::string::npos);
  CHECK(json.find(R"("gamertag":"Alpha")") != std::string::npos);
  CHECK(json.find(R"("hostAddress":"127.0.0.2")") != std::string::npos);
  CHECK(json.find(R"("macAddress":"02aabbcc0a01")") != std::string::npos);
  CHECK(json.find(R"("machineId":"fa00000000000a01")") != std::string::npos);

  const auto parsed = live::ParsePlayer(json);
  REQUIRE(parsed.has_value());
  CHECK(parsed->xuid == player.xuid);
  CHECK(parsed->mac == player.mac);
  CHECK(parsed->host_address == "127.0.0.2");
}

TEST_CASE("a registration gives the relay and its token", "[online]") {
  const auto registration = live::ParseRegistration(
      R"({"relayAddress":"46.36.38.4:32482","relayToken":"000102030405060708090a0b0c0d0e0f"})");
  REQUIRE(registration.has_value());
  CHECK(registration->relay_address == "46.36.38.4:32482");
  CHECK(registration->relay_token == "000102030405060708090a0b0c0d0e0f");
  CHECK(live::ParseRegistration("{}").has_value());  // an older server: no relay
  CHECK_FALSE(live::ParseRegistration("not json").has_value());
}

TEST_CASE("a player sends the game version the port is built on", "[online]") {
  live::PlayerRecord player;
  player.xuid = 0x0009000000000A01;
  CHECK(live::PlayerJson(player).find(R"("gameVersion":"tu2")") != std::string::npos);
}

TEST_CASE("a refused sign-in gives the server's update message", "[online]") {
  const auto message = live::ParseRefusal(
      R"({"error":"update required","gameVersion":"tu3","message":"Update the port to play online."})");
  REQUIRE(message.has_value());
  CHECK(*message == "Update the port to play online.");
  CHECK_FALSE(live::ParseRefusal("{}").has_value());
  CHECK_FALSE(live::ParseRefusal("not json").has_value());
}

TEST_CASE("a player carries its local address next to its public one", "[online]") {
  live::PlayerRecord player;
  player.xuid = 0x0009000000000C02;
  player.host_address = "203.0.113.7";
  player.local_address = "192.168.1.11";
  const auto json = live::PlayerJson(player);
  CHECK(json.find(R"("localAddress":"192.168.1.11")") != std::string::npos);
  const auto parsed = live::ParsePlayer(json);
  REQUIRE(parsed.has_value());
  CHECK(parsed->host_address == "203.0.113.7");
  CHECK(parsed->local_address == "192.168.1.11");
}

TEST_CASE("the title server list becomes LSP servers", "[online]") {
  const auto servers = live::ParseTitleServers(
      R"([{"address":"10.0.0.5","flags":0,"description":"WWE All Stars Rendez-Vous"}])");
  REQUIRE(servers.size() == 1);
  CHECK(servers[0].address == 0x0A000005);
  CHECK(servers[0].flags == 0);
  CHECK(servers[0].description == "WWE All Stars Rendez-Vous");
  CHECK(live::ParseTitleServers("[]").empty());
  CHECK(live::ParseTitleServers("oops").empty());
}
