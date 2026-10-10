"""Compare variants (presets, builds) over repeated captures (low-end-preset ticket 03).

Each variant is N capture folders from scripts/capture.ps1; the baseline is captured first and
last, so its own run-to-run spread is known. Per window (the route labels the marks give, or
"whole capture") each capture gives:
- displayed frame time: median of PresentMon's DisplayedTime (FrameTime where none was shown);
- guest work per tick: the --wwe_tick_log seconds' mean, weighted by ticks, and its p99 over the
  per-second maxima (an upper bound: the log keeps one maximum per second, not every tick);
- GPU time: median and p99 of Native's GPU timer (gpu_timer.csv), else of PresentMon's GPUBusy;
- 1 % low (the 99th percentile frame time) and hitches (frames over 25 ms).
A variant's value is the median over its runs. A difference against the baseline counts only
when it exceeds the baseline-to-baseline spread (zero-stutter ticket 11's ship rule).

The minimum-spec bar (spec "Measurement"): in the match window, 1 % low within 16.7 ms, no hitch,
guest work p99 and GPU p99 under 14 ms. It is the target for a Steam Deck; captures from any
other machine give an estimate only, and the summary says so.

    python scripts/benchmark_summary.py --baseline analysis/capture-a analysis/capture-d \\
        --variant low=analysis/capture-b,analysis/capture-c [--runs 2]
"""
import argparse
import json
import re
import statistics
import sys
from dataclasses import dataclass, fields
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_hitches import (HITCH_MS, LOG_TIME, load_gpu_timer, load_marks,  # noqa: E402
                             load_presentmon, nearest_rank, parse_time, split_windows, window_stats)

MIN_SPEC_LOW_MS = 16.7
MIN_SPEC_P99_MS = 14.0

# Device names a Steam Deck reports (LCD "VanGogh", OLED "Sephiroth"), under RADV or Proton.
DECK_GPU = re.compile(r"GPU: .*(VANGOGH|SEPHIROTH|Custom GPU 0405|Custom GPU 0932)", re.IGNORECASE)
TICK_SECOND = re.compile(r"game tick 1s: (\d+) ticks .*work mean ([\d.]+) ms max ([\d.]+) ms")


@dataclass
class WindowMetrics:
    displayed_ms: float | None
    work_ms: float | None
    gpu_ms: float | None
    one_percent_low_ms: float | None
    hitches: int | None
    work_p99_ms: float | None
    gpu_p99_ms: float | None


# What the comparison table shows, in order.
COMPARED = {"displayed_ms": "displayed", "work_ms": "guest work", "gpu_ms": "GPU"}


@dataclass
class Row:
    value: float      # the variant's median over its runs
    baseline: float   # the baseline's median over its runs
    spread: float     # the baseline's max minus min
    flagged: bool     # the difference exceeds the spread


@dataclass
class Summary:
    rows: dict        # variant -> window -> metric -> Row (None when a capture lacks the metric)
    refused: dict     # variant (or "baseline") -> reason
    verdicts: dict    # variant (or "baseline") -> window -> (passed, reasons)
    deck: bool        # every capture came from a Steam Deck


def median_metrics(runs, label):
    """The median over runs of each metric of one window; None where any run lacks it."""
    values = {}
    for field in fields(WindowMetrics):
        numbers = [r["windows"].get(label) and getattr(r["windows"][label], field.name) for r in runs]
        values[field.name] = None if any(n is None for n in numbers) else statistics.median(numbers)
    return WindowMetrics(**values)


def min_spec_verdict(m):
    """(True, []) inside every bar; (False, reasons) when one fails; (None, reasons) when a
    number is missing and nothing failed."""
    failed, missing = [], []
    if m.one_percent_low_ms is None or m.hitches is None:
        missing.append("no presents in the window")
    else:
        if m.one_percent_low_ms > MIN_SPEC_LOW_MS:
            failed.append(f"1 % low {m.one_percent_low_ms:.1f} ms is over {MIN_SPEC_LOW_MS} ms")
        if m.hitches:
            failed.append(f"{m.hitches:g} hitch" + ("es" if m.hitches != 1 else ""))
    if m.work_p99_ms is None:
        missing.append("no guest work numbers (capture without --wwe_tick_log)")
    elif m.work_p99_ms >= MIN_SPEC_P99_MS:
        failed.append(f"guest work p99 {m.work_p99_ms:.1f} ms is not under {MIN_SPEC_P99_MS:g} ms")
    if m.gpu_p99_ms is None:
        missing.append("no GPU numbers (no GPU timer CSV or GPUBusy column)")
    elif m.gpu_p99_ms >= MIN_SPEC_P99_MS:
        failed.append(f"GPU p99 {m.gpu_p99_ms:.1f} ms is not under {MIN_SPEC_P99_MS:g} ms")
    if failed:
        return False, failed
    return (None, missing) if missing else (True, [])


