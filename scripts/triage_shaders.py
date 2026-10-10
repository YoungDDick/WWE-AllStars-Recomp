"""Convert each dumped shader container on its own with XenosRecomp and
report the ones that crash the tool or fail to compile.

Usage: python scripts/triage_shaders.py [cache/guest-shaders]
"""
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "out/build/xenosrecomp/XenosRecomp/XenosRecomp.exe"
COMMON = ROOT / ".tools/xenosrecomp/XenosRecomp/shader_common.h"
source = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "cache/guest-shaders")


def convert(path):
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        (work / "in").mkdir()
        shutil.copy(path, work / "in" / path.name)
        result = subprocess.run([str(TOOL), str(work / "in"), str(work / "out.cpp"), str(COMMON),
                                 str(work / "hlsl")], capture_output=True, text=True, errors="replace")
        failed = "reason=" in result.stdout
        return path.name, result.returncode, failed


with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
    results = list(pool.map(convert, sorted(source.glob("*.bin"))))
bad = [(name, code, failed) for name, code, failed in results if code != 0 or failed]
for name, code, failed in bad:
    print(f"{name} exit={code} {'compile-failed' if failed else ''}")
print(f"{len(bad)} of {len(results)} containers fail", file=sys.stderr)
