"""Disassemble a range of the TU2 guest image (analysis/xex/tu2.pe, build.ps1 -Action Image).

Usage: python scripts/disasm.py <start_hex> [count]
The image is the XEX's memory image, so file offset = address - 0x82000000.
IMAGE=analysis/xex/default.pe reads the base v5 image instead (tu_addrmap.py input).
"""
import sys
from pathlib import Path

import capstone

BASE = 0x82000000
import os
image = Path(os.environ.get("IMAGE", Path(__file__).resolve().parents[1] / "analysis/xex/tu2.pe"))
data = image.read_bytes()
start = int(sys.argv[1], 16)
count = int(sys.argv[2]) if len(sys.argv) > 2 else 24
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
offset = start - BASE
for addr in range(start, start + count * 4, 4):
    word = data[addr - BASE:addr - BASE + 4]
    insns = list(md.disasm(word, addr))
    text = f"{insns[0].mnemonic} {insns[0].op_str}" if insns else ".long 0x" + word.hex()
    print(f"{addr:08X}  {word.hex()}  {text}")
