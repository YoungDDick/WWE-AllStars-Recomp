#include <catch_amalgamated.hpp>

#include <rex/kernel/xam/live/session_data.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace live = rex::kernel::xam::live;

namespace {
uint32_t Be32(const std::vector<uint8_t>& b, size_t at) {
  return (uint32_t(b[at]) << 24) | (uint32_t(b[at + 1]) << 16) | (uint32_t(b[at + 2]) << 8) |
         b[at + 3];
}
uint64_t Be64(const std::vector<uint8_t>& b, size_t at) {
  return (uint64_t(Be32(b, at)) << 32) | Be32(b, at + 4);
}

live::SessionRecord HostedMatch() {
  live::SessionRecord s;
  s.session_id = 0x80123456789ABCDEull;
  s.host_xuid = 0x0009000000000A01ull;
  s.flags = 0x82F;
  s.public_slots = 2;
  s.open_public = 1;
  s.filled_public = 1;
  s.host_address = "127.0.0.2";
  s.mac = {0x02, 0xAA, 0xBB, 0xCC, 0x0A, 0x01};
  s.port = 36000;
  s.contexts = {{0x800B, 0}, {0x800A, 1}};
  live::UserProperty count;
  count.id = 0x2000000D;
  count.type = live::kUserDataInt64;
  count.value = {0, 0, 0, 0, 0, 0, 0, 3};
  live::UserProperty name;
  name.id = 0x40008109;
  name.type = live::kUserDataWString;
  name.value = {0, 'A', 0, 'l', 0, 0};
  s.properties = {count, name};
  return s;
}

constexpr uint32_t kBase = 0x416D0000;
}  // namespace

TEST_CASE("search results land in the caller's buffer", "[online]") {
  const auto bytes = live::PackSearchResults({HostedMatch()}, kBase, 66308);
  REQUIRE(bytes.size() <= 66308);
  CHECK(Be32(bytes, 0) == 1);          // dwSearchResults
  CHECK(Be32(bytes, 4) == kBase + 8);  // pResults
  const size_t r = 8;
  CHECK(Be64(bytes, r + 0) == 0x80123456789ABCDEull);  // info.sessionID
  CHECK(Be32(bytes, r + 8) == 0x7F000002);             // hostAddress.ina
  CHECK(Be32(bytes, r + 12) == 0x7F000002);            // hostAddress.inaOnline
  CHECK(bytes[r + 16] == 0x8C);                        // wPortOnline 36000
  CHECK(bytes[r + 17] == 0xA0);
  CHECK(std::memcmp(&bytes[r + 18], "\x02\xAA\xBB\xCC\x0A\x01", 6) == 0);  // abEnet
  CHECK(bytes[r + 44] == 0);   // keyExchangeKey 00 01 02 ...
  CHECK(bytes[r + 59] == 15);
  CHECK(Be32(bytes, r + 60) == 1);  // open public
  CHECK(Be32(bytes, r + 64) == 0);  // open private
  CHECK(Be32(bytes, r + 68) == 1);  // filled public
  CHECK(Be32(bytes, r + 76) == 2);  // properties
  CHECK(Be32(bytes, r + 80) == 2);  // contexts
  const uint32_t properties = Be32(bytes, r + 84);
  const uint32_t contexts = Be32(bytes, r + 88);

  const size_t c = contexts - kBase;
  CHECK(Be32(bytes, c) == 0x800B);
  CHECK(Be32(bytes, c + 12) == 1);

  const size_t p = properties - kBase;
  CHECK(p % 8 == 0);
  CHECK(Be32(bytes, p) == 0x2000000D);
  CHECK(bytes[p + 8] == live::kUserDataInt64);
  CHECK(Be64(bytes, p + 16) == 3);
  CHECK(Be32(bytes, p + 24) == 0x40008109);
  CHECK(bytes[p + 32] == live::kUserDataWString);
  CHECK(Be32(bytes, p + 40) == 6);  // string size in bytes
  const size_t text = Be32(bytes, p + 44) - kBase;
  CHECK(bytes[text + 1] == 'A');
  CHECK(bytes[text + 3] == 'l');
}

TEST_CASE("a search result gives the host's local and public addresses", "[online]") {
  auto session = HostedMatch();
  session.host_address = "203.0.113.7";
  session.local_address = "192.168.1.10";
  const auto bytes = live::PackSearchResults({session}, kBase, 66308);
  CHECK(Be32(bytes, 8 + 8) == 0xC0A8010A);   // hostAddress.ina
  CHECK(Be32(bytes, 8 + 12) == 0xCB007107);  // hostAddress.inaOnline
}

