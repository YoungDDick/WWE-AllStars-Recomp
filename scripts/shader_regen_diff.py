"""Translate every dumped shader container with two XenosRecomp builds and
report which HLSL outputs differ and which fail DXC.

Used to check XenosRecomp patch changes (tickets 13 and 14): a fix should
change exactly the shaders it targets and leave the DXC failures alone.

    python scripts/shader_regen_diff.py OLD.exe NEW.exe [--show]

A baseline OLD.exe is the tool built from the patch at HEAD in a second build
directory: with the working tree's source changes set aside (the patch file
regenerated or stashed), `pwsh scripts/build-xenosrecomp.ps1 -BuildDirectory
out/build/xenosrecomp-base`.

Inputs are the union of cache/guest-shaders* (minus -rejected), by file name.
The known-bad containers listed in cache/guest-shaders-rejected decode as
garbage: their output differs from run to run even with one tool, and they
make the tool crash now and then in a batch. Each runs alone, with retries,
and their changes are listed apart. EXCLUDED always crashes and is skipped.
Everything written goes to a temporary directory; nothing in the repo changes.
"""

import argparse
import difflib
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
COMMON_HEADER = ROOT / ".tools/xenosrecomp/XenosRecomp/shader_common.h"
REJECTED = ROOT / "cache/guest-shaders-rejected"
# vs_e20973377b9b2417 crashes XenosRecomp (docs/research/xenosrecomp-fixes.md).
EXCLUDED = {"vs_e20973377b9b2417"}
CRASH_RETRIES = 3
# XenosRecomp's note for a co-issue hazard it could not reorder (ticket 13).
UNFIXED_HAZARD = "co-issue hazard left in order"
FAILURE = re.compile(r"hash=0x([0-9A-F]{16}) reason=(\S+)")
HEADER_HASH = re.compile(r"hash=0x([0-9A-F]{16})")


def gather(dest: pathlib.Path) -> list[pathlib.Path]:
    """Copy the union of dumped containers into dest, apart from the known-bad
    ones, which are returned for single runs."""
    dest.mkdir(parents=True)
    known_bad = {path.name for path in REJECTED.glob("*.bin")}
    singles = []
    names = set()
    for folder in sorted(ROOT.glob("cache/guest-shaders*")):
        if folder == REJECTED:
            continue
        for container in folder.glob("*.bin"):
            if container.stem in EXCLUDED or container.name in names:
                continue
            names.add(container.name)
            if container.name in known_bad:
                singles.append(container)
            else:
                shutil.copy2(container, dest / container.name)
    return singles


def run_tool(tool: pathlib.Path, inputs: pathlib.Path, work: pathlib.Path):
    """One tool run; returns (stderr, crashed)."""
    work.mkdir(parents=True)  # the tool fail-fasts if it cannot write its cache file
    result = subprocess.run(
        [str(tool), str(inputs), str(work / "shader_cache.cpp"), str(COMMON_HEADER),
         str(work / "hlsl")],
        capture_output=True, text=True, cwd=tool.parent)
    return result.stderr, result.returncode not in (0, 2)  # 2 = finished with failures


def collect(work: pathlib.Path, stderr: str):
    """({name: hlsl}, {name: failure reason}) from one finished run."""
    outputs = {path.stem: path.read_text(encoding="utf-8")
               for path in (work / "hlsl").glob("*.hlsl")}
    by_hash = {}
    for name, text in outputs.items():
        match = HEADER_HASH.search(text.split("\n", 1)[0])
        if match:
            by_hash[match.group(1)] = name
    failures = {}
    for hash_hex, reason in FAILURE.findall(stderr):
        failures[by_hash.get(hash_hex, "0x" + hash_hex)] = reason
    return outputs, failures


def translate(tool: pathlib.Path, inputs: pathlib.Path, singles: list[pathlib.Path],
              work: pathlib.Path):
    """The batch plus each single; a single that keeps crashing is a failure.
    Also returns how many hazards the tool left in order (batch only)."""
    stderr, crashed = run_tool(tool, inputs, work / "batch")
    if crashed:
        sys.exit(f"{tool} crashed on the batch:\n{stderr[-2000:]}")
    unfixed = stderr.count(UNFIXED_HAZARD)
    outputs, failures = collect(work / "batch", stderr)
    for container in singles:
        for attempt in range(CRASH_RETRIES):
            single = work / f"{container.stem}-{attempt}"
            (single / "in").mkdir(parents=True)
            shutil.copy2(container, single / "in" / container.name)
            stderr, crashed = run_tool(tool, single / "in", single / "out")
            if not crashed:
                more, more_failures = collect(single / "out", stderr)
                outputs.update(more)
                failures.update(more_failures)
                break
        else:
            failures[container.stem] = f"crashed-{CRASH_RETRIES}-times"
    return outputs, failures, unfixed


def body(text: str) -> list[str]:
    # The first line carries the container hash and spec-constant mask, which
    # never differ between tools for the same input.
    return text.splitlines()[1:]


def main() -> int:
    parser = argparse.ArgumentParser(description="Diff two XenosRecomp builds over the dumped shaders.")
    parser.add_argument("old", type=pathlib.Path, help="baseline XenosRecomp.exe")
    parser.add_argument("new", type=pathlib.Path, help="patched XenosRecomp.exe")
    parser.add_argument("--show", action="store_true", help="print a unified diff per changed shader")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="shader-regen-") as temp:
        temp = pathlib.Path(temp)
        singles = gather(temp / "in")
        count = len(list((temp / "in").glob("*.bin"))) + len(singles)
        old, old_failures, _ = translate(args.old.resolve(), temp / "in", singles, temp / "old")
        new, new_failures, new_unfixed = translate(args.new.resolve(), temp / "in", singles, temp / "new")

    print(f"containers: {count}, {len(singles)} of them run alone "
          f"(excluded: {', '.join(sorted(EXCLUDED))})")
    print(f"translated: old {len(old)}, new {len(new)}")
    for label, names in (("only in old", old.keys() - new.keys()),
                         ("only in new", new.keys() - old.keys())):
        if names:
            print(f"{label}: {' '.join(sorted(names))}")
    differs = sorted(name for name in old.keys() & new.keys() if body(old[name]) != body(new[name]))
    known_bad = {container.stem for container in singles}
    changed = [name for name in differs if name not in known_bad]
    print(f"changed: {len(changed)}")
    noisy = [name for name in differs if name in known_bad]
    if noisy:
        print(f"known-bad, output varies run to run (not counted): {' '.join(noisy)}")
    for name in changed:
        print(f"  {name}")
        if args.show:
            for line in difflib.unified_diff(body(old[name]), body(new[name]), "old", "new",
                                             lineterm="", n=2):
                print(f"    {line}")
    print(f"co-issue hazards left in order (new): {new_unfixed}")
    for label, failures in (("old", old_failures), ("new", new_failures)):
        print(f"DXC failures ({label}): {len(failures)}")
        for name, reason in sorted(failures.items()):
            print(f"  {name} {reason}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
