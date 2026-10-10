"""Judge a capture against the Hitch and Judder bar, or list slow frames in a hitch session.

A capture folder from scripts/capture.ps1 (presentmon.csv present) gets the smoothness report:
the marks split it into the route's windows (intro, entrance 1, entrance 2, match), and each
window gets a verdict.
- Hitch: no frame over 25 ms between presents. With --allow-30hz <label>, that window may run
  an even 30 Hz instead, but no frame may be longer than one 30 Hz step.
- Judder (match windows only): every frame on screen 16.7 ms +- 1 ms (PresentMon's
  DisplayedTime), none left unshown, and no repeated or skipped game frame (Native's swap events
  from --frame_event_log, and the held or replaced frames in present_pacing.csv).
Each hitch is listed with the runtime events logged just before it. Each window also gets a GPU
line (median, p99, max): from Native's GPU timer (gpu_timer.csv, --wwe_native_gpu_timer_csv) when
the capture has one, else from PresentMon's GPUBusy, which includes the presenter's work. The
header names the Graphics preset, its settings and whether the launcher was open (display.json).
The zero-stutter bars follow (judge_bars): the 1 % low and worst frame per window, then the
first-run bar (no pipeline_pending skip, render-thread pipeline compile or wait, or render-thread
shader module creation in runtime.log, no hitch), the intro bar (intro and entrances at an even
cadence of the game's own step: each frame one step long, no hitch) and the match bar (judder
bar, 1 % low within 16.7 ms). Hitches
the owner accepts as load boundaries are named with --accept-load and left out of the first two.
The game's own step comes from the --wwe_tick_log seconds: 1/30 s in a second with ticks at
1/30 s (a flagged Matinee: the faithful 30 Hz intro and entrances), else 1/60 s. A hitch is a
frame over 25 ms at 1/60 s, over 41.7 ms (the same 8.3 ms margin) at 1/30 s.

An older hitch session (present.csv from the frame-pacing log) gets the slow-frame list: a frame
is slow when its present interval exceeds the scene's normal interval (the median of its
neighbours, so 30 FPS entrances are not flagged) by more than --slack milliseconds. With
marks.txt present, only frames from --before seconds before a mark to --after seconds after it
are reported.
"""
import argparse
import bisect
import csv
import datetime
import json
import math
import re
import statistics
from dataclasses import dataclass, field
from pathlib import Path

LOG_TIME = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]")
ORIGIN = "Paced presentation log started"
EVENTS = {
    "Creating graphics pipeline": "pipeline build",
    "Frame event: present skipped": "present skipped (pipeline building)",
    "Frame event: blocking resolve readback": "blocking readback",
    "Frame event: deferred resolve readback": "deferred readback (no wait)",
    "native renderer: hitch": "Native hitch line",
    "game tick: ": "Game tick line",
    "File open: ": "file open",
}

FRAME_MS = 1000 / 60
HITCH_MS = 25.0
STEP_30HZ_MS = 1000 / 30 + 1.0
HALF_STEP_MS = 1000 / 30          # the game's own half-rate engine step
HITCH_MARGIN_MS = HITCH_MS - FRAME_MS
ON_SCREEN_SLACK_MS = 1.0
MATCH_ONE_PERCENT_LOW_MS = 16.7   # the spec's match bar: a 1 % low no worse than 16.7 ms
EVENT_WINDOW_MS = 250.0
ROUTE = ["intro", "entrance 1", "entrance 2", "match"]


def parse_time(text):
    return datetime.datetime.strptime(text, "%Y-%m-%d %H:%M:%S.%f")


def load_log(path):
    origin, events = None, []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LOG_TIME.match(line)
        if not match:
            continue
        if ORIGIN in line and origin is None:
            origin = parse_time(match.group(1))
        for needle, kind in EVENTS.items():
            if needle in line:
                events.append((parse_time(match.group(1)), kind, line[match.end():].strip()))
    return origin, events


def load_marks(path):
    marks = []
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            stamp, _, label = line.partition("\t")
            marks.append((parse_time(stamp), label))
    return marks


@dataclass
class Frame:
    at: datetime.datetime
    frame_ms: float               # time since the previous present
    displayed_ms: float | None    # time on screen; None when it was never shown
    gpu_busy_ms: float | None = None  # PresentMon's GPUBusy: the game's GPU work, presenter included


