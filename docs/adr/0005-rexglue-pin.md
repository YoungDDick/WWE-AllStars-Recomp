# ADR-0005: Toolchain: ReXGlue v0.10.0 and LLVM 21.1.8

Status: accepted (2026-10-02)

## Context

SVR2011's patches target rexglue-sdk commit f5337cdc.

## Decision

Pin the same SDK, source revision and LLVM. Upgrade both projects together.

## Consequences

Development machines can link `.tools/` to SVR2011's toolchain. `rexglue-source` is a separate copy because its patch set differs.
