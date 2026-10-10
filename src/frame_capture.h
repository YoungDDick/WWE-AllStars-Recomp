// F12 capture (ticket 33): while the owner plays, a key press saves the frame
// on screen as a numbered PNG next to the session log, on Emulated and on
// Native, so Emulated and Native captures of the same moment can be paired.
// The frame comes from the frame-dump paths (Native's presented image, or
// Emulated's front buffer with --readback_resolve, else its presenter output).

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/ui/virtual_key.h>

namespace wwe::frame_capture {

// "capture-0001-frame-012345.png": session sequence, then the guest frame.
inline std::string CaptureFileName(uint32_t sequence, uint64_t frame) {
  return fmt::format("capture-{:04}-frame-{:06}.png", sequence, frame);
}

// An 8-bit RGB PNG with uncompressed (stored) deflate blocks: no compression
// library needed, and the files are only screenshots.
inline std::vector<uint8_t> EncodePngRgb(const uint8_t* rgb, uint32_t width, uint32_t height) {
  static const std::array<uint32_t, 256> crc_table = [] {
    std::array<uint32_t, 256> table{};
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    return table;
  }();
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  auto be32 = [&](uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(v >> s));
  };
  auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
    be32(uint32_t(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = start; i < out.size(); ++i) crc = crc_table[(crc ^ out[i]) & 0xFF] ^ (crc >> 8);
    be32(crc ^ 0xFFFFFFFFu);
  };

  std::vector<uint8_t> ihdr;
  for (uint32_t v : {width, height}) {
    for (int s = 24; s >= 0; s -= 8) ihdr.push_back(uint8_t(v >> s));
  }
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit RGB, no interlace
  chunk("IHDR", ihdr);

  // Each row behind filter byte 0, in a zlib stream of stored blocks.
  std::vector<uint8_t> raw;
  raw.reserve(size_t(height) * (size_t(width) * 3 + 1));
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + size_t(y) * width * 3, rgb + size_t(y + 1) * width * 3);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t at = 0; at < raw.size() || raw.empty(); at += 65535) {
    const size_t n = std::min<size_t>(65535, raw.size() - at);
    z.push_back(at + n == raw.size() ? 1 : 0);
    z.insert(z.end(), {uint8_t(n), uint8_t(n >> 8), uint8_t(~n), uint8_t(~n >> 8)});
    z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    if (raw.empty()) break;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  for (int s = 24; s >= 0; s -= 8) z.push_back(uint8_t((b << 16 | a) >> s));
  chunk("IDAT", z);
  chunk("IEND", {});
  return out;
}

// Registers the F12 bind (rebindable as bind_capture_frame); call once at setup.
void Configure();

// True for a key repeat of the capture bind, which must not capture again
// (holding the key saves one capture).
bool IsCaptureRepeat(rex::ui::VirtualKey key, bool repeat);

// The path to write this frame's capture to, consuming a pending request; empty
// if F12 was not pressed. Called by the frame-dump paths at each present.
std::string TakeRequest(uint64_t frame);

}  // namespace wwe::frame_capture
