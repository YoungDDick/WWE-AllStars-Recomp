"""Map hard-coded guest addresses from the base v5 image to the Title Update 2 image.

Usage: python scripts/tu_addrmap.py [--base IMG] [--tu2 IMG] [--write] [--report FILE] PATH...

TU2 relinks the exe: nearly every function moves (ADR-0022). This script finds
every `0x8…`, `sub_8…`, `loc_8…` and bare upper-case `8…` address in PATH (files or folders) and maps
it from the base image to the TU2 image. Methods, strongest first:

- function: the address is in a `.pdata` function matched by code with branch
  and address immediates masked; the offset inside the function is kept.
  `function*` means the function matched but some non-address operand
  (a struct offset or constant) differs, so a hook there needs a look.
- data-ref: a data address learned from `lis`/`addi` (or load/store) pairs at
  the same place in matched code.
- local: the bytes at the address match the TU2 image at a delta taken from
  neighbouring matches, or at one unique place in `.text`. Code compares the
  instructions up to the first b/blr/bctr (at most 32 bytes) with branch
  targets followed through the map and other operands equal, except the halves
  of an address; data compares 32 bytes with pointers mapped.
- via-table: code reached only through pointer tables (vtable thunks): every
  table slot holding it maps to a TU2 slot, and those slots agree.

Without --write it only reports. With --write it rewrites the mapped addresses
in place and prints the ones it could not map. Images default to
analysis/xex/default.pe (base) and analysis/xex/tu2.pe (`build.ps1 -Action Image`).
"""
from __future__ import annotations

import argparse
import bisect
import collections
import difflib
import hashlib
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

BASE = 0x82000000
ROOT = Path(__file__).resolve().parents[1]
# 0x/sub_/loc_ addresses, and bare upper-case ones such as hook macro arguments
# (WWE_PROBE_HOOK(82DB22E0, ...)). sub_ may follow an underscore (__imp__sub_...,
# the original a hook calls); nothing else matches inside identifiers or longer numbers.
ADDRESS = re.compile(r"(?:(?<![0-9A-Za-z])(sub_|loc_)|(?<![0-9A-Za-z_])(0x|))"
                     r"(8[23][0-9A-Fa-f]{6})(?![0-9A-Za-z_])")
SOURCE_SUFFIXES = (".cpp", ".h", ".inc", ".toml", ".py", ".go")
LOCAL_BYTES = 32


@dataclass
class Image:
    data: bytes
    functions: dict[int, int]  # .pdata start -> length in bytes
    sections: list[tuple[str, int, int]]  # name, start address, size
    starts: list[int] = field(default_factory=list)

    def __post_init__(self):
        self.starts = sorted(self.functions)

    @property
    def end(self) -> int:
        return BASE + len(self.data)

    def text(self) -> tuple[int, int]:
        _, start, size = next(s for s in self.sections if s[0] == ".text")
        return start, start + size

    def function_at(self, address: int) -> int | None:
        i = bisect.bisect_right(self.starts, address) - 1
        if i >= 0 and address < self.starts[i] + self.functions[self.starts[i]]:
            return self.starts[i]
        return None

    def word(self, address: int) -> int:
        o = address - BASE
        return struct.unpack(">I", self.data[o:o + 4])[0]


def load_image(path: Path) -> Image:
    data = Path(path).read_bytes()
    pe = struct.unpack("<I", data[0x3C:0x40])[0]
    count = struct.unpack("<H", data[pe + 6:pe + 8])[0]
    optional = struct.unpack("<H", data[pe + 20:pe + 22])[0]
    exception_va, exception_size = struct.unpack("<II", data[pe + 24 + 96 + 24:pe + 24 + 96 + 32])
    functions = {}
    for i in range(0, exception_size, 8):
        start, info = struct.unpack(">II", data[exception_va + i:exception_va + i + 8])
        if start:
            functions[start] = ((info >> 8) & 0x3FFFFF) * 4
    sections = []
    table = pe + 24 + optional
    for i in range(count):
        entry = data[table + 40 * i:table + 40 * i + 40]
        size, va = struct.unpack("<II", entry[8:16])
        sections.append((entry[:8].rstrip(b"\0").decode(), BASE + va, size))
    return Image(data, functions, sections)