@dataclass
class Window:
    label: str
    kind: str                     # intro, entrance or match
    frames: list = field(default_factory=list)


@dataclass
class Verdict:
    hitches: list
    hitch_pass: bool
    uneven_on_screen: int
    not_shown: int
    judder_pass: bool | None      # None: the judder bar does not apply to this window


def parse_presentmon_time(text):
    day, clock = text.split(" ")
    year, month, date = (int(x) for x in day.split("-"))
    hour, minute, second = clock.split(":")
    return (datetime.datetime(year, month, date, int(hour), int(minute))
            + datetime.timedelta(seconds=float(second)))


def load_presentmon(path, started=None):
    """Read a PresentMon --date_time --v2_metrics CSV.

    PresentMon's wall clock is not the local clock the marks use (it ran exactly 2 h ahead on
    the owner's machine), so the rows are shifted by the whole quarter hours between the first
    row and the capture's start time.
    """
    def number(text):
        return None if text in ("NA", "", None) else float(text)

    rows, mode = [], None
    with path.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            rows.append(Frame(parse_presentmon_time(row["CPUStartDateTime"]), float(row["FrameTime"]),
                              number(row.get("DisplayedTime", "NA")), number(row.get("GPUBusy"))))
            mode = mode or row.get("PresentMode")
    if rows and started is not None:
        quarter = 15 * 60
        shift = round((rows[0].at - started).total_seconds() / quarter) * quarter
        for frame in rows:
            frame.at -= datetime.timedelta(seconds=shift)
    return rows, mode


def kind_of(label):
    for kind in ("intro", "entrance", "match"):
        if label.startswith(kind):
            return kind
    return "match"


def split_windows(frames, marks):
    """One window per mark, from that mark to the next; labels follow the route."""
    if not marks:
        return [Window("whole capture", "match", list(frames))]
    windows = []
    for i, start in enumerate(marks):
        end = marks[i + 1] if i + 1 < len(marks) else None
        label = ROUTE[i] if i < len(ROUTE) else f"mark {i + 1}"
        inside = [f for f in frames if f.at >= start and (end is None or f.at < end)]
        windows.append(Window(label, kind_of(label), inside))
    return windows


def judge_window(frames, kind, allow_30hz=False, repeats=0, skips=0):
    limit = STEP_30HZ_MS if allow_30hz else HITCH_MS
    hitches = [f for f in frames if f.frame_ms > limit]
    uneven = sum(1 for f in frames
                 if f.displayed_ms is not None and abs(f.displayed_ms - FRAME_MS) > ON_SCREEN_SLACK_MS)
    not_shown = sum(1 for f in frames if f.displayed_ms is None)
    judder = None
    if kind == "match":
        judder = uneven == 0 and not_shown == 0 and repeats == 0 and skips == 0
    return Verdict(hitches, not hitches, uneven, not_shown, judder)


NATIVE_RENDERER = re.compile(r"renderer ([\d.]+) ms")
NATIVE_READBACK = re.compile(r"readbacks queued \d+ \(([\d.]+) ms\), delivered \d+ \([\d.]+ MB, ([\d.]+) ms\)")


def native_hitch_note(text):
    """Native's own share of a hitch: time in its hooks and in resolve copy-backs."""
    renderer, readback = NATIVE_RENDERER.search(text), NATIVE_READBACK.search(text)
    note = f"Native renderer {renderer.group(1)} ms" if renderer else "Native hitch"
    if readback:
        note += f", readback {float(readback.group(1)) + float(readback.group(2)):.1f} ms"
    return note


GAME_TICK = re.compile(r"game tick: [\d.]+ ms, work ([\d.]+) ms, limiter ([\d.]+) ms"
                       r"(?:, render thread ([\d.]+) ms)?")


def game_tick_note(text):
    """The game's own share of a hitch: its tick's work, the frame limiter's wait and (logs
    from low-end-preset ticket 10 on) its wait for the render thread."""
    tick = GAME_TICK.search(text)
    if not tick:
        return "slow game tick"
    note = f"game work {tick.group(1)} ms, limiter {tick.group(2)} ms"
    return note + f", render thread {tick.group(3)} ms" if tick.group(3) else note


