# ADR-0009: Title update: latest TU, base fallback

Status: accepted (2026-10-02). Carried out 2026-10-10; the consequence below is
superseded by ADR-0022 (addresses move by script, no byte signatures).

## Context

xboxunity lists TU1 and TU2 for media 60CEF505 (`docs/research/title-update.md`). TUs are copyrighted and cannot be redistributed.

## Decision

Build on the base v5 XEX until a player-supplied TU2 can be verified by hash. Then rebase onto the TU-patched XEX, moving manifest seeds and hooks to the new addresses.

## Consequences

Hook addresses must be recorded with byte signatures, not bare addresses, so they can be moved.
