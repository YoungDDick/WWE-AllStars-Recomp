#include <catch_amalgamated.hpp>

#include <rex/kernel/xam/live/addressing.h>

#include <cstdint>

namespace live = rex::kernel::xam::live;

namespace {
constexpr uint32_t kHomePublic = 0xCB007107;  // 203.0.113.7
constexpr uint32_t kOtherPublic = 0xC6336404;  // 198.51.100.4
constexpr uint32_t kHomeLocalB = 0xC0A8010B;  // 192.168.1.11
constexpr uint32_t kCopyA = 0x7F000002;       // 127.0.0.2
constexpr uint32_t kCopyB = 0x7F000003;       // 127.0.0.3
}  // namespace

TEST_CASE("a peer behind the same public address is reached on its local address", "[online]") {
  CHECK(live::PeerAddress(kHomeLocalB, kHomePublic, kHomePublic) == kHomeLocalB);
  CHECK(live::PeerAddress(kCopyB, kHomePublic, kHomePublic) == kCopyB);
}

TEST_CASE("a peer elsewhere is reached on its public address", "[online]") {
  CHECK(live::PeerAddress(kHomeLocalB, kHomePublic, kOtherPublic) == kHomePublic);
}

TEST_CASE("a peer with only one address is reached there", "[online]") {
  CHECK(live::PeerAddress(0, kHomePublic, kHomePublic) == kHomePublic);
  CHECK(live::PeerAddress(kHomeLocalB, 0, kOtherPublic) == kHomeLocalB);
}

TEST_CASE("without a copy address every bind stays as the game asks", "[online]") {
  const auto bind = live::PlanBind(0, 1001, 0);
  CHECK(bind.address == 0);
  CHECK(bind.port == 1001);
}

TEST_CASE("a copy's peer ports bind to its own address", "[online]") {
  for (const uint16_t port : {uint16_t(1000), uint16_t(9100)}) {
    const auto bind = live::PlanBind(0, port, kCopyA);
    CHECK(bind.address == kCopyA);
    CHECK(bind.port == port);
  }
}

// The Rendez-Vous client's fixed port would collide between copies on one PC, and a
// loopback address cannot reach a server on the internet.
TEST_CASE("a copy's other sockets take any port on the default route", "[online]") {
  const auto bind = live::PlanBind(0, 1001, kCopyA);
  CHECK(bind.address == 0);
  CHECK(bind.port == 0);
}

TEST_CASE("an explicit bind address is kept", "[online]") {
  const auto bind = live::PlanBind(kHomeLocalB, 1000, kCopyA);
  CHECK(bind.address == kHomeLocalB);
  CHECK(bind.port == 1000);
}

TEST_CASE("an unbound socket leaves from the copy's address only towards loopback", "[online]") {
  CHECK(live::ImplicitBindAddress(kCopyB, kCopyA) == kCopyA);
  CHECK(live::ImplicitBindAddress(kHomePublic, kCopyA) == 0);
  CHECK(live::ImplicitBindAddress(kCopyB, 0) == 0);
}
