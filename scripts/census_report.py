"""Split a match's GPU draws by source, per phase (ticket 04).

Reads a session log with --wwe_d3d_census and --gpu_draw_census. For each
60-frame window it takes the per-present deltas and splits them into:

  hooked_d3d    DRAW_INDX from DrawIndexedVertices, 0x82DB1C48 and 0x82DB9828
  user_memory   DRAW_INDX from BeginVertices (DrawVerticesUP) and
                BeginIndexedVertices (DrawIndexedVerticesUP)
  d3d_internal  DRAW_INDX_2 from ClearRect, Resolve, the other DRAW_INDX_2
                builders and D3D's replayed point-list buffer
  unattributed  what the GPU executed beyond those, per packet type

Windows are labelled with the phase whose start frame they end in.
Usage: census_report.py <runtime.log> [phase=start_frame ...]
"""
import re
import sys
from dataclasses import dataclass, field

# Key frames from ticket 01 (session-m01b, Hogan vs Cena).
DEFAULT_PHASES = [("menus", 0), ("select", 4300), ("entrance", 5900), ("loading", 8000),
                  ("in-ring", 8700), ("finisher", 16600), ("result", 17000)]

KV = re.compile(r"(\w+)=(\d+)(?=[\s)]|$)")
CALLERS = re.compile(r"draw-census callers (\w+):(.*)$")


@dataclass
class Log:
    guest: dict = field(default_factory=dict)
    gpu: dict = field(default_factory=dict)
    vulkan: dict = field(default_factory=dict)
    callers: dict = field(default_factory=dict)


def kv(line):
    return {k: int(v) for k, v in KV.findall(line)}


def parse(lines):
    log = Log()
    for line in lines:
        if "draw-census guest" in line:
            d = kv(line)
            log.guest[d["presents"]] = d
        elif "draw-census gpu swaps" in line:
            d = kv(line)
            log.gpu[d["swaps"]] = d
        elif "draw-census vulkan" in line:
            d = kv(line)
            log.vulkan[d["swaps"]] = d
        elif m := CALLERS.search(line):
            # Counts are cumulative; the latest line has the newest value per caller.
            seen = log.callers.setdefault(m.group(1), {})
            for caller, n in re.findall(r"([0-9A-F]{8})=(\d+)", m.group(2)):
                seen[caller] = int(n)
    return log


def window_sources(log, start, end):
    frames = end - start

    def delta(table, key):
        return table[end].get(key, 0) - table[start].get(key, 0)

    g = lambda k: delta(log.guest, k)
    hooked = g("indexed") + g("draw_vertices") + g("1A468")
    user = g("up") + g("begin_indexed")
    internal = (g("clear_rect") + g("resolve") + g("other_draws") - g("1A468")
                + delta(log.vulkan, "points"))
    return {
        "hooked_d3d": hooked / frames,
        "user_memory": user / frames,
        "d3d_internal": internal / frames,
        "unattributed_draw_indx": (delta(log.gpu, "draw_indx") - hooked - user) / frames,
        "unattributed_draw_indx_2": (delta(log.gpu, "draw_indx_2") - internal) / frames,
    }


def phase_of(frame, phases):
    name = phases[0][0]
    for phase, first in phases:
        if frame >= first:
            name = phase
    return name


def report(log, phases):
    frames = sorted(set(log.guest) & set(log.gpu) & set(log.vulkan))
    rows = {}
    for start, end in zip(frames, frames[1:]):
        s = window_sources(log, start, end)
        row = rows.setdefault(phase_of(end, phases), {"windows": 0, "peak_unattributed": 0.0})
        row["windows"] += 1
        for key, value in s.items():
            row[key] = row.get(key, 0.0) + value
        residual = s["unattributed_draw_indx"] + s["unattributed_draw_indx_2"]
        row["peak_unattributed"] = max(row["peak_unattributed"], residual)
    out = []
    for phase, _ in phases:
        if phase in rows:
            row = rows[phase]
            for key in ("hooked_d3d", "user_memory", "d3d_internal", "unattributed_draw_indx",
                        "unattributed_draw_indx_2"):
                row[key] /= row["windows"]
            out.append({"phase": phase, **row})
    return out


def main(argv):
    path, *phase_args = argv
    phases = [(a.split("=")[0], int(a.split("=")[1])) for a in phase_args] or DEFAULT_PHASES
    with open(path, encoding="utf-8", errors="replace") as f:
        log = parse(f)
    print("| Phase | Windows | Hooked D3D | User memory | D3D internal | "
          "Unattributed DRAW_INDX | Unattributed DRAW_INDX_2 | Peak unattributed |")
    print("|---|---|---|---|---|---|---|---|")
    for r in report(log, phases):
        print(f"| {r['phase']} | {r['windows']} | {r['hooked_d3d']:.1f} | {r['user_memory']:.1f} | "
              f"{r['d3d_internal']:.1f} | {r['unattributed_draw_indx']:.1f} | "
              f"{r['unattributed_draw_indx_2']:.1f} | {r['peak_unattributed']:.1f} |")
    for builder, callers in sorted(log.callers.items()):
        top = sorted(callers.items(), key=lambda kv: -kv[1])
        print(f"{builder}: " + " ".join(f"{c}={n}" for c, n in top))


if __name__ == "__main__":
    main(sys.argv[1:])
