// Call counters on every function of the XDK D3D library (sweep builds only,
// -DWWE_D3D_SWEEP=ON); used to find which draw entry points a game uses.
//
// Besides calls, each hook attributes the draw packets it wrote: the device's
// ring write cursor (device+0x30, the field EndVertices restores) is read
// before and after the original, and the PM4 type-3 DRAW_INDX/DRAW_INDX_2
// headers in between are counted. Counts are inclusive of nested hooked calls.
#pragma once

#include <atomic>
#include <cstdint>

#include "d3d_sweep_count.h"

namespace wwe::sweep {

extern const uint32_t kAddresses[kCount];
extern std::atomic<uint64_t> g_calls[kCount];
extern std::atomic<uint64_t> g_draws[kCount];

// Device pointer seen by D3DDevice_Present; zero until the first present.
void SetDevice(uint32_t device);

uint32_t Enter(int index, const uint8_t* base);
void Exit(int index, const uint8_t* base, uint32_t cursor_before);

}  // namespace wwe::sweep