def smoothness_report(windows, allow_30hz, events, swap_counts=None):
    lines = []
    for window in windows:
        frames = window.frames
        if not frames:
            lines.append(f"{window.label}: no frames")
            continue
        repeats, skips = (swap_counts or {}).get(window.label, (0, 0))
        verdict = judge_window(frames, window.kind, window.label in allow_30hz, repeats, skips)
        seconds = max((frames[-1].at - frames[0].at).total_seconds(), 1e-6)
        shown = [f.displayed_ms for f in frames if f.displayed_ms is not None]
        hitch_word = "PASS" if verdict.hitch_pass else "FAIL"
        judder_word = {None: "n/a", True: "PASS", False: "FAIL"}[verdict.judder_pass]
        lines.append(
            f"{window.label}: {seconds:.1f} s, {len(frames) / seconds:.1f} presents/s, "
            f"median {statistics.median(f.frame_ms for f in frames):.2f} ms, "
            f"max {max(f.frame_ms for f in frames):.1f} ms"
            + (", allowed 30 Hz" if window.label in allow_30hz else ""))
        lines.append(
            f"  on screen: median {statistics.median(shown):.2f} ms, outside 16.7 +- 1 ms: "
            f"{verdict.uneven_on_screen}, not shown: {verdict.not_shown}, "
            f"repeated game frames: {repeats}, skipped: {skips}" if shown else
            f"  on screen: no displayed times, not shown: {verdict.not_shown}")
        lines.append(f"  hitch {hitch_word} ({len(verdict.hitches)} over the limit), judder {judder_word}")
        for hitch in verdict.hitches:
            near = [e for e in events if 0 <= (hitch.at - e[0]).total_seconds() * 1000 <= EVENT_WINDOW_MS]
            counts = {}
            for _, kind, _ in near:
                counts[kind] = counts.get(kind, 0) + 1
            note = ", ".join(f"{n} x {k}" for k, n in counts.items()) or "no logged events"
            native = [native_hitch_note(text) for _, kind, text in near if kind == "Native hitch line"]
            native += [game_tick_note(text) for _, kind, text in near if kind == "Game tick line"]
            if native:
                note += "; " + "; ".join(native)
            lines.append(f"    {hitch.at:%H:%M:%S.%f}"[:-3] + f"  {hitch.frame_ms:.1f} ms  ({note})")
    return lines


@dataclass
class RenderThreadCounts:
    """Compiles on the hot path over the whole session, from the runtime log."""
    pipeline_pending: int = 0     # main-scene draws skipped while their pipeline was built
    pipelines_compiled: int = 0   # pipelines the render thread compiled itself
    modules_created: int = 0      # shader modules the render thread created
    pipeline_waits: int = 0       # pipelines the render thread waited for a worker to build
    live_conversions: int = 0     # shaders the pack lacked, converted in the session (ticket 22)
    live_conversion_failures: int = 0  # left for the launcher's shader step


@dataclass
class Bars:
    first_run_pass: bool | None   # None: no runtime log, so nothing to count
    intro_pass: bool | None       # None: no intro or entrance window
    match_pass: bool | None       # None: no match window
    lines: list = field(default_factory=list)


PENDING = re.compile(r"native renderer: (?:session )?draw skips .*pipeline_pending=(\d+)")


RENDER_THREAD_TOTALS = re.compile(r"native renderer: render thread totals at frame \d+: "
                                  r"pipelines compiled (\d+), shader modules created (\d+)"
                                  r"(?:, pipeline waits (\d+))?")
HITCH_COMPILES = re.compile(r"native renderer: hitch at .*pipelines compiled on this thread (\d+)")
LIVE_CONVERTED = re.compile(r"shader top-up: converted \S+ live in")
LIVE_FAILED = re.compile(r"shader top-up: could not convert \S+ live:")


def load_render_thread_counts(lines):
    """Native's draw-skip and render-thread totals lines carry session totals, so the largest
    one counts. Builds without the totals line only logged compiles in hitch lines, which are
    summed instead."""
    counts = RenderThreadCounts()
    hitch_compiles, totals_seen = 0, False
    for line in lines:
        pending = PENDING.search(line)
        if pending:
            counts.pipeline_pending = max(counts.pipeline_pending, int(pending.group(1)))
        totals = RENDER_THREAD_TOTALS.search(line)
        if totals:
            totals_seen = True
            counts.pipelines_compiled = max(counts.pipelines_compiled, int(totals.group(1)))
            counts.modules_created = max(counts.modules_created, int(totals.group(2)))
            counts.pipeline_waits = max(counts.pipeline_waits, int(totals.group(3) or 0))
        hitch = HITCH_COMPILES.search(line)
        if hitch:
            hitch_compiles += int(hitch.group(1))
        counts.live_conversions += bool(LIVE_CONVERTED.search(line))
        counts.live_conversion_failures += bool(LIVE_FAILED.search(line))
    if not totals_seen:
        counts.pipelines_compiled = hitch_compiles
    return counts


