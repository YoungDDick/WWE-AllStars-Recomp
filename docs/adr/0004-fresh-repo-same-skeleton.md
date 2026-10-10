# ADR-0004: Codebase: fresh repo using SVR2011's skeleton

Status: accepted (2026-10-02)

## Context

The engine is THQ San Diego's UE3, unrelated to Yuke's engine, but the runtime patches, native renderer core and tooling are generic.

## Decision

This is a new repo with SVR2011's layout. It uses the same ReXGlue pin and copies the runtime patches and scripts. `src/native` is ported in M2. A shared library is extracted later, once both games show what is genuinely common.

## Consequences

Fixes to code copied from SVR2011 must be ported back by hand until the shared library exists.
