"""Which texture fetch slots each converted shader reads, from XenosRecomp's HLSL.

The native renderer binds a texture for every valid fetch constant unless it
knows the slots a draw's shaders use. XenosRecomp names each sampler slot with
defines like

    #define sampDiffuse_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 20)

(offset / 4 = dimension * 32 + slot), and the shader body refers to those
names. scripts/shader_pack.py stores one entry per shader in the shader pack:
the FNV-1a hash in its file name (the one --wwe_shader_dump_dir uses) and a
32-bit mask of the slots its body references. Game-derived output, never
committed.
"""

from __future__ import annotations

import re
from pathlib import Path

SLOTS = 32
ALL_SLOTS = (1 << SLOTS) - 1

DEFINE = re.compile(
    r"^#define (\w+)_(?:Texture\w+|Sampler)DescriptorIndex "
    r"vk::RawBufferLoad<uint>\(g_PushConstants\.SharedConstants \+ (\d+)\)",
    re.MULTILINE,
)
REFERENCE = re.compile(r"\b(\w+)_(?:Texture(?:2D|3D|Cube)|Sampler)DescriptorIndex\b")
FILE_NAME = re.compile(r"^(?:ps|vs)_([0-9a-f]{16})\.hlsl$")


def slot_mask(hlsl: str) -> int:
    """Mask of the fetch slots the shader body uses; all slots if unsure."""
    slots: dict[str, int] = {}
    for name, offset in DEFINE.findall(hlsl):
        slots[name] = (int(offset) // 4) % SLOTS
    mask = 0
    for line in hlsl.splitlines():
        stripped = line.strip()
        if stripped.startswith("#define") or "packoffset" in stripped:
            continue
        for name in REFERENCE.findall(line):
            if name not in slots:
                return ALL_SLOTS
            mask |= 1 << slots[name]
    return mask


def fetch_slot_entries(hlsl_dir: Path) -> list[tuple[int, int]]:
    """(FNV-1a container hash, slot mask) for each converted shader, sorted."""
    entries = []
    for path in sorted(hlsl_dir.glob("*.hlsl")):
        match = FILE_NAME.match(path.name)
        if not match:
            continue
        mask = slot_mask(path.read_text(encoding="utf-8", errors="replace"))
        entries.append((int(match.group(1), 16), mask))
    entries.sort()
    return entries
