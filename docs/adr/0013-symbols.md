# ADR-0013: Symbols: Ghidra plus signature matching

Status: accepted (2026-10-02)

## Context

There is no PDB, but UE3 class and native tables and the XDK, Havok, FMOD and Scaleform libraries can all be matched.

## Decision

A Ghidra project is the source of truth for names, which are exported into hook headers.