def window_stats(frames):
    """The 1 % low (the frame time only the slowest 1 % of frames exceed: the 99th percentile,
    nearest rank) and the worst frame time, in ms."""
    times = sorted(f.frame_ms for f in frames)
    rank = max(math.ceil(0.99 * len(times)), 1)
    return times[rank - 1], times[-1]


def game_steps_ms(at, tick_samples):
    """The engine steps the game took around a present, from the tick log's second holding it
    (each once-a-second line covers the second before it): 1/30 s when all its ticks were at
    1/30 s, both 1/60 s and 1/30 s when some were (the game switched step in that second), else
    1/60 s. tick_samples: load_tick_seconds' list, in time order."""
    times = [s[0] for s in tick_samples]
    i = bisect.bisect_left(times, at)
    if i < len(tick_samples) and tick_samples[i][0] - datetime.timedelta(seconds=1) < at:
        ticks, half = tick_samples[i][1], tick_samples[i][2]
        if half and half >= ticks:
            return (HALF_STEP_MS,)
        if half:
            return (FRAME_MS, HALF_STEP_MS)
    return (FRAME_MS,)


def game_step_ms(at, tick_samples):
    """The longest engine step the game took around a present (game_steps_ms)."""
    return max(game_steps_ms(at, tick_samples))


