# ADR-0002: Renderer path: Classic first, then Native D3D9 hooks

Status: accepted (2026-10-02)

## Context

SVR2011-Recomp showed that hooking the statically linked XDK D3D9 library works (`src/native`, PM4-builder scan, draw census). All Stars links D3D9 statically too (XDK 2.0.11775.3).

## Decision

Boot on ReXGlue's emulated Xenos renderer (Classic). Native work starts as soon as the main menu renders, by hooking D3D9 and drawing to Vulkan. Classic stays as a player fallback and as the debugging reference for Native. Native becomes the default only at full parity on the test matrix.

## Consequences

Both renderers must keep building, and frame comparison between them is part of the toolset.
