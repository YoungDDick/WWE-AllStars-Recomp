# ADR-0015: Native renderer architecture: hybrid

Status: accepted (2026-10-02)

## Context

On the main menu, SVR2011-style D3D hooks see about 16 of the roughly 83
draws per present. The rest arrive through indirect buffers (command
buffers) that no hooked D3D call writes into the device ring
(`docs/research/native-renderer.md`).

## Decision

Use a hybrid native renderer:

- Draws issued through the device ring are taken from the D3D hooks, as in
  SVR2011.
- Indirect buffers those hooks do not cover are decoded at the PM4 level
  (register writes plus DRAW packets).
- Both sources feed the same Vulkan backend in `src/native` (pipelines,
  textures, shaders, native render targets).

Static buffers are decoded once and cached by address and content hash.

## Consequences

- Every draw avoids EDRAM emulation, so performance is expected to be at
  least as good as Classic. Decoding about 67 draws per present costs
  microseconds.
- The draw count during gameplay must be measured before the design is
  final.
- The main risk is correctness: register state has to stay consistent
  between the two sources. Frame comparison against Classic (ADR-0002)
  catches divergences.

## Amendment (2026-10-03): D3D hooks first

The black-screen fix showed that draws we had blamed on indirect buffers came
from D3D user-memory draw paths (DrawVerticesUP, BeginIndexedVertices,
DrawIndexedVerticesUP) that our hooks were missing. wwe-svr2011-pc
(`docs/research/svr2011-pc-port.md`) ships a Native default on D3D-level hooks
alone. The draws missed in a match are therefore covered by finding further D3D
entry points first. PM4-level decoding is built only for a draw source that a
match draw census shows has no hookable D3D path. The two-source design above
stays the fallback plan.

## Outcome (2026-10-03, tickets 04 and 05)

A full-match draw census found no draw source without a hookable D3D entry
point. The PM4 path is not built; the two-source design stays on paper as the
fallback if a later mode or scene shows such a source.
