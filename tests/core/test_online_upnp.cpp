#include <catch_amalgamated.hpp>

#include <rex/kernel/xam/live/port_map.h>
#include <rex/kernel/xam/live/xlive_json.h>

#include <cstdint>
#include <string>

namespace live = rex::kernel::xam::live;

namespace {
constexpr uint32_t kPeerPublic = 0xCB007107;  // 203.0.113.7
constexpr uint32_t kOther = 0xC6336404;       // 198.51.100.4
}  // namespace

TEST_CASE("a mapped game port is sent to its external port", "[online][upnp]") {
  const live::PortMap peer = {{1000, 2000}, {9100, 9100}};
  CHECK(live::ExternalPort(peer, 1000) == 2000);
  CHECK(live::ExternalPort(peer, 9100) == 9100);
  CHECK(live::ExternalPort(peer, 9101) == 9101);  // not mapped: as the game asks
  CHECK(live::ExternalPort({}, 1000) == 1000);
}

TEST_CASE("a datagram from a mapped external port reaches the game from its game port",
          "[online][upnp]") {
  const live::PortMap peer = {{1000, 2000}};
  CHECK(live::GamePort(peer, 2000) == 1000);
  CHECK(live::GamePort(peer, 1000) == 1000);
  CHECK(live::GamePort(peer, 5555) == 5555);
}

// The router may already map a port for another device on the network.
TEST_CASE("a taken external port is tried again a thousand higher", "[online][upnp]") {
  CHECK(live::CandidateExternalPort(1000, 0) == 1000);
  CHECK(live::CandidateExternalPort(1000, 1) == 2000);
  CHECK(live::CandidateExternalPort(9101, 2) == 11101);
}

TEST_CASE("each peer's mappings apply only to its public address", "[online][upnp]") {
  live::PeerPorts ports;
  ports.Remember(kPeerPublic, {{1000, 2000}});
  CHECK(ports.External(kPeerPublic, 1000) == 2000);
  CHECK(ports.Game(kPeerPublic, 2000) == 1000);
  CHECK(ports.External(kOther, 1000) == 1000);
  CHECK(ports.Game(kOther, 2000) == 2000);
}

TEST_CASE("a player's mapped ports are sent and read", "[online][upnp]") {
  live::PlayerRecord player;
  player.xuid = 0x0009000000000A01;
  player.mapped_ports = {{1000, 2000}, {9101, 9101}};
  const auto json = live::PlayerJson(player);
  CHECK(json.find(R"("mappedPorts":{"1000":2000,"9101":9101})") != std::string::npos);
  const auto parsed = live::ParsePlayer(json);
  REQUIRE(parsed.has_value());
  CHECK(parsed->mapped_ports == player.mapped_ports);
  live::PlayerRecord unmapped;
  unmapped.xuid = 1;
  CHECK(live::PlayerJson(unmapped).find("mappedPorts") == std::string::npos);
}

TEST_CASE("a registration says whether the server reached the mapped port", "[online][upnp]") {
  const auto reached = live::ParseRegistration(R"({"reachable":true})");
  REQUIRE(reached.has_value());
  CHECK(reached->reachable == true);
  const auto silent = live::ParseRegistration("{}");
  REQUIRE(silent.has_value());
  CHECK_FALSE(silent->reachable.has_value());
}
