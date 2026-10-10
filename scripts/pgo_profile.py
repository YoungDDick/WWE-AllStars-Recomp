"""Merge the raw PGO profiles of an instrumented build (WWE_GAME_PGO_GENERATE) into one
.profdata for WWE_GAME_PGO_USE. See the README, "Build variants".

    python scripts/pgo_profile.py merge out/build/<instrumented build>/pgo-raw -o data/pgo/route.profdata
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LLVM_PROFDATA = ROOT / ".tools" / "llvm-21.1.8" / "bin" / "llvm-profdata.exe"


class NoProfiles(Exception):
    pass


def find_raw_profiles(directory):
    """The non-empty .profraw files in directory, by name. An empty one is a run that was killed
    before it wrote its counters."""
    found = sorted(p for p in Path(directory).glob("*.profraw") if p.is_file() and p.stat().st_size > 0)
    if not found:
        raise NoProfiles(f"No non-empty .profraw in {directory}. Play the route on the instrumented "
                         "build and close the game with the window's X (a killed game writes nothing).")
    return found


def merge_command(inputs, output, llvm_profdata=LLVM_PROFDATA):
    return [Path(llvm_profdata).as_posix(), "merge", "-o", Path(output).as_posix()] + [Path(p).as_posix() for p in inputs]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    merge = sub.add_parser("merge", help="merge an instrumented build's pgo-raw folder into one .profdata")
    merge.add_argument("raw_dir", type=Path)
    merge.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        inputs = find_raw_profiles(args.raw_dir)
    except NoProfiles as error:
        print(error, file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(merge_command(inputs, args.output), check=True)
    print(f"Merged {len(inputs)} raw profile(s) into {args.output}")
    print(f"Use it: build.ps1 -Action Configure -Variant <name> -CMakeArgs "
          f"'-DWWE_GAME_PGO_USE={args.output.resolve().as_posix()}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
