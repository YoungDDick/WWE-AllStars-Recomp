// --wwe_resolve_read_watch: which resolve destinations the guest CPU reads
// back (ticket 06). Research tooling for Emulated runs, never for releases.
//
// After each guest Resolve, the destination's level-0 pages are made
// inaccessible in the three guest CPU views of physical memory (0xA0000000,
// 0xC0000000, 0xE0000000). The GPU reads guest memory through the separate
// physical view, so only guest CPU accesses fault. The first access to each
// page is recorded with the host code address, then the page's protection is
// restored. Faults are logged every 600 presents (resolve_watch_ledger.h).

#pragma once

#include <cstdint>

namespace rex {
class Runtime;
}

namespace wwe::resolve_watch {

void Configure(rex::Runtime* runtime);

// After the guest's D3D Resolve: destination is the texture object.
void OnResolve(const uint8_t* base, uint32_t destination, uint64_t frame);

void LogPeriodic(uint64_t frame);

}  // namespace wwe::resolve_watch
