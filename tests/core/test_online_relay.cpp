#include <catch_amalgamated.hpp>

#include <rex/kernel/xam/live/relay_protocol.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace relay = rex::kernel::xam::live::relay;

namespace {
std::vector<uint8_t> Bytes(const std::string& text) {
  return {text.begin(), text.end()};
}
}  // namespace

// The same layout as server/internal/relay (big-endian after "ASR1" and the type).
TEST_CASE("a game datagram is wrapped for the relay", "[online][relay]") {
  const auto payload = Bytes("vdp");
  const auto packet = relay::Send(0x0009000000000B02, 1000, 9100, payload.data(), payload.size());
  const std::vector<uint8_t> expected = {'A', 'S', 'R', '1', 5,    0x00, 0x09, 0, 0, 0,  0,
                                         0x0B, 0x02, 0x03, 0xE8, 0x23, 0x8C, 'v', 'd', 'p'};
  CHECK(packet == expected);
}

TEST_CASE("a hello carries the XUID and the registration's token", "[online][relay]") {
  const auto token = relay::ParseToken("000102030405060708090a0b0c0d0e0f");
  REQUIRE(token.has_value());
  const auto packet = relay::Hello(0x0009000000000A01, *token);
  REQUIRE(packet.size() == 5 + 8 + 16);
  CHECK(packet[4] == 1);
  CHECK(packet[12] == 0x01);
  CHECK(packet[13] == 0x00);
  CHECK(packet[28] == 0x0F);
  CHECK_FALSE(relay::ParseToken("not hex").has_value());
  CHECK_FALSE(relay::ParseToken("0001").has_value());
}

TEST_CASE("a delivered datagram is unwrapped", "[online][relay]") {
  const std::vector<uint8_t> packet = {'A', 'S', 'R', '1', 6,    0x00, 0x09, 0, 0, 0,  0,
                                       0x0A, 0x01, 0x23, 0x8C, 0x03, 0xE8, 'h', 'i'};
  const auto message = relay::Parse(packet.data(), packet.size());
  REQUIRE(message.has_value());
  CHECK(message->kind == relay::Message::kDeliver);
  CHECK(message->value == 0x0009000000000A01);
  CHECK(message->src_port == 9100);
  CHECK(message->dst_port == 1000);
  CHECK(std::string(message->payload, message->payload + message->size) == "hi");
}

TEST_CASE("welcome and pong are read; anything else is not", "[online][relay]") {
  const auto pong = relay::Ping(42);
  auto as_pong = pong;
  as_pong[4] = 4;
  const auto message = relay::Parse(as_pong.data(), as_pong.size());
  REQUIRE(message.has_value());
  CHECK(message->kind == relay::Message::kPong);
  CHECK(message->value == 42);

  const auto garbage = Bytes("ASR9xxxxxxxxxxxx");
  CHECK_FALSE(relay::Parse(garbage.data(), garbage.size()).has_value());
  const std::vector<uint8_t> short_deliver = {'A', 'S', 'R', '1', 6, 0, 0};
  CHECK_FALSE(relay::Parse(short_deliver.data(), short_deliver.size()).has_value());
  // A player never receives a send.
  CHECK_FALSE(relay::Parse(relay::Send(1, 1, 1, nullptr, 0).data(), 17).has_value());
}

TEST_CASE("each relayed peer gets its own stand-in loopback address", "[online][relay]") {
  relay::PeerTable table;
  const auto always = [](uint32_t) { return true; };
  const auto a = table.Add(0x0009000000000A01, always);
  const auto b = table.Add(0x0009000000000B02, always);
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(*a != *b);
  CHECK((*a >> 24) == 127);
  CHECK(table.Add(0x0009000000000A01, always) == a);  // the same peer keeps its address
  CHECK(table.XuidAt(*b) == 0x0009000000000B02);
  CHECK(table.StandInFor(0x0009000000000A01) == a);
  CHECK_FALSE(table.XuidAt(0x7F000001).has_value());
}

// Another copy on this PC may hold a stand-in address already.
TEST_CASE("a stand-in address that cannot be bound is skipped", "[online][relay]") {
  relay::PeerTable table;
  std::set<uint32_t> taken = {relay::kFirstStandIn, relay::kFirstStandIn + 1};
  const auto stand_in =
      table.Add(0x0009000000000A01, [&](uint32_t address) { return !taken.count(address); });
  CHECK(stand_in == relay::kFirstStandIn + 2);
  CHECK_FALSE(table.Add(0x0009000000000B02, [](uint32_t) { return false; }).has_value());
}

TEST_CASE("a peer that answers the direct probe is reached directly", "[online][relay]") {
  relay::RouteTable routes;
  CHECK(routes.Get(0x0009000000000A01) == relay::Route::kUndecided);
  CHECK(routes.Settle(0x0009000000000A01, true) == relay::Route::kDirect);
  CHECK(routes.Get(0x0009000000000A01) == relay::Route::kDirect);
}

TEST_CASE("a peer that never answers goes through the relay", "[online][relay]") {
  relay::RouteTable routes;
  CHECK(routes.Settle(0x0009000000000B02, false) == relay::Route::kRelay);
}

TEST_CASE("a peer's route stays put for the session", "[online][relay]") {
  relay::RouteTable routes;
  routes.Settle(0x0009000000000A01, false);
  CHECK(routes.Settle(0x0009000000000A01, true) == relay::Route::kRelay);
  routes.Settle(0x0009000000000B02, true);
  CHECK(routes.Settle(0x0009000000000B02, false) == relay::Route::kDirect);
}

// The side that did not probe follows the side that fell back.
TEST_CASE("relayed data from an undecided peer settles it on the relay", "[online][relay]") {
  relay::RouteTable routes;
  routes.Relayed(0x0009000000000A01);
  CHECK(routes.Get(0x0009000000000A01) == relay::Route::kRelay);
  routes.Settle(0x0009000000000B02, true);
  routes.Relayed(0x0009000000000B02);
  CHECK(routes.Get(0x0009000000000B02) == relay::Route::kDirect);
}

TEST_CASE("a direct probe is answered with its nonce", "[online][relay]") {
  const auto probe = relay::Probe(0x1122334455667788);
  const auto nonce = relay::ParseProbe(probe.data(), probe.size());
  REQUIRE(nonce.has_value());
  CHECK(*nonce == 0x1122334455667788);
  const auto answer = relay::ProbeAnswer(*nonce);
  CHECK(relay::ParseProbeAnswer(answer.data(), answer.size()) == 0x1122334455667788);
  CHECK_FALSE(relay::ParseProbe(answer.data(), answer.size()).has_value());
  CHECK_FALSE(relay::ParseProbeAnswer(probe.data(), probe.size()).has_value());
}
