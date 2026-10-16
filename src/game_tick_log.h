// The engine tick's hooks (game_tick_log.cpp): the tick log, the frame waits, and the
// 60 fps entrances and 120 fps match steps.

#pragma once

namespace rex {
class Runtime;
}

namespace wwe::game_tick {

// Gives the tick hooks the runtime, so a 120 Hz tick can set the guest vblank's rate.
void Configure(rex::Runtime* runtime);

}  // namespace wwe::game_tick