TEST_CASE("a session's local address survives JSON", "[online]") {
  auto session = HostedMatch();
  session.local_address = "192.168.1.10";
  const auto parsed = live::ParseSessions(live::SessionJson(session, 0));
  REQUIRE(parsed.size() == 1);
  CHECK(parsed[0].local_address == "192.168.1.10");
}

TEST_CASE("results that don't fit are left out", "[online]") {
  const auto session = HostedMatch();
  const auto one = live::PackSearchResults({session}, kBase, 66308);
  const auto bytes = live::PackSearchResults({session, session}, kBase, uint32_t(one.size()));
  CHECK(Be32(bytes, 0) == 1);
  CHECK(bytes.size() <= one.size());
}

TEST_CASE("a session is created and found through JSON", "[online]") {
  auto session = HostedMatch();
  session.build_id = "abc123";
  const auto json = live::SessionJson(session, 0);
  CHECK(json.find(R"("sessionId":"80123456789abcde")") != std::string::npos);
  CHECK(json.find(R"("xuid":"0009000000000A01")") != std::string::npos);
  CHECK(json.find(R"("hostAddress":"127.0.0.2")") != std::string::npos);
  CHECK(json.find(R"("buildId":"abc123")") != std::string::npos);
  CHECK(json.find(R"("publicSlotsCount":2)") != std::string::npos);

  const auto properties = live::PropertiesJson(session.contexts, session.properties);
  CHECK(properties.find(R"({"id":32779,"value":0})") != std::string::npos);
  CHECK(properties.find(R"("id":536870925,"type":2,"bytes":"AAAAAAAAAAM=")") != std::string::npos);
  CHECK(properties.find(R"("id":1073774857,"type":4,"bytes":"AEEAbAAA")") != std::string::npos);

  // The server's answer to a search: the session with its slot counts and properties.
  const std::string reply =
      R"([{"sessionId":"80123456789abcde","xuid":"0009000000000A01","flags":2095,)"
      R"("publicSlotsCount":2,"privateSlotsCount":0,"hostAddress":"127.0.0.2",)"
      R"("macAddress":"02aabbcc0a01","port":36000,"openPublicSlotsCount":1,)"
      R"("openPrivateSlotsCount":0,"filledPublicSlotsCount":1,"filledPrivateSlotsCount":0,)"
      R"("contexts":[{"id":32779,"value":0},{"id":32778,"value":1}],)"
      R"("properties":[{"id":536870925,"type":2,"int":3},)"
      R"({"id":1073774857,"type":4,"bytes":"AEEAbAAA"}]}])";
  const auto found = live::ParseSessions(reply);
  REQUIRE(found.size() == 1);
  CHECK(found[0].session_id == session.session_id);
  CHECK(found[0].open_public == 1);
  CHECK(found[0].mac == session.mac);
  REQUIRE(found[0].properties.size() == 2);
  CHECK(found[0].properties[0].value == session.properties[0].value);
  CHECK(found[0].properties[1].value == session.properties[1].value);
  CHECK(found[0].contexts.size() == 2);
}

TEST_CASE("a guest property value keeps its type and its bytes", "[online]") {
  const uint8_t int32[] = {0, 0, 1, 2};
  const auto p = live::GuestProperty(0x1000000C, int32, sizeof(int32));
  CHECK(p.type == live::kUserDataInt32);
  CHECK(p.value == std::vector<uint8_t>{0, 0, 1, 2});
  // The game sets 4-byte values on its 8-byte (int64) properties 0x2000000D-0F and reads
  // them back as 32-bit numbers, so the bytes are kept as given.
  const uint8_t arena[] = {0, 0, 0, 5};
  const auto a = live::GuestProperty(0x2000000F, arena, sizeof(arena));
  CHECK(a.type == live::kUserDataInt64);
  CHECK(a.value == std::vector<uint8_t>{0, 0, 0, 5});
  const auto packed = live::PackSearchResults(
      {[&] {
        live::SessionRecord s;
        s.properties = {a};
        return s;
      }()},
      kBase, 4096);
  const size_t at = Be32(packed, 8 + 84) - kBase;
  CHECK(Be32(packed, at + 16) == 5);
  const auto parsed = live::ParseSessions(
      "{\"properties\":[{\"id\":536870927,\"type\":2,\"bytes\":\"AAAABQ==\"}]}");
  REQUIRE(parsed.size() == 1);
  CHECK(parsed[0].properties[0].value == a.value);
  CHECK(live::PropertiesJson({}, {a}).find(R"("bytes":"AAAABQ==")") != std::string::npos);
}