def mask(word: int) -> int:
    """An instruction with its branch displacement or 16-bit immediate cleared."""
    op = word >> 26
    if op == 18:
        return word & 0xFC000003
    if op == 16:
        return word & 0xFFFF0003
    if op in (14, 15, 24, 25) or 32 <= op <= 55:
        return word & 0xFFFF0000
    return word


def normalized(image: Image, address: int, length: int) -> bytes:
    o = address - BASE
    words = struct.unpack(f">{length // 4}I", image.data[o:o + length - length % 4])
    return struct.pack(f">{len(words)}I", *map(mask, words))


def code_window(image: Image, address: int) -> int:
    """Bytes from address through the first b/blr/bctr, at most LOCAL_BYTES."""
    for offset in range(0, LOCAL_BYTES, 4):
        word = image.word(address + offset)
        if (word >> 26 == 18 and not word & 1) or word in (0x4E800020, 0x4E800420):
            return offset + 4
    return LOCAL_BYTES


@dataclass
class FunctionMatch:
    map: dict[int, int]  # base start -> TU2 start
    changed: set[int]  # base starts matched by slot whose code differs


def match_functions(base: Image, tu2: Image) -> FunctionMatch:
    """Pair .pdata functions: unique masked-code hashes anchor, difflib fills the gaps."""
    def hashes(image):
        return [hashlib.sha1(normalized(image, a, image.functions[a])).digest() for a in image.starts]

    bh, th = hashes(base), hashes(tu2)
    bcount, tcount = collections.Counter(bh), collections.Counter(th)
    tindex = {h: j for j, h in enumerate(th) if tcount[h] == 1}
    candidates = [(i, tindex[h]) for i, h in enumerate(bh) if bcount[h] == 1 and h in tindex]
    anchors = longest_increasing(candidates)

    pairs, changed = {}, set()
    previous = (-1, -1)
    for i, j in anchors + [(len(bh), len(th))]:
        i1, j1 = previous[0] + 1, previous[1] + 1
        matcher = difflib.SequenceMatcher(None, bh[i1:i], th[j1:j], autojunk=False)
        for tag, a1, a2, b1, b2 in matcher.get_opcodes():
            if tag == "equal" or (tag == "replace" and a2 - a1 == b2 - b1):
                for k in range(a2 - a1):
                    pairs[i1 + a1 + k] = j1 + b1 + k
                    if tag == "replace":
                        changed.add(base.starts[i1 + a1 + k])
        if i < len(bh):
            pairs[i] = j
        previous = (i, j)
    return FunctionMatch({base.starts[i]: tu2.starts[j] for i, j in pairs.items()}, changed)


