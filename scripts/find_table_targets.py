"""List likely function entries that codegen has not discovered.

A candidate is a code address stored in data next to a known function start
(a vtable or callback-table neighbour), where the preceding instruction ends
a block (blr, unconditional b, or padding). Jump-table case labels are skipped:
their neighbours are case labels as well, not known function starts.

Usage: python scripts/find_table_targets.py [--toml]
Inputs: analysis/xex/tu2.pe (the TU2 memory image, build.ps1 -Action Image) and
the function addresses in generated/default/wweallstars_init.cpp.
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
TEXT_LO, TEXT_HI = 0x82410000, 0x83504018  # TU2 .text
# Guest SEH scope records ({count, begin, end, handler} per function) point at
# handler blocks inside their functions, which codegen already covers.
SEH_SCOPES_LO, SEH_SCOPES_HI = 0x82357DB0, 0x82358DB0

image = (ROOT / "analysis/xex/tu2.pe").read_bytes()
init = (ROOT / "generated/default/wweallstars_init.cpp").read_text(errors="replace")
known = {int(a, 16) for a in re.findall(r"\{ (0x8[0-9A-F]{7}), \w+ \}", init)}


def word(address):
    return struct.unpack_from(">I", image, address - BASE)[0]


def ends_block(insn):
    if insn in (0x4E800020, 0x4E800420, 0x00000000, 0x60000000):  # blr, bctr, padding, nop
        return True
    return (insn >> 26) == 18 and (insn & 1) == 0  # b / ba without link


candidates = {}
for offset in range(0, len(image) - 4, 4):
    address = BASE + offset
    if TEXT_LO <= address < TEXT_HI or SEH_SCOPES_LO <= address < SEH_SCOPES_HI:
        continue
    target = struct.unpack_from(">I", image, offset)[0]
    if not (TEXT_LO <= target < TEXT_HI) or target & 3 or target in known:
        continue
    neighbours = (address - 4, address + 4)
    if not any(BASE <= n < BASE + len(image) - 4 and word(n) in known for n in neighbours):
        continue
    if not ends_block(word(target - 4)):
        continue
    candidates.setdefault(target, []).append(address)

if "--toml" in sys.argv:
    for target in sorted(candidates):
        refs = ", ".join(f"0x{r:08X}" for r in candidates[target][:3])
        print(f"0x{target:08X} = {{}}  # table refs {refs}")
else:
    for target in sorted(candidates):
        print(f"{target:08X}  refs {' '.join(f'{r:08X}' for r in candidates[target][:4])}")
    print(f"{len(candidates)} candidates", file=sys.stderr)
