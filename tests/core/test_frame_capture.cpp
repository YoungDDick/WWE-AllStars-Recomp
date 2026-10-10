#include <catch_amalgamated.hpp>

#include <cstdint>
#include <vector>

#include "frame_capture.h"

using namespace wwe::frame_capture;

namespace {

uint32_t BE32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}

}  // namespace

TEST_CASE("captures are numbered per session and carry the guest frame") {
  CHECK(CaptureFileName(1, 12345) == "capture-0001-frame-012345.png");
  CHECK(CaptureFileName(27, 7) == "capture-0027-frame-000007.png");
}

TEST_CASE("a PNG is a valid RGB image of the pixels given") {
  // 2x2: red, green / blue, white.
  const uint8_t rgb[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
  const std::vector<uint8_t> png = EncodePngRgb(rgb, 2, 2);
  const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  REQUIRE(png.size() > 8 + 25 + 12);
  for (int i = 0; i < 8; ++i) CHECK(png[i] == signature[i]);

  // IHDR: 2x2, 8-bit RGB, no interlace.
  CHECK(BE32(png, 8) == 13);
  CHECK(png[12] == 'I');
  CHECK(BE32(png, 16) == 2);
  CHECK(BE32(png, 20) == 2);
  CHECK(png[24] == 8);
  CHECK(png[25] == 2);

  // IDAT: zlib with stored blocks holding each row behind a filter byte 0.
  const size_t idat = 8 + 25;
  const uint32_t length = BE32(png, idat);
  CHECK(png[idat + 4] == 'I');
  CHECK(png[idat + 5] == 'D');
  const size_t z = idat + 8;
  CHECK(png[z] == 0x78);
  const std::vector<uint8_t> raw = {0, 255, 0, 0, 0, 255, 0, 0, 0, 0, 255, 255, 255, 255};
  CHECK(png[z + 2] == 1);  // final stored block
  CHECK((png[z + 3] | png[z + 4] << 8) == raw.size());
  for (size_t i = 0; i < raw.size(); ++i) CHECK(png[z + 7 + i] == raw[i]);
  // Adler-32 of the raw rows ends the zlib stream.
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  CHECK(BE32(png, z + 7 + raw.size()) == (b << 16 | a));
  CHECK(length == 2 + 5 + raw.size() + 4);

  // IEND closes the file.
  const size_t end = png.size() - 12;
  CHECK(BE32(png, end) == 0);
  CHECK(png[end + 4] == 'I');
  CHECK(png[end + 5] == 'E');
  CHECK(BE32(png, end + 8) == 0xAE426082u);  // the fixed CRC of an empty IEND
}
