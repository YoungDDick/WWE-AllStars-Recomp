"""Compare build-flag variants by the game's own work per engine tick (ticket 11).

Each variant is a runtime log of a start with --wwe_tick_log=true. The game's work per tick
(the tick minus its frame limiter's wait, from the once-a-second "game tick 1s" lines) is
averaged over the same span of each run, and every build is compared with the first one.

    python scripts/compare_builds.py v2o2=analysis/runtime-a/runtime.log v3o2=... [--skip 20] [--span 40]
"""
import argparse
import datetime
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_hitches import load_tick_seconds  # noqa: E402


@dataclass
class Row:
    name: str
    ticks: int
    work_ms: float
    change_pct: float | None


def compare(logs, skip_s, span_s):
    """logs: {name: runtime log lines}, baseline first. Only the summary seconds from skip_s
    to skip_s + span_s after each run's first one count."""
    rows = []
    for name, lines in logs.items():
        samples = load_tick_seconds(lines)
        if samples:
            start = samples[0][0] + datetime.timedelta(seconds=skip_s)
            end = start + datetime.timedelta(seconds=span_s)
            samples = [s for s in samples if start <= s[0] < end]
        ticks = sum(s[1] for s in samples)
        work = sum(s[1] * s[3] for s in samples) / ticks if ticks else float("nan")
        change = None
        if rows:
            change = 100 * (work - rows[0].work_ms) / rows[0].work_ms
        rows.append(Row(name, ticks, work, change))
    return rows


def format_table(rows):
    lines = ["| Build | Ticks | Game work per tick | Change |", "|---|---|---|---|"]
    for r in rows:
        change = "baseline" if r.change_pct is None else f"{r.change_pct:+.1f} %"
        lines.append(f"| {r.name} | {r.ticks} | {r.work_ms:.2f} ms | {change} |")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("runs", nargs="+", help="name=path/to/runtime.log, the baseline first")
    parser.add_argument("--skip", type=float, default=0, help="seconds left out after the first summary line")
    parser.add_argument("--span", type=float, default=1e9, help="seconds compared after --skip")
    args = parser.parse_args()
    logs = {}
    for run in args.runs:
        name, _, path = run.partition("=")
        logs[name] = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
    print(format_table(compare(logs, args.skip, args.span)))


if __name__ == "__main__":
    main()
