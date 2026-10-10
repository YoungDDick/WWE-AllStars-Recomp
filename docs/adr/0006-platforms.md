# ADR-0006: Platforms: Windows first, then Linux/Deck

Status: accepted (2026-10-02)

## Decision

Windows x64 is the development target. Linux and Steam Deck arrive at beta (M6). No macOS.

## Consequences

Code must stay portable: no Win32-only APIs outside platform layers.
