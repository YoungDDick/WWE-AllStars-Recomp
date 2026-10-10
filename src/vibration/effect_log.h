// The log lines of the effects the Enhanced hooks add to the rumble mixer,
// capped like the vibration request trace so a long match cannot flood the
// log. The trace stays the record of what reached the pad.

#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "vibration/shapes.h"

namespace wwe::vibration {

// One effect added for a pad: the event, the role of that pad's wrestler in
// it, both motors' peaks and the length. `detail` names what the game passed
// (the combat type, the ground-event type).
inline std::string EffectLine(std::string_view event, std::string_view role, uint32_t pad,
                              const Shape& shape, std::string_view detail = {}) {
  const Motors peak = shape.Peak();
  return fmt::format("vibration effect: event={} role={} pad={} left={} right={} ms={}{}{}", event,
                     role, pad, peak.left, peak.right, std::lround(shape.Seconds() * 1000),
                     detail.empty() ? "" : " ", detail);
}

class EffectLog {
 public:
  static constexpr uint32_t kMaxLines = 300;

  // The line to log, the one notice once the cap is reached, or nothing.
  std::optional<std::string> Take(std::string line) {
    if (lines_ > kMaxLines) {
      return std::nullopt;
    }
    if (lines_++ == kMaxLines) {
      return fmt::format("vibration effect: {} lines logged, later effects are not logged",
                         kMaxLines);
    }
    return line;
  }

 private:
  uint32_t lines_ = 0;
};

}  // namespace wwe::vibration
