"""Disassemble a range of the TU2 guest image (analysis/xex/tu2.pe, build.ps1 -Action Image).

Usage: python scripts/disasm.py <start_hex> [count]
The image is the XEX's memory image, so file offset = address - 0x82000000.
IMAGE=analysis/xex/default.pe reads the base v5 image instead (tu_addrmap.py input).

Decoder: the Xenon binutils objdump in .tools/rexglue-source/tools/binutils with
-M cell,vmx128, which knows the VMX128 vector instructions. Capstone does not: it
prints .long for most of them and misreads some, e.g. lvx128 v63,r0,r7
(0x13E038C7) as vcmpequd. That objdump in turn drops a few plain AltiVec words
when vmx128 is on (vsubshs 0x10C86F40), so a word it leaves as .long goes to
Capstone; those lines end in "; capstone". objdump prints immediates in decimal,
Capstone in hex. Without the objdump, every word goes to Capstone, with a warning.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import capstone

BASE = 0x82000000
ROOT = Path(__file__).resolve().parents[1]
OBJDUMP = ROOT / ".tools/rexglue-source/tools/binutils/powerpc-none-elf-objdump.exe"
image = Path(os.environ.get("IMAGE", ROOT / "analysis/xex/tu2.pe"))
data = image.read_bytes()
start = int(sys.argv[1], 16)
count = int(sys.argv[2]) if len(sys.argv) > 2 else 24
chunk = data[start - BASE:start - BASE + count * 4]


def objdump(chunk, start):
    """Map address -> 'mnemonic operands' from the Xenon objdump; {} when it is missing."""
    if not OBJDUMP.exists():
        print(f"warning: {OBJDUMP} missing, VMX128 words will not decode", file=sys.stderr)
        return {}
    with tempfile.TemporaryDirectory() as tmp:
        # A Cygwin build: a bare file name in cwd avoids its warning about C:/ paths.
        Path(tmp, "range.bin").write_bytes(chunk)
        out = subprocess.run(
            [str(OBJDUMP), "-D", "-b", "binary", "-m", "powerpc:common64", "-EB",
             "-M", "cell,vmx128", f"--adjust-vma={start:#x}", "range.bin"],
            cwd=tmp, capture_output=True, text=True, check=True).stdout
    insns = {}
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\t[0-9a-f ]+\t(.*)$", line)
        if m:
            text = re.sub(r"\s*<[^>]*>$", "", m.group(2)).split(None, 1)
            insns[int(m.group(1), 16)] = " ".join(text)
    return insns


decoded = objdump(chunk, start)
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
for addr in range(start, start + len(chunk), 4):
    word = data[addr - BASE:addr - BASE + 4]
    text = decoded.get(addr, ".long")
    if text.startswith(".long"):
        insns = list(md.disasm(word, addr))
        text = f"{insns[0].mnemonic} {insns[0].op_str}; capstone" if insns else ".long 0x" + word.hex()
    print(f"{addr:08X}  {word.hex()}  {text}")