def to_millisecond(at):
    return at.replace(microsecond=at.microsecond // 1000 * 1000)


def verdict_word(passed):
    return {None: "n/a", True: "PASS", False: "FAIL"}[passed]


def judge_bars(windows, counts, accepted=(), swap_counts=None, tick_samples=()):
    """The zero-stutter bars over a whole route capture.

    A frame's step is the game's own (game_step_ms over tick_samples; 1/60 s without them),
    and a hitch is a frame longer than that step by more than 8.3 ms (over 25 ms at 1/60 s,
    over 41.7 ms at 1/30 s): a game frame held or skipped.
    - First run: no pipeline_pending skip, no pipeline compiled or waited for and no shader
      module created on the render thread, and no hitch (judged at the game's own step, so the
      faithful 30 Hz of ticket 15's choice A may run 33.3 ms frames).
    - Intro: the intro and entrance windows at an even cadence of the game's own step (each
      frame's time on screen, or its present interval if never shown, within 1 ms of the step:
      60 Hz, or the faithful 30 Hz where the game stepped 1/30 s) with no hitch.
    accepted: the present times (to the millisecond, as the report prints them) of load
    boundaries the owner accepted by eye; those frames count for neither bar above.
    - Match: each match window passes the judder bar and has a 1 % low within 16.7 ms.
    counts is None when there is no runtime log; the first-run bar is then n/a.
    """
    windows = [w for w in windows if w.frames]
    accepted = {to_millisecond(at) for at in accepted}

    def counted(frames):
        return [f for f in frames if to_millisecond(f.at) not in accepted]

    lines = []
    for window in windows:
        one_percent_low, worst = window_stats(window.frames)
        lines.append(f"{window.label}: 1 % low {one_percent_low:.1f} ms "
                     f"({1000 / one_percent_low:.1f} fps), worst {worst:.1f} ms")
    tick_samples = sorted(tick_samples)
    steps = {id(f): game_steps_ms(f.at, tick_samples) for w in windows for f in w.frames}
    step = {key: max(value) for key, value in steps.items()}

    def hitch(frame):
        return frame.frame_ms > step[id(frame)] + HITCH_MARGIN_MS

    def even(frames):
        def one_step(frame):
            ms = frame.displayed_ms if frame.displayed_ms is not None else frame.frame_ms
            return any(abs(ms - s) <= ON_SCREEN_SLACK_MS for s in steps[id(frame)])
        return all(one_step(f) for f in frames)

    long_frames = [f for w in windows for f in counted(w.frames) if hitch(f)]
    excluded = sum(len(w.frames) - len(counted(w.frames)) for w in windows)
    first_run = None
    if counts is not None:
        first_run = (counts.pipeline_pending == 0 and counts.pipelines_compiled == 0
                     and counts.pipeline_waits == 0 and counts.modules_created == 0
                     and not long_frames)
    build_up = [w for w in windows if w.kind in ("intro", "entrance")]
    intro = None
    if build_up:
        intro = all(even(counted(w.frames)) and not any(hitch(f) for f in counted(w.frames))
                    for w in build_up)
    matches = [w for w in windows if w.kind == "match"]
    match = None
    if matches:
        match = True
        for window in matches:
            repeats, skips = (swap_counts or {}).get(window.label, (0, 0))
            judder = judge_window(window.frames, "match", False, repeats, skips).judder_pass
            match = match and judder and window_stats(window.frames)[0] <= MATCH_ONE_PERCENT_LOW_MS
    if counts is None:
        lines.append("first run: n/a (no runtime log)")
    else:
        lines.append(f"first run {verdict_word(first_run)}: pipeline_pending {counts.pipeline_pending}, "
                     f"render-thread pipeline compiles {counts.pipelines_compiled}, render-thread "
                     f"pipeline waits {counts.pipeline_waits}, render-thread "
                     f"shader modules {counts.modules_created}, hitches {len(long_frames)}, "
                     f"accepted load boundaries {excluded}; live shader conversions "
                     f"{counts.live_conversions} (failed {counts.live_conversion_failures})")
    half = sum(1 for ms in step.values() if ms == HALF_STEP_MS)
    lines.append(f"intro {verdict_word(intro)}: intro and entrances even at the game's own step "
                 f"(each frame one step +- 1 ms; {half} frames at 1/30 s, the rest 1/60 s) with no "
                 f"hitch (25 ms at 1/60 s, 41.7 ms at 1/30 s)")
    lines.append(f"match {verdict_word(match)}: judder bar and 1 % low within 16.7 ms")
    return Bars(first_run, intro, match, lines)


SWAP_EVENT = re.compile(r"Frame event: external frame (repeated|skipped (\d+)), sequence")


def load_swap_events(lines):
    """The pacing log's origin and Native's swap events: (time, repeats, skips)."""
    origin, events = None, []
    for line in lines:
        match = LOG_TIME.match(line)
        if not match:
            continue
        if ORIGIN in line and origin is None:
            origin = parse_time(match.group(1))
        swap = SWAP_EVENT.search(line)
        if swap:
            skipped = int(swap.group(2)) if swap.group(2) else 0
            events.append((parse_time(match.group(1)), 0 if skipped else 1, skipped))
    return origin, events


def swap_counts(windows, origin, events, pacing_rows):
    """Repeated and skipped game frames per window.

    A swap that shows Native's previous frame again, or a paced vblank with no new frame, repeats
    one; a swap that jumps over published frames, or a frame replaced before it was presented,
    skips one.
    """
    counts = {}
    for window in windows:
        if not window.frames:
            continue
        start, end = window.frames[0].at, window.frames[-1].at
        repeats = skips = 0
        for at, repeat, skip in events:
            if start <= at <= end:
                repeats += repeat
                skips += skip
        if origin is not None:
            for row in pacing_rows:
                at = origin + datetime.timedelta(microseconds=int(row["present_us"]))
                if start <= at <= end:
                    repeats += int(row.get("repeated_vblanks") or 0)
                    skips += int(row.get("frames_replaced") or 0)
        counts[window.label] = (repeats, skips)
    return counts


def nearest_rank(values, fraction):
    """The value at the given fraction by nearest rank (0.99 for p99)."""
    ordered = sorted(values)
    return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]