def compare_variants(baseline, variants, runs_required=2):
    """baseline: the baseline's captures (first and last at least); variants: {name: captures}.
    A capture is {"deck": bool, "windows": {label: WindowMetrics}}."""
    summary = Summary({}, {}, {}, all(c["deck"] for c in baseline + sum(variants.values(), [])))
    if len(baseline) < 2:
        summary.refused["baseline"] = (f"{len(baseline)} capture; the baseline is captured first and "
                                       "last, so its spread needs at least 2")
        return summary
    labels = list(baseline[0]["windows"])
    base = {label: median_metrics(baseline, label) for label in labels}
    summary.verdicts["baseline"] = {label: min_spec_verdict(base[label]) for label in labels
                                    if label in ("match", "whole capture")}
    for name, runs in variants.items():
        if len(runs) < runs_required:
            summary.refused[name] = (f"{len(runs)} run{'s' if len(runs) != 1 else ''}; "
                                     f"the summary needs {runs_required} per variant")
            continue
        summary.rows[name] = {}
        summary.verdicts[name] = {}
        for label in labels:
            value = median_metrics(runs, label)
            table = {}
            for metric in COMPARED:
                numbers = [getattr(c["windows"][label], metric) if label in c["windows"] else None
                           for c in baseline]
                mine = getattr(value, metric)
                known = [n for n in numbers if n is not None]
                if mine is None or len(known) < len(numbers):
                    table[metric] = None
                    continue
                spread = max(known) - min(known)
                middle = statistics.median(known)
                table[metric] = Row(mine, middle, spread, abs(mine - middle) > spread)
            summary.rows[name][label] = table
            if label in ("match", "whole capture"):
                summary.verdicts[name][label] = min_spec_verdict(value)
    return summary


def format_summary(summary):
    lines = ["| Variant | Window | Metric | Value | Baseline | Spread | Difference |",
             "|---|---|---|---|---|---|---|"]
    for name, windows in summary.rows.items():
        for label, table in windows.items():
            for metric, row in table.items():
                if row is None:
                    lines.append(f"| {name} | {label} | {COMPARED[metric]} | n/a | | | |")
                    continue
                mark = " *" if row.flagged else ""
                lines.append(f"| {name} | {label} | {COMPARED[metric]} | {row.value:.2f} ms | "
                             f"{row.baseline:.2f} ms | {row.spread:.2f} ms | "
                             f"{row.value - row.baseline:+.2f} ms{mark} |")
    lines.append("")
    lines.append("* the difference exceeds the baseline-to-baseline spread")
    for name, reason in summary.refused.items():
        lines.append(f"refused: {name}: {reason}")
    lines.append("")
    lines.append("Minimum-spec bar (match window: 1 % low within 16.7 ms, no hitch, guest work and "
                 "GPU p99 under 14 ms):")
    for name, windows in summary.verdicts.items():
        for label, (passed, reasons) in windows.items():
            word = {True: "PASS", False: "FAIL", None: "n/a"}[passed]
            lines.append(f"  {name} ({label}): {word}" + (f" ({'; '.join(reasons)})" if reasons else ""))
    lines.append("Steam Deck captures." if summary.deck else
                 "These captures are not from a Steam Deck: the minimum-spec verdict is an estimate "
                 "only, and says nothing about the Deck's CPU, shared memory or power budget.")
    return "\n".join(lines)


def load_capture(folder):
    """A capture folder's metrics per window (capture.ps1's layout)."""
    folder = Path(folder)
    if not (folder / "presentmon.csv").exists():
        raise SystemExit(f"{folder}: no presentmon.csv, so not a finished capture")
    info = {}
    if (folder / "display.json").exists():
        info = json.loads((folder / "display.json").read_text(encoding="utf-8-sig"))
    started = parse_time(info["started"]) if "started" in info else None
    frames, _ = load_presentmon(folder / "presentmon.csv", started)
    windows = split_windows(frames, [m for m, _ in load_marks(folder / "marks.txt")])
    log = folder / "runtime.log"
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines() if log.exists() else []
    ticks = []
    for line in lines:
        stamp, tick = LOG_TIME.match(line), TICK_SECOND.search(line)
        if stamp and tick:
            ticks.append((parse_time(stamp.group(1)), int(tick.group(1)), float(tick.group(2)),
                          float(tick.group(3))))
    timer_path = folder / "gpu_timer.csv"
    timer = [r for r in load_gpu_timer(timer_path) if r.gpu_ms is not None] if timer_path.exists() else []
    metrics = {}
    for window in windows:
        if not window.frames:
            continue
        start, end = window.frames[0].at, window.frames[-1].at
        shown = [f.displayed_ms for f in window.frames if f.displayed_ms is not None]
        inside = [t for t in ticks if start <= t[0] <= end]
        count = sum(t[1] for t in inside)
        gpu = [r.gpu_ms for r in timer if start <= r.at <= end]
        if not gpu:
            gpu = [f.gpu_busy_ms for f in window.frames if f.gpu_busy_ms is not None]
        low, _ = window_stats(window.frames)
        metrics[window.label] = WindowMetrics(
            statistics.median(shown or [f.frame_ms for f in window.frames]),
            sum(t[1] * t[2] for t in inside) / count if count else None,
            statistics.median(gpu) if gpu else None,
            low,
            sum(1 for f in window.frames if f.frame_ms > HITCH_MS),
            nearest_rank([t[3] for t in inside], 0.99) if inside else None,
            nearest_rank(gpu, 0.99) if gpu else None)
    return {"deck": any(DECK_GPU.search(line) for line in lines), "windows": metrics}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", nargs="+", required=True, help="baseline capture folders, first and last")
    parser.add_argument("--variant", action="append", default=[], metavar="NAME=FOLDER,FOLDER",
                        help="a variant's capture folders (repeatable)")
    parser.add_argument("--runs", type=int, default=2, help="captures required per variant")
    args = parser.parse_args()
    variants = {}
    for text in args.variant:
        name, _, folders = text.partition("=")
        variants[name] = [load_capture(f) for f in folders.split(",") if f]
    print(format_summary(compare_variants([load_capture(f) for f in args.baseline], variants, args.runs)))


if __name__ == "__main__":
    main()
