#include "vibration/rumble_mixer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace wwe::vibration {

namespace {

// The game's sampler 0x829ED788 scales an amplitude by 655 (mulli r10,r11,0x28f).
constexpr double kSpeedPerPercent = 655.0;

}  // namespace

void RumbleMixer::Add(uint32_t pad, const Shape& shape) {
  if (pad >= kPads || shape.samples.empty()) {
    return;
  }
  pads_[pad].effects.push_back({shape});
}

bool RumbleMixer::Advance(Effect& effect, double seconds) {
  effect.elapsed += seconds;
  const std::vector<wwe::vibration::Sample>& samples = effect.shape.samples;
  while (effect.sample < samples.size()) {
    const wwe::vibration::Sample& current = samples[effect.sample];
    // Every sample is output on at least one request and a crack on two, so a
    // long frame never skips one (a snap's gap) or cuts a crack to one frame.
    if (effect.elapsed < current.seconds || effect.requests < (current.crack ? 2u : 1u)) {
      return true;
    }
    // The remainder carries into the next sample, as the game's update
    // 0x8292B4B8 carries it.
    effect.elapsed -= current.seconds;
    effect.sample++;
    effect.requests = 0;
  }
  return false;
}

// One motor's speed `t` (0-1) into its sample, by the functions of the game's
// sampler 0x829ED788.
uint16_t RumbleMixer::Speed(const Motor& motor, double t) {
  using std::numbers::pi;
  double f = 1;
  switch (motor.function) {
    case kLinearIncreasing:
      f = t;
      break;
    case kLinearDecreasing:
      f = 1 - t;
      break;
    case kSineRise:
      f = std::sin(t * pi / 2);
      break;
    case kCosineFall:
      f = std::sin((t + 1) * pi / 2);
      break;
    case kSineBump:
      f = std::sin(t * pi);
      break;
    case kNoise:
      f = std::uniform_real_distribution<double>(0, 1)(noise_);
      break;
    default:  // kConstant
      break;
  }
  return uint16_t(std::lround(std::clamp(f, 0.0, 1.0) * motor.amplitude * kSpeedPerPercent));
}

Speeds RumbleMixer::Sample(uint32_t pad, Clock::time_point now) {
  Speeds out{0, 0};
  if (pad >= kPads) {
    return out;
  }
  Pad& state = pads_[pad];
  double step = 0;
  if (state.last_request) {
    step = std::chrono::duration<double>(std::min(now - *state.last_request, kMaxStep)).count();
  }
  state.last_request = now;
  std::erase_if(state.effects, [&](Effect& effect) {
    if (!effect.started) {
      effect.started = true;
    } else if (!Advance(effect, step)) {
      return true;
    }
    const wwe::vibration::Sample& sample = effect.shape.samples[effect.sample];
    const double t = sample.seconds > 0 ? effect.elapsed / sample.seconds : 0;
    out.left = std::max(out.left, Speed(sample.left, t));
    out.right = std::max(out.right, Speed(sample.right, t));
    effect.requests++;
    return false;
  });
  return out;
}

void RumbleMixer::Clear() {
  for (Pad& pad : pads_) pad.effects.clear();
}

}  // namespace wwe::vibration