def latency_summary(windows, origin, pacing_rows):
    """Per window: guest frame to present (latency_us) and, with present-wait, present to shown
    and how often a present waited on the previous one (the swap log's later columns)."""
    lines = {}
    if origin is None:
        return lines

    def at(us):
        return origin + datetime.timedelta(microseconds=int(us))

    for window in windows:
        if not window.frames:
            continue
        start, end = window.frames[0].at, window.frames[-1].at
        rows = [r for r in pacing_rows if r.get("latency_us") and start <= at(r["present_us"]) <= end]
        if not rows:
            continue
        to_present = [int(r["latency_us"]) / 1000 for r in rows]
        line = (f"latency: frame to present median {statistics.median(to_present):.2f} ms, "
                f"p99 {nearest_rank(to_present, 0.99):.2f} ms; ")
        waited = [r for r in rows if r.get("present_id")]
        if not waited:
            lines[window.label] = line + "present to shown n/a (timer pacing)"
            continue
        shown = [int(r["shown_after_present_us"]) / 1000 for r in pacing_rows
                 if r.get("shown_after_present_us") and start <= at(r["shown_us"]) <= end]
        line += (f"present to shown median {statistics.median(shown):.2f} ms, "
                 f"p99 {nearest_rank(shown, 0.99):.2f} ms ({len(shown)} shown); " if shown else
                 "present to shown: none recorded; ")
        waits = [int(r["wait_us"]) / 1000 for r in waited if int(r["wait_us"]) > 0]
        line += (f"{len(waits)} presents waited on the previous one"
                 + (f" (max {max(waits):.2f} ms)" if waits else "")
                 + f", most unshown {max(int(r['unshown']) for r in waited)}")
        lines[window.label] = line
    return lines


DISPLAY_LINE = re.compile(r"Display vblank 1s: display ([\d.]+) Hz, (\d+) refreshes held longer"
                          r".*?drift ([-+\d.]+) ms/s")


def load_display_lines(lines):
    """--display_vblank_log samples: (time, display Hz, refreshes held longer, drift ms/s)."""
    samples = []
    for line in lines:
        match, display = LOG_TIME.match(line), DISPLAY_LINE.search(line)
        if match and display:
            samples.append((parse_time(match.group(1)), float(display.group(1)), int(display.group(2)),
                            float(display.group(3))))
    return samples


def display_summary(windows, samples):
    """Per window: the display's refresh, how often variable refresh held one, and the guest
    vblank's mean absolute drift against it."""
    summary = {}
    for window in windows:
        if not window.frames:
            continue
        start, end = window.frames[0].at, window.frames[-1].at
        inside = [s for s in samples if start <= s[0] <= end]
        if inside:
            summary[window.label] = (
                f"display {statistics.mean(s[1] for s in inside):.2f} Hz, "
                f"{statistics.mean(s[2] for s in inside):.1f} held longer/s (variable refresh), "
                f"guest vblank drift {statistics.mean(abs(s[3]) for s in inside):.3f} ms/s")
    return summary


TICK_SECOND = re.compile(r"game tick 1s: (\d+) ticks \((\d+) at 1/30 s, \d+ other steps\), "
                         r"work mean ([\d.]+) ms")


def load_tick_seconds(lines):
    """--wwe_tick_log's once-a-second lines: (time, ticks, ticks at 1/30 s, mean work ms)."""
    samples = []
    for line in lines:
        match, tick = LOG_TIME.match(line), TICK_SECOND.search(line)
        if match and tick:
            samples.append((parse_time(match.group(1)), int(tick.group(1)), int(tick.group(2)),
                            float(tick.group(3))))
    return samples


def game_step_summary(windows, samples):
    """Per window: how many engine ticks stepped 1/30 s and the game's mean work per tick.

    A 1/30 s step is the game's own half-rate (FEngineLoop::Tick takes it while a Matinee
    flagged for it plays; docs/research/entrance-video-stutter.md), so a 30 Hz window with
    1/30 s steps is the game's choice, and one with 1/60 s steps and long work is guest cost.
    """
    summary = {}
    for window in windows:
        if not window.frames:
            continue
        start, end = window.frames[0].at, window.frames[-1].at
        inside = [s for s in samples if start <= s[0] <= end]
        ticks = sum(s[1] for s in inside)
        if not ticks:
            continue
        half = sum(s[2] for s in inside)
        work = sum(s[1] * s[3] for s in inside) / ticks
        summary[window.label] = (f"game: {ticks} ticks, {half} at 1/30 s ({100 * half / ticks:.1f} %"
                                 + (", the game's half-rate" if half else "")
                                 + f"), work mean {work:.1f} ms")
    return summary


@dataclass
class TimerFrame:
    at: datetime.datetime
    gpu_ms: float | None          # None: the device had no timestamps on the graphics queue
    cpu_ms: float                 # the renderer's CPU time in its hooks for this frame