def longest_increasing(pairs: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """The longest chain of pairs increasing in both coordinates (pairs sorted by the first)."""
    tails, tail_index, parent = [], [], [-1] * len(pairs)
    for k, (_, j) in enumerate(pairs):
        p = bisect.bisect_left(tails, j)
        if p == len(tails):
            tails.append(j)
            tail_index.append(k)
        else:
            tails[p] = j
            tail_index[p] = k
        parent[k] = tail_index[p - 1] if p else -1
    chain, k = [], tail_index[-1] if tail_index else -1
    while k >= 0:
        chain.append(pairs[k])
        k = parent[k]
    return chain[::-1]


def address_refs(image: Image, start: int, length: int) -> list[int | None]:
    """Per instruction: the absolute address a lis-based addi/load/store forms, else None."""
    high, out = {}, []
    for address in range(start, start + length, 4):
        word = image.word(address)
        op, rt, ra, imm = word >> 26, (word >> 21) & 31, (word >> 16) & 31, word & 0xFFFF
        if op == 15 and ra == 0:
            high[rt] = imm << 16
            out.append(None)
            continue
        if (op == 14 or 32 <= op <= 55) and ra and ra in high:
            out.append((high[ra] + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF)
        else:
            out.append(None)
        if (op in (14, 15) or 32 <= op <= 47) and rt in high and rt != ra:
            del high[rt]
    return out


def learn_data_refs(base: Image, tu2: Image, functions: FunctionMatch) -> dict[int, int]:
    votes = collections.defaultdict(collections.Counter)
    for b, t in functions.map.items():
        if base.functions[b] != tu2.functions[t] or b in functions.changed:
            continue
        for x, y in zip(address_refs(base, b, base.functions[b]), address_refs(tu2, t, tu2.functions[t])):
            if x is not None and y is not None and BASE <= x < base.end and BASE <= y < tu2.end:
                votes[x][y] += 1
    return {x: c.most_common(1)[0][0] for x, c in votes.items()}


def operands_differ(base: Image, tu2: Image, b: int, t: int) -> int:
    """Instructions whose non-address operands differ between two matched functions."""
    length = base.functions[b]
    assert tu2.functions[t] == length, "functions matched by masked code have equal lengths"
    base_refs, tu2_refs = address_refs(base, b, length), address_refs(tu2, t, length)
    count = 0
    for k, offset in enumerate(range(0, length, 4)):
        x, y = base.word(b + offset), tu2.word(t + offset)
        if x == y or x >> 26 in (16, 18):
            continue
        if b <= x < b + length and y - x == t - b:
            continue  # a jump-table entry inside the function
        if x >> 26 == 15 and (x >> 16) & 31 == 0:  # lis: the high half of an address
            continue
        if base_refs[k] is not None and tu2_refs[k] is not None:
            continue
        count += 1
    return count


@dataclass
class Mapping:
    old: int
    new: int | None
    method: str
    note: str = ""


class AddressMap:
    def __init__(self, base: Image, tu2: Image):
        self.base, self.tu2 = base, tu2
        self.functions = match_functions(base, tu2)
        self.data_refs = learn_data_refs(base, tu2, self.functions)
        self.data_keys = sorted(self.data_refs)
        self.function_keys = sorted(self.functions.map)
        self._text_norm = None
        self._pointer_cache: dict[int, int | None] = {}
        self._depth = 0

    def map(self, address: int) -> Mapping:
        if not BASE <= address < self.base.end:
            return Mapping(address, None, "outside image")
        start = self.base.function_at(address)
        if start is not None and start in self.functions.map:
            target = self.functions.map[start]
            if start not in self.functions.changed:
                differ = operands_differ(self.base, self.tu2, start, target)
                note = f"{differ} operand(s) differ in sub_{start:08X}" if differ else ""
                return Mapping(address, target + address - start, "function*" if differ else "function", note)
            if address == start:
                return Mapping(address, target, "function*", "matched by slot, code changed")
        if address in self.data_refs:
            return Mapping(address, self.data_refs[address], "data-ref")
        local = self._local(address, start)
        if local is not None:
            return local
        if self._in_text(address):
            table = self._via_table(address)
            if table is not None:
                return table
        return Mapping(address, None, "unmapped", "changed function" if start in self.functions.changed else "")

    def _in_text(self, address: int) -> bool:
        lo, hi = self.base.text()
        return lo <= address < hi

    def _same(self, address: int, target: int) -> bool:
        """32 bytes match: code with immediates masked, data with pointers mapped."""
        if not BASE <= target <= self.tu2.end - LOCAL_BYTES or address > self.base.end - LOCAL_BYTES:
            return False
        if self.base.data[address - BASE:address - BASE + LOCAL_BYTES] == bytes(LOCAL_BYTES):
            return False
        if self._in_text(address):
            return self._same_code(address, target)
        unchecked = 0
        for k in range(0, LOCAL_BYTES, 4):
            x, y = self.base.word(address + k), self.tu2.word(target + k)
            if x == y or self.data_refs.get(x) == y or self._same_leaf(x, y):
                continue
            mapped = self._code_pointer(x)
            if mapped == y:
                continue
            lo, hi = self.tu2.text()
            if mapped is None and self._in_text(x) and lo <= y < hi:
                unchecked += 1  # a code pointer we cannot map either way
                continue
            return False
        return unchecked <= 1

    def _same_function(self, a: int, b: int) -> bool:
        return self.base.function_at(a) is not None and self.base.function_at(a) == self.base.function_at(b)

    def _same_leaf(self, x: int, y: int) -> bool:
        """Two code pointers to byte-identical leaf code without branches (shared stubs)."""
        lo, hi = self.tu2.text()
        if not (self._in_text(x) and lo <= y < hi) or x % 4 or y % 4:
            return False
        length = code_window(self.base, x)
        if length == LOCAL_BYTES or any(self.base.word(x + k) >> 26 in (16, 18) for k in range(0, length, 4)):
            return False
        return self.base.data[x - BASE:x - BASE + length] == self.tu2.data[y - BASE:y - BASE + length]

    def _same_code(self, address: int, target: int) -> bool:
        """Same instructions: branches must reach matched targets, other operands must be
        equal except the halves of an address formed inside the window."""
        length = code_window(self.base, address)
        base_refs = address_refs(self.base, address, length)
        tu2_refs = address_refs(self.tu2, target, length)
        for k, offset in enumerate(range(0, length, 4)):
            x, y = self.base.word(address + offset), self.tu2.word(target + offset)
            if x == y and x >> 26 not in (16, 18):
                continue
            if mask(x) != mask(y):
                return False
            op = x >> 26
            if op in (16, 18):
                if x & 2:  # absolute branch
                    if x != y:
                        return False
                    continue
                width = 0xFFFC if op == 16 else 0x3FFFFFC
                sign = 0x8000 if op == 16 else 0x2000000
                def reach(word, at):
                    d = word & width
                    return at + (d - 2 * sign if d & sign else d)
                bt, tt = reach(x, address + offset), reach(y, target + offset)
                if bt - (address + offset) == tt - (target + offset) and self._same_function(address, bt):
                    continue  # a branch inside the same function, same distance
                if bt - (address + offset) == tt - (target + offset) and self._same_leaf(bt, tt):
                    continue  # a tail branch to a nearby shared stub, same distance
                if self._depth > 3 or self._code_pointer(bt) != tt:
                    return False
                continue
            if op == 15 and (x >> 16) & 31 == 0:
                continue  # lis: the high half of an address
            if base_refs[k] is not None and tu2_refs[k] is not None:
                continue
            return False
        return True

    def _distinctive(self, address: int) -> bool:
        length = code_window(self.base, address)
        last = self.base.word(address + length - 4)
        return length >= 16 or (last >> 26 == 18 and length >= 8)

    def _code_pointer(self, pointer: int) -> int | None:
        """Where a code pointer stored in data points in TU2 (function match or local code match)."""
        if not self._in_text(pointer) or pointer % 4:
            return None
        if pointer not in self._pointer_cache:
            self._pointer_cache[pointer] = None  # a cycle reads as unmapped
            self._depth += 1
            start = self.base.function_at(pointer)
            if start in self.functions.map and start not in self.functions.changed:
                self._pointer_cache[pointer] = self.functions.map[start] + pointer - start
            else:
                local = self._local(pointer, start)
                self._pointer_cache[pointer] = local.new if local else None
            self._depth -= 1
        return self._pointer_cache[pointer]

    def _local(self, address: int, start: int | None) -> Mapping | None:
        deltas = []
        if start is not None and start in self.functions.map:
            deltas.append(self.functions.map[start] - start)
        for keys, table in ((self.data_keys, self.data_refs), (self.function_keys, self.functions.map)):
            j = bisect.bisect_right(keys, address)
            deltas += [table[k] - k for k in keys[max(0, j - 4):j + 4]]
        if self._in_text(address) and not self._distinctive(address):
            deltas = []  # a tiny stub has copies everywhere: only a unique match counts
        hits = [address + d for d in dict.fromkeys(deltas) if self._same(address, address + d)]
        if len(hits) == 1:
            return Mapping(address, hits[0], "local")
        if hits:
            return None  # ambiguous: several neighbouring deltas fit
        if self._in_text(address) and address % 4 == 0:
            target = self._unique_in_text(address)
            if target is not None:
                return Mapping(address, target, "local", "unique in .text")
        return None

    def _via_table(self, address: int) -> Mapping | None:
        """Code reached only through pointer tables: read the mapped slots in the TU2 image."""
        needle = struct.pack(">I", address)
        targets, at = set(), self.base.data.find(needle)
        while at >= 0:
            slot = BASE + at
            if at % 4 == 0 and not self._in_text(slot):
                mapped = self.data_refs.get(slot)
                if mapped is None:
                    local = self._local(slot, None)
                    mapped = local.new if local else None
                if mapped is not None:
                    targets.add(self.tu2.word(mapped))
            at = self.base.data.find(needle, at + 1)
        lo, hi = self.tu2.text()
        if len(targets) == 1 and lo <= (target := targets.pop()) < hi:
            return Mapping(address, target, "via-table", "the TU2 slots of its table refs point here")
        return None

    def _unique_in_text(self, address: int) -> int | None:
        lo, hi = self.tu2.text()
        if self._text_norm is None:
            self._text_norm = normalized(self.tu2, lo, hi - lo)
        needle = normalized(self.base, address, code_window(self.base, address))
        if not any(needle):
            return None
        found, at = [], self._text_norm.find(needle)
        while at >= 0:
            if at % 4 == 0 and self._same_code(address, lo + at):
                found.append(lo + at)
                if len(found) > 1:
                    return None
            at = self._text_norm.find(needle, at + 1)
        return found[0] if found else None


def source_files(paths: list[Path]) -> list[Path]:
    files = []
    for path in paths:
        if path.is_file():
            files.append(path)
        else:
            # *.gen.cpp files are regenerated from the generated code (gen_d3d_sweep.py).
            files += sorted(p for p in path.rglob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES
                            and not p.name.endswith(".gen.cpp"))
    return files


def split(match) -> tuple[str, str]:
    """An ADDRESS match as (prefix, hex digits)."""
    return match.group(1) or match.group(2) or "", match.group(3)


def is_address(match) -> bool:
    """A bare lower-case run is more likely a hash than an address."""
    prefix, digits = split(match)
    return bool(prefix) or digits == digits.upper()


def find_addresses(files: list[Path]) -> dict[int, set[str]]:
    found = collections.defaultdict(set)
    for path in files:
        for match in ADDRESS.finditer(path.read_bytes().decode("utf-8")):
            if not is_address(match):
                continue
            found[int(split(match)[1], 16)].add(path.as_posix())
    return found


def rewrite(text: str, mapped: dict[int, int]) -> str:
    """Replace every mapped address in one pass, keeping its prefix and hex case."""
    def replace(match):
        prefix, digits = split(match)
        old = int(digits, 16)
        if old not in mapped or not is_address(match):
            return match.group(0)
        new = f"{mapped[old]:08x}"
        if not any(c in "abcdef" for c in digits):
            new = new.upper()
        return prefix + new
    return ADDRESS.sub(replace, text)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Map guest addresses from the base image to TU2.")
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--base", type=Path, default=ROOT / "analysis/xex/default.pe")
    parser.add_argument("--tu2", type=Path, default=ROOT / "analysis/xex/tu2.pe")
    parser.add_argument("--write", action="store_true", help="rewrite mapped addresses in place")
    parser.add_argument("--report", type=Path, help="write the full report (Markdown) here")
    args = parser.parse_args(argv)

    amap = AddressMap(load_image(args.base), load_image(args.tu2))
    files = source_files(args.paths)
    found = find_addresses(files)
    mappings = [amap.map(a) for a in sorted(found)]
    unmapped = [m for m in mappings if m.new is None]
    counts = collections.Counter(m.method for m in mappings)

    print(f"{len(amap.functions.map)} functions matched ({len(amap.functions.changed)} changed), "
          f"{len(amap.data_refs)} data refs learned")
    print(f"{len(mappings)} distinct addresses in {len(files)} files: " +
          ", ".join(f"{n} {k}" for k, n in counts.most_common()))
    for m in unmapped:
        print(f"  {m.method}: 0x{m.old:08X} {m.note} in {', '.join(sorted(found[m.old]))}")

    if args.report:
        lines = ["| base | TU2 | method | note | files |", "|---|---|---|---|---|"]
        for m in mappings:
            new = f"0x{m.new:08X}" if m.new is not None else "-"
            lines.append(f"| 0x{m.old:08X} | {new} | {m.method} | {m.note} | "
                         f"{', '.join(sorted(Path(f).name for f in found[m.old]))} |")
        args.report.write_text("\n".join(lines) + "\n", encoding="utf-8")

    if args.write:
        table = {m.old: m.new for m in mappings if m.new is not None}
        for path in files:
            text = path.read_bytes().decode("utf-8")
            new = rewrite(text, table)
            if new != text:
                path.write_bytes(new.encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
