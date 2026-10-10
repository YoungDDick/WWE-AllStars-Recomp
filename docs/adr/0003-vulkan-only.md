# ADR-0003: Graphics API: Vulkan only

Status: accepted (2026-10-02)

## Context

In SVR2011, ReXGlue's D3D12 backend caused DEVICE_HUNG on an RX 7900 XT. Vulkan also covers Linux and Steam Deck.

## Decision

Vulkan only, for both Classic (FSI render-target path) and Native.

## Consequences

D3D12 is not planned.