def load_gpu_timer(path):
    """Native's GPU timer CSV (--wwe_native_gpu_timer_csv): its per-frame rows, by submit time."""
    rows = []
    with path.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            if row["item"] != "frame" or not row["time"]:
                continue
            rows.append(TimerFrame(parse_time(row["time"]), float(row["gpu_ms"]) if row["gpu_ms"] else None,
                                   float(row["cpu_ms"])))
    return rows


def ms_stats(values):
    return (f"median {statistics.median(values):.2f} ms, p99 {nearest_rank(values, 0.99):.2f} ms, "
            f"max {max(values):.2f} ms")


def gpu_summary(windows, timer_rows):
    """Per window: GPU time from Native's timer when it measured any, else PresentMon's GPUBusy
    (labelled, as it includes the presenter), plus the renderer's CPU time from the timer. A
    capture with neither gets no line, as before. timer_rows: load_gpu_timer's list, or None."""
    summary = {}
    for window in windows:
        if not window.frames:
            continue
        start, end = window.frames[0].at, window.frames[-1].at
        timed = [r for r in timer_rows or () if start <= r.at <= end]
        timer_gpu = [r.gpu_ms for r in timed if r.gpu_ms is not None]
        busy = [f.gpu_busy_ms for f in window.frames if f.gpu_busy_ms is not None]
        if timer_gpu:
            line = f"GPU (Native timer): {ms_stats(timer_gpu)}"
        elif busy:
            line = f"GPU (PresentMon GPUBusy, includes the presenter): {ms_stats(busy)}"
        else:
            continue
        if timed:
            line += f"; renderer CPU median {statistics.median(r.cpu_ms for r in timed):.2f} ms"
        summary[window.label] = line
    return summary


def settings_header(info):
    """The Graphics preset, its settings and the launcher's state as capture.ps1 recorded them;
    None for older captures."""
    if "settings" not in info:
        return None
    settings = ", ".join(f"{name} {value}" for name, value in info["settings"].items())
    launcher = {True: "yes", False: "no"}.get(info.get("launcher_open"), "?")
    return f"settings: Graphics preset {info.get('preset', '?')} ({settings}); launcher open: {launcher}"


def renderer_name(info):
    """The renderer a capture's display.json names. Captures on the Emulated renderer store
    "classic", its former name (ADR-0020); the stored value stays so old captures still read."""
    renderer = info.get("renderer", "?")
    return "emulated" if renderer == "classic" else renderer


def capture_report(args):
    started = None
    info_path = args.run / "display.json"
    info = {}
    if info_path.exists():
        info = json.loads(info_path.read_text(encoding="utf-8-sig"))
        started = parse_time(info["started"])
    frames, mode = load_presentmon(args.run / "presentmon.csv", started)
    marks = load_marks(args.run / "marks.txt")
    log_path = args.run / "runtime.log"
    events = load_log(log_path)[1] if log_path.exists() else []
    windows = split_windows(frames, [m for m, _ in marks])
    counts_by_window = None
    latencies = {}
    if log_path.exists():
        origin, swap_events = load_swap_events(
            log_path.read_text(encoding="utf-8", errors="replace").splitlines())
        pacing_path = args.run / "present_pacing.csv"
        pacing_rows = []
        if pacing_path.exists():
            with pacing_path.open(newline="", encoding="utf-8-sig") as stream:
                pacing_rows = list(csv.DictReader(stream))
        counts_by_window = swap_counts(windows, origin, swap_events, pacing_rows)
        latencies = latency_summary(windows, origin, pacing_rows)
    print(f"{args.run.name}: {renderer_name(info)}, present mode {mode}, "
          f"{len(frames)} presents, {len(marks)} marks; notes: {info.get('notes', '')}")
    if header := settings_header(info):
        print(header)
    timer_path = args.run / "gpu_timer.csv"
    gpus = gpu_summary(windows, load_gpu_timer(timer_path) if timer_path.exists() else None)
    displays, steps, tick_samples = {}, {}, []
    if log_path.exists():
        log_lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        displays = display_summary(windows, load_display_lines(log_lines))
        tick_samples = load_tick_seconds(log_lines)
        steps = game_step_summary(windows, tick_samples)
    for line in smoothness_report(windows, set(args.allow_30hz), events, counts_by_window):
        print(line)
        label = line.split(":")[0]
        if not line.startswith(" ") and label in gpus:
            print(f"  {gpus[label]}")
        if not line.startswith(" ") and label in displays:
            print(f"  {displays[label]}")
        if not line.startswith(" ") and label in steps:
            print(f"  {steps[label]}")
        if not line.startswith(" ") and label in latencies:
            print(f"  {latencies[label]}")
        window = next((w for w in windows if w.label == label and w.frames), None)
        if window and not line.startswith(" "):
            opened = [text.rsplit("\\", 1)[-1] for at, kind, text in events
                      if kind == "file open" and window.frames[0].at <= at <= window.frames[-1].at]
            if opened:
                print(f"  files opened: {', '.join(opened)}")
    counts = None
    if log_path.exists():
        counts = load_render_thread_counts(
            log_path.read_text(encoding="utf-8", errors="replace").splitlines())
    day = frames[0].at.date() if frames else datetime.date.today()
    accepted = [datetime.datetime.combine(day, datetime.datetime.strptime(text, "%H:%M:%S.%f").time())
                for text in args.accept_load]
    print("zero-stutter bars:")
    for line in judge_bars(windows, counts, accepted, counts_by_window, tick_samples).lines:
        print(f"  {line}")


