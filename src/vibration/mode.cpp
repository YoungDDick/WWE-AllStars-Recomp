#include "vibration/mode.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include <rex/cvar.h>
#include <rex/input/input_system.h>

#include "vibration/rumble_mixer.h"

REXCVAR_DECLARE(int32_t, wwe_vibration_mode);      // settings_cvars.cpp
REXCVAR_DECLARE(int32_t, wwe_vibration_strength);  // settings_cvars.cpp

namespace wwe::vibration {

namespace {

// Guest threads add effects while the input thread samples them.
std::mutex g_mixer_mutex;
RumbleMixer g_mixer;
uint32_t g_world = 0;  // the GWorld the effects belong to

// The mixer's speeds for the pad's request. Every effect is dropped outside
// Enhanced, on a pause and in a new world. Holds g_mixer_mutex.
Speeds Mix(uint32_t pad, Clock::time_point now, const GameState& game) {
  const bool playing = CurrentMode() == Mode::kEnhanced && !game.paused;
  if (!playing || game.world != g_world) {
    g_mixer.Clear();
    g_world = game.world;
  }
  return playing ? g_mixer.Sample(pad, now) : Speeds{0, 0};
}

double Scale() {
  if (CurrentMode() == Mode::kOff) {
    return 0.0;
  }
  // A perceptual curve (strength squared, the spec's choice): the steps feel
  // more even than a linear scale, at the cost of a weak low end (25 % drives
  // the motors at about 6 %). The owner judges it in the match check.
  const double strength = REXCVAR_GET(wwe_vibration_strength) / 100.0;
  return strength * strength;
}

}  // namespace

Mode CurrentMode() { return static_cast<Mode>(REXCVAR_GET(wwe_vibration_mode)); }

void Configure(rex::input::InputSystem* input, std::function<Clock::time_point()> now,
               std::function<GameState()> game) {
  {
    std::lock_guard lock(g_mixer_mutex);
    g_world = game().world;
  }
  input->set_vibration_output([now = std::move(now), game = std::move(game)](
                                  uint32_t user_index,
                                  const rex::input::X_INPUT_VIBRATION& requested) {
    // Read before the lock: these are guest-memory reads.
    const GameState state = game();
    const Clock::time_point time = now();
    Speeds mixed;
    {
      std::lock_guard lock(g_mixer_mutex);
      mixed = Mix(user_index, time, state);
    }
    // While paused Enhanced sends zero: the game's own waveform keeps
    // advancing through a pause (docs/research/controller-vibration.md).
    const double scale = CurrentMode() == Mode::kEnhanced && state.paused ? 0.0 : Scale();
    const auto scaled = [scale](uint16_t game, uint16_t mixer) {
      return uint16_t(std::lround(std::max(game, mixer) * scale));
    };
    rex::input::X_INPUT_VIBRATION out;
    out.left_motor_speed = scaled(requested.left_motor_speed, mixed.left);
    out.right_motor_speed = scaled(requested.right_motor_speed, mixed.right);
    return out;
  });
}

void AddEffect(uint32_t pad, const Shape& shape) {
  std::lock_guard lock(g_mixer_mutex);
  g_mixer.Add(pad, shape);
}

void ClearEffects() {
  std::lock_guard lock(g_mixer_mutex);
  g_mixer.Clear();
}

}  // namespace wwe::vibration
