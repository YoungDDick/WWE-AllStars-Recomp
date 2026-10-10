"""Match functions from a reference recomp (SVR2011) into this game's image.

Each function is reduced to a masked signature: the top 16 bits of every
instruction (opcode and registers), with branch displacements and immediates
dropped, since relocated addresses differ between the two links. A reference
function matches when exactly one same-length function here has an identical
signature.

Usage: python scripts/match_functions.py <ref_image.pe> <ref_init.cpp> <lo> <hi>
       (lo/hi: hex bounds of the reference range to match)
Writes analysis/function-matches.tsv: ref_address, our_address, size, status.
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000


def starts(init_path):
    text = Path(init_path).read_text(errors="replace")
    return sorted({int(a, 16) for a in re.findall(r"\{ (0x8[0-9A-F]{7}), \w+ \}", text)})


def signature(image, start, end):
    words = []
    for address in range(start, end, 4):
        insn = int.from_bytes(image[address - BASE:address - BASE + 4], "big")
        opcode = insn >> 26
        if opcode in (16, 18):  # bc, b: keep opcode, BO/BI and link bits
            words.append((insn & 0xFFFF0003) if opcode == 16 else (insn & 0xFC000003))
        else:
            words.append(insn & 0xFFFF0000)
    return tuple(words)


def spans(addresses, text_end):
    return [(a, b) for a, b in zip(addresses, addresses[1:] + [text_end])]


def main():
    ref_image = Path(sys.argv[1]).read_bytes()
    ref_starts = starts(sys.argv[2])
    lo, hi = int(sys.argv[3], 16), int(sys.argv[4], 16)
    image = (ROOT / "analysis/xex/tu2.pe").read_bytes()
    ours = starts(ROOT / "generated/default/wweallstars_init.cpp")

    by_signature = defaultdict(list)
    for a, b in spans(ours, 0x83504018):  # end of TU2 .text
        if b - a <= 0x4000:
            by_signature[signature(image, a, b)].append(a)

    rows, unique = [], 0
    for a, b in spans(ref_starts, hi):
        if not (lo <= a < hi) or b - a > 0x4000:
            continue
        hits = by_signature.get(signature(ref_image, a, b), [])
        status = "unique" if len(hits) == 1 else ("none" if not hits else f"ambiguous:{len(hits)}")
        unique += len(hits) == 1
        rows.append(f"{a:08X}\t{hits[0] if len(hits) == 1 else 0:08X}\t{b - a:X}\t{status}")
    out = ROOT / "analysis/function-matches.tsv"
    out.write_text("ref\tours\tsize\tstatus\n" + "\n".join(rows) + "\n")
    print(f"{len(rows)} reference functions, {unique} unique matches -> {out}")


if __name__ == "__main__":
    main()