def load_presents(path, origin):
    rows = []
    with path.open(newline="", encoding="utf-8-sig") as stream:
        for row in csv.DictReader(stream):
            if not row.get("interval_us") or int(row["interval_us"]) <= 0:
                continue
            at = origin + datetime.timedelta(microseconds=int(row["present_us"]))
            rows.append((at, int(row["interval_us"]) / 1000))
    return rows


def slow_frame_report(args, parser):
    origin, events = load_log(args.run / "runtime.log")
    if origin is None:
        parser.exit(1, "No paced presentation log in this run (frame pacing must be on).\n")
    presents = load_presents(args.run / "present.csv", origin)
    marks = load_marks(args.run / "marks.txt")

    def in_scope(at):
        if not marks:
            return True
        return any(-args.before <= (at - m).total_seconds() <= args.after for m, _ in marks)

    intervals = [ms for _, ms in presents]
    slow = []
    for i, (at, ms) in enumerate(presents):
        normal = statistics.median(intervals[max(0, i - 15):i + 16])
        if ms > normal + args.slack and in_scope(at):
            slow.append((at, ms, normal))

    for mark, label in marks:
        print(f"MARK {mark:%H:%M:%S.%f}"[:-3] + f"  {label}")
    print(f"{len(slow)} slow frames" + (" near marks" if marks else ""))
    for at, ms, normal in slow:
        near = [e for e in events if 0 <= (at - e[0]).total_seconds() * 1000 <= args.window]
        mark_note = ""
        if marks:
            mark, label = min(marks, key=lambda m: abs((at - m[0]).total_seconds()))
            mark_note = f"  ({(at - mark).total_seconds():+.2f} s from {label})"
        print(f"\n{at:%H:%M:%S.%f}"[:-3] + f"  {ms:.1f} ms (normal {normal:.1f}){mark_note}")
        counts = {}
        for _, kind, _ in near:
            counts[kind] = counts.get(kind, 0) + 1
        if not near:
            print("  no logged events: not a pipeline build or readback (check CPU-side work)")
        for kind, count in counts.items():
            print(f"  {count} x {kind}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", type=Path, help="analysis/capture-* or analysis/hitch-* folder")
    parser.add_argument("--allow-30hz", action="append", default=[], metavar="LABEL",
                        help="window that may run an even 30 Hz (repeatable), e.g. intro")
    parser.add_argument("--accept-load", action="append", default=[], metavar="HH:MM:SS.fff",
                        help="a hitch the owner accepts as a load boundary, by the time the "
                             "report printed for it (repeatable); the first-run and intro bars "
                             "leave it out")
    parser.add_argument("--slack", type=float, default=4.0, help="ms over the scene's normal interval")
    parser.add_argument("--window", type=float, default=250.0, help="ms of log events before a frame")
    parser.add_argument("--before", type=float, default=2.0, help="seconds before a mark")
    parser.add_argument("--after", type=float, default=20.0, help="seconds after a mark")
    args = parser.parse_args()
    if (args.run / "presentmon.csv").exists():
        capture_report(args)
    else:
        slow_frame_report(args, parser)


if __name__ == "__main__":
    main()
