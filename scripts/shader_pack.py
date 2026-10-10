"""Write the shader pack the native renderer loads at startup.

XenosRecomp writes its converted shaders as C++ (a shader entry table and the
zstd-compressed SPIR-V as a byte array). This repacks them, with the fetch
slots each shader reads (scripts/shader_fetch_slots.py) and the converter ID,
into the binary pack src/native/shader_pack.h describes. Game-derived output,
never committed.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from shader_fetch_slots import ALL_SLOTS, fetch_slot_entries
else:
    from scripts.shader_fetch_slots import ALL_SLOTS, fetch_slot_entries

MAGIC = b"WWSP"
VERSION = 1
FILE_NAME = "wweallstars.shaderpack"

# { hash, dxilOffset, dxilSize, spirvOffset, spirvSize, specConstantsMask }
ENTRY = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}")
ENTRY_TABLE = re.compile(r"g_shaderCacheEntries\[\]\s*=\s*\{(.*?)\n\};", re.DOTALL)
SPIRV = re.compile(r"g_compressedSpirvCache\[\]\s*=\s*\{([^}]*)\}")
SIZE = re.compile(r"\b(g_spirvCacheCompressedSize|g_spirvCacheDecompressedSize)\s*=\s*(\d+)")


class XenosCache:
    """The parts of XenosRecomp's shader_cache.cpp the pack keeps."""

    def __init__(self, entries: list[tuple[int, int, int, int]], compressed: bytes,
                 decompressed_size: int):
        self.entries = entries  # (hash, spirv offset, spirv size, spec constants mask)
        self.compressed = compressed
        self.decompressed_size = decompressed_size


def parse_xenos_cache(text: str) -> XenosCache:
    table = ENTRY_TABLE.search(text)
    spirv = SPIRV.search(text)
    sizes = dict(SIZE.findall(text))
    if not table or not spirv or len(sizes) != 2:
        raise ValueError("not a XenosRecomp shader_cache.cpp")
    entries = [(int(h, 0), int(offset), int(size), int(mask))
               for h, _, _, offset, size, mask in ENTRY.findall(table.group(1))]
    compressed = bytes(int(b) for b in spirv.group(1).replace("\n", "").split(",") if b.strip())
    if len(compressed) != int(sizes["g_spirvCacheCompressedSize"]):
        raise ValueError("compressed SPIR-V size does not match its array")
    return XenosCache(sorted(entries), compressed, int(sizes["g_spirvCacheDecompressedSize"]))


def build_pack(cache: XenosCache, fetch_slots: list[tuple[int, int]], converter_id: int) -> bytes:
    out = bytearray(MAGIC)
    out += struct.pack("<IQIIII", VERSION, converter_id, len(cache.entries), len(fetch_slots),
                       len(cache.compressed), cache.decompressed_size)
    for shader_hash, offset, size, mask in cache.entries:
        out += struct.pack("<QIIII", shader_hash, offset, size, mask, 0)
    for fnv, mask in sorted(fetch_slots):
        out += struct.pack("<QII", fnv, mask, 0)
    out += cache.compressed
    return bytes(out)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Write the native renderer's shader pack.")
    parser.add_argument("shader_cache", type=Path, help="XenosRecomp's shader_cache.cpp")
    parser.add_argument("hlsl_dir", type=Path, help="XenosRecomp's HLSL output folder")
    parser.add_argument("converter_id", help="XenosRecomp patch ID, 16 hex digits")
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)

    cache = parse_xenos_cache(args.shader_cache.read_text(encoding="utf-8", errors="replace"))
    fetch_slots = fetch_slot_entries(args.hlsl_dir)
    pack = build_pack(cache, fetch_slots, int(args.converter_id, 16))
    temporary = args.output.with_name(args.output.name + ".tmp")
    temporary.write_bytes(pack)
    temporary.replace(args.output)
    unsure = sum(1 for _, mask in fetch_slots if mask == ALL_SLOTS)
    print(f"{len(cache.entries)} shaders, {len(fetch_slots)} fetch slot entries "
          f"({unsure} bind every slot), {len(pack)} bytes: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
