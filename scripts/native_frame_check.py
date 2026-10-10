"""Feedback loop for the native renderer's presented image.

Runs the game headless and dumps one presented frame (--wwe_native_dump_frame).

  --mode native   native renderer; RED below --min-nonblack-fraction coverage
  --mode emulated Emulated renderer's front buffer (--readback_resolve=full);
                  --mode classic still works (the renderer's former name)
  --mode diff     both, then the mean absolute pixel difference; RED when
                  it exceeds --tolerance or either frame lacks visible content

Extra runtime flags pass through after "--".
Usage: python scripts/native_frame_check.py [--mode M] [--frame N] [--seconds S] [-- --flag=value ...]
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

parser = argparse.ArgumentParser()
# "classic" is the Emulated renderer's former name (ADR-0020), kept as a hidden alias.
parser.add_argument("--mode", choices=["native", "emulated", "diff"], default="native",
                    type=lambda mode: "emulated" if mode == "classic" else mode)
parser.add_argument("--frame", type=int, default=600)
parser.add_argument("--seconds", type=int, default=25)
parser.add_argument("--tolerance", type=float, default=24.0)
parser.add_argument("--min-nonblack-fraction", type=float, default=0.01,
                    help="Minimum sampled coverage; rejects a stray line on a black frame")
parser.add_argument("extra", nargs="*")
args = parser.parse_args()


def run(renderer):
    dump = ROOT / "analysis" / f"{renderer}-frame-{args.frame}.ppm"
    dump.unlink(missing_ok=True)
    flags = [f"--wwe_native_dump_frame={args.frame}", f"--wwe_native_dump_path={dump.as_posix()}",
             *args.extra]
    # Native is the default (ADR-0020); each run names its renderer.
    flags += (["--wwe_native_renderer=true"] if renderer == "native"
              else ["--wwe_native_renderer=false", "--readback_resolve=full"])
    quoted = ",".join(f"'{f}'" for f in flags)
    result = subprocess.run(
        ["pwsh", "-NoProfile", "-Command",
         f"./scripts/smoke.ps1 -Headless -Seconds {args.seconds} -ExtraArguments {quoted}"],
        cwd=ROOT, capture_output=True, text=True)
    out = result.stdout.strip()
    outcome = json.loads(out)["outcome"] if out.startswith("{") else "?"
    if not dump.exists():
        print(f"NO-DUMP {renderer} outcome={outcome} (frame {args.frame} not reached?)")
        sys.exit(2)
    data = dump.read_bytes()
    header, _, pixels = data.split(b"\n", 3)[0:2], None, data.split(b"\n", 3)[3]
    width, height = map(int, data.split(b"\n", 3)[1].split())
    return width, height, pixels, dump


def nonblack(pixels):
    return sum(1 for i in range(0, len(pixels), 3 * 97) if any(pixels[i:i + 3]))


def visible(pixels):
    samples = len(range(0, len(pixels), 3 * 97))
    return samples > 0 and nonblack(pixels) / samples > args.min_nonblack_fraction


if args.mode in ("native", "emulated"):
    w, h, px, dump = run(args.mode)
    verdict = "GREEN" if visible(px) else "RED"
    print(f"{verdict} {args.mode} frame={args.frame} {w}x{h} nonblack_samples={nonblack(px)} dump={dump}")
    sys.exit(0 if verdict == "GREEN" else 1)

cw, ch, cpx, cdump = run("emulated")
nw, nh, npx, ndump = run("native")
# Compare on a coarse grid in normalised coordinates (the sizes may differ).
total, samples = 0.0, 0
for gy in range(64):
    for gx in range(64):
        ci = ((gy * ch // 64) * cw + gx * cw // 64) * 3
        ni = ((gy * nh // 64) * nw + gx * nw // 64) * 3
        total += sum(abs(cpx[ci + c] - npx[ni + c]) for c in range(3)) / 3
        samples += 1
mean = total / samples
verdict = "GREEN" if visible(cpx) and visible(npx) and mean <= args.tolerance else "RED"
print(f"{verdict} diff frame={args.frame} mean_abs_diff={mean:.1f} (tolerance {args.tolerance}) "
      f"emulated_nonblack={nonblack(cpx)} native_nonblack={nonblack(npx)} {cdump} {ndump}")
sys.exit(0 if verdict == "GREEN" else 1)
