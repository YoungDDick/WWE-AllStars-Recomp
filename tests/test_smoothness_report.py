import datetime
import tempfile
import unittest
from pathlib import Path

from scripts.analyze_hitches import (Frame, judge_window, load_presentmon, renderer_name,
                                     split_windows, smoothness_report)

T0 = datetime.datetime(2026, 10, 3, 17, 0, 0)


def frames(intervals, displayed=None, start=T0):
    """Frames presented back to back with the given intervals (ms)."""
    out, at = [], start
    for i, ms in enumerate(intervals):
        at += datetime.timedelta(milliseconds=ms)
        out.append(Frame(at, ms, ms if displayed is None else displayed[i]))
    return out


class RendererNameTests(unittest.TestCase):
    def test_a_capture_on_the_emulated_renderer_reads_emulated(self):
        # display.json keeps "classic", the Emulated renderer's former name (ADR-0020).
        self.assertEqual(renderer_name({"renderer": "classic"}), "emulated")
        self.assertEqual(renderer_name({"renderer": "native"}), "native")
        self.assertEqual(renderer_name({"renderer": "canary"}), "canary")
        self.assertEqual(renderer_name({}), "?")


class JudgeWindowTests(unittest.TestCase):
    def test_even_60_passes_both_bars(self):
        verdict = judge_window(frames([16.67] * 600), "match")
        self.assertTrue(verdict.judder_pass)
        self.assertTrue(verdict.hitch_pass)

    def test_one_frame_held_33_ms_on_screen_fails_judder(self):
        shown = [16.67] * 300 + [33.33] + [16.67] * 299
        verdict = judge_window(frames([16.67] * 600, shown), "match")
        self.assertFalse(verdict.judder_pass)
        self.assertEqual(verdict.uneven_on_screen, 1)

    def test_a_frame_never_shown_fails_judder(self):
        shown = [16.67] * 300 + [None] + [16.67] * 299
        verdict = judge_window(frames([16.67] * 600, shown), "match")
        self.assertFalse(verdict.judder_pass)
        self.assertEqual(verdict.not_shown, 1)

    def test_repeat_then_skip_fails_judder_although_presents_are_even(self):
        verdict = judge_window(frames([16.67] * 600), "match", repeats=1, skips=1)
        self.assertFalse(verdict.judder_pass)

    def test_40_ms_frame_in_an_entrance_fails_hitch(self):
        verdict = judge_window(frames([16.67] * 300 + [40.0] + [16.67] * 300), "entrance")
        self.assertFalse(verdict.hitch_pass)
        self.assertEqual([round(h.frame_ms) for h in verdict.hitches], [40])

    def test_judder_bar_is_not_applied_outside_matches(self):
        shown = [16.67] * 300 + [33.33] + [16.67] * 299
        verdict = judge_window(frames([16.67] * 600, shown), "entrance")
        self.assertIsNone(verdict.judder_pass)

    def test_even_30_hz_passes_inside_the_allowance(self):
        verdict = judge_window(frames([33.33] * 300), "intro", allow_30hz=True)
        self.assertTrue(verdict.hitch_pass)

    def test_30_hz_fails_outside_the_allowance(self):
        verdict = judge_window(frames([33.33] * 300), "intro", allow_30hz=False)
        self.assertFalse(verdict.hitch_pass)

    def test_longer_than_30_hz_fails_inside_the_allowance(self):
        verdict = judge_window(frames([33.33] * 300 + [50.0]), "intro", allow_30hz=True)
        self.assertFalse(verdict.hitch_pass)


class SplitWindowsTests(unittest.TestCase):
    def test_marks_split_the_capture_and_route_labels_name_the_windows(self):
        sec = datetime.timedelta(seconds=1)
        marks = [T0 + 10 * sec, T0 + 20 * sec, T0 + 30 * sec, T0 + 40 * sec]
        windows = split_windows(frames([1000.0] * 60), marks)
        self.assertEqual([w.label for w in windows], ["intro", "entrance 1", "entrance 2", "match"])
        self.assertEqual([w.kind for w in windows], ["intro", "entrance", "entrance", "match"])
        self.assertEqual(len(windows[0].frames), 10)
        self.assertEqual(len(windows[3].frames), 21)  # the last mark runs to the end (40 s .. 60 s)

    def test_frames_before_the_first_mark_are_left_out(self):
        marks = [T0 + datetime.timedelta(seconds=30)]
        windows = split_windows(frames([1000.0] * 60), marks)
        self.assertEqual(len(windows), 1)
        self.assertEqual(len(windows[0].frames), 31)  # 30 s .. 60 s

    def test_without_marks_the_whole_capture_is_one_match_window(self):
        windows = split_windows(frames([16.67] * 10), [])
        self.assertEqual([(w.label, w.kind) for w in windows], [("whole capture", "match")])


class LoadPresentMonTests(unittest.TestCase):
    HEADER = ("Application,ProcessID,SwapChainAddress,PresentRuntime,SyncInterval,PresentFlags,"
              "AllowsTearing,PresentMode,CPUStartDateTime,FrameTime,CPUBusy,DisplayedTime\n")

    def test_rows_are_shifted_to_the_local_clock_of_the_marks(self):
        # PresentMon's --date_time ran exactly 2 h ahead of the marks on the owner's machine.
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "presentmon.csv"
            path.write_text(self.HEADER
                            + "x.exe,1,0x1,DXGI,0,0,0,Composed: Flip,2026-10-3 19:00:01.500000000,16.6,1,16.7\n"
                            + "x.exe,1,0x1,DXGI,0,0,0,Composed: Flip,2026-10-3 19:00:01.516600000,16.6,1,NA\n",
                            encoding="utf-8")
            rows, mode = load_presentmon(path, started=T0)
        self.assertEqual(rows[0].at, T0 + datetime.timedelta(seconds=1.5))
        self.assertAlmostEqual(rows[0].displayed_ms, 16.7)
        self.assertIsNone(rows[1].displayed_ms)
        self.assertEqual(mode, "Composed: Flip")


class ReportTests(unittest.TestCase):
    def test_report_prints_a_verdict_line_per_window(self):
        sec = datetime.timedelta(seconds=1)
        windows = split_windows(frames([16.67] * 600), [T0 + sec, T0 + 5 * sec])
        lines = smoothness_report(windows, allow_30hz=set(), events=[])
        text = "\n".join(lines)
        self.assertIn("intro", text)
        self.assertIn("entrance 1", text)
        self.assertIn("PASS", text)


if __name__ == "__main__":
    unittest.main()


class SwapCountTests(unittest.TestCase):
    def test_native_swap_events_and_paced_presents_count_per_window(self):
        from scripts.analyze_hitches import load_swap_events, swap_counts
        log = [
            "[2026-10-03 17:00:01.000] [info] [core] [t1] Paced presentation log started (present_us 0)",
            "[2026-10-03 17:00:02.000] [info] [gpu] [t2] Frame event: external frame skipped 1, sequence 50",
            "[2026-10-03 17:00:02.016] [info] [gpu] [t2] Frame event: external frame repeated, sequence 50",
            "[2026-10-03 17:00:12.000] [info] [gpu] [t2] Frame event: external frame skipped 2, sequence 90",
        ]
        origin, events = load_swap_events(log)
        self.assertEqual(origin, T0 + datetime.timedelta(seconds=1))
        # present_us is relative to the origin: one row in each window.
        pacing = [
            {"present_us": "3000000", "repeated_vblanks": "1", "frames_replaced": "0"},
            {"present_us": "12000000", "repeated_vblanks": "0", "frames_replaced": "1"},
        ]
        sec = datetime.timedelta(seconds=1)
        windows = split_windows(frames([1000.0] * 30), [T0, T0 + 10 * sec])
        counts = swap_counts(windows, origin, events, pacing)
        self.assertEqual(counts["intro"], (2, 1))       # one swap repeat + one held vblank; one skip
        self.assertEqual(counts["entrance 1"], (0, 3))  # a 2-frame swap skip + one replaced frame


class LatencyTests(unittest.TestCase):
    """Per-window latency from the swap log (present_pacing.csv)."""

    def windows(self):
        sec = datetime.timedelta(seconds=1)
        return split_windows(frames([1000.0] * 30), [T0, T0 + 10 * sec])

    def test_timer_pacing_gives_frame_to_present_only(self):
        from scripts.analyze_hitches import latency_summary
        origin = T0
        # Rows from before ticket 13 have no present-wait columns; newer timer rows leave them empty.
        rows = [{"present_us": "2000000", "latency_us": "3000"},
                {"present_us": "3000000", "latency_us": "5000", "present_id": "", "wait_us": "",
                 "unshown": "", "shown_id": "", "shown_us": "", "shown_after_present_us": ""}]
        lines = latency_summary(self.windows(), origin, rows)
        self.assertEqual(lines["intro"],
                         "latency: frame to present median 4.00 ms, p99 5.00 ms; "
                         "present to shown n/a (timer pacing)")
        self.assertNotIn("entrance 1", lines)

    def test_present_wait_rows_give_present_to_shown_and_queue(self):
        from scripts.analyze_hitches import latency_summary
        origin = T0
        rows = [
            {"present_us": "12000000", "latency_us": "4000", "present_id": "10", "wait_us": "0",
             "unshown": "0", "shown_id": "9", "shown_us": "11990000", "shown_after_present_us": "2000"},
            {"present_us": "13000000", "latency_us": "4000", "present_id": "11", "wait_us": "1500",
             "unshown": "1", "shown_id": "10", "shown_us": "12990000", "shown_after_present_us": "6000"},
            {"present_us": "14000000", "latency_us": "4000", "present_id": "12", "wait_us": "0",
             "unshown": "0", "shown_id": "", "shown_us": "", "shown_after_present_us": ""},
        ]
        lines = latency_summary(self.windows(), origin, rows)
        self.assertEqual(lines["entrance 1"],
                         "latency: frame to present median 4.00 ms, p99 4.00 ms; "
                         "present to shown median 4.00 ms, p99 6.00 ms (2 shown); "
                         "1 presents waited on the previous one (max 1.50 ms), most unshown 1")


class DisplayVblankTests(unittest.TestCase):
    def test_display_lines_summarise_per_window(self):
        from scripts.analyze_hitches import display_summary, load_display_lines
        line = ("[2026-10-03 17:00:{:02d}.000] [info] [gpu] [t1] Display vblank 1s: display {} Hz, "
                "{} refreshes held longer (variable refresh), 0 events missed; guest 60.010 Hz, "
                "guest phase 1.000 ms of 2.780 ms, drift {} ms/s")
        lines = [line.format(2, "359.600", 30, "+0.100"), line.format(3, "359.800", 20, "-0.300"),
                 line.format(15, "59.940", 0, "+1.000")]
        samples = load_display_lines(lines)
        sec = datetime.timedelta(seconds=1)
        windows = split_windows(frames([1000.0] * 30), [T0, T0 + 10 * sec])
        summary = display_summary(windows, samples)
        self.assertIn("359.70 Hz", summary["intro"])
        self.assertIn("25.0 held longer/s", summary["intro"])
        self.assertIn("59.94 Hz", summary["entrance 1"])
        self.assertIn("drift 1.000 ms/s", summary["entrance 1"])


class GameStepTests(unittest.TestCase):
    LINE = ("[2026-10-03 17:00:{:02d}.000] [info] [core] [t1] game tick 1s: {} ticks ({} at 1/30 s, "
            "0 other steps), work mean {} ms max 15.0 ms, tick max 33.3 ms, dilation 1.000..1.000")

    def summary(self, lines):
        from scripts.analyze_hitches import game_step_summary, load_tick_seconds
        sec = datetime.timedelta(seconds=1)
        windows = split_windows(frames([1000.0] * 30), [T0, T0 + 10 * sec])
        return game_step_summary(windows, load_tick_seconds(lines))

    def test_a_half_rate_window_names_the_games_own_1_30_step(self):
        summary = self.summary([self.LINE.format(2, 31, 31, "13.47"), self.LINE.format(3, 31, 31, "13.61"),
                                self.LINE.format(15, 61, 0, "5.00")])
        self.assertIn("62 ticks, 62 at 1/30 s (100.0 %, the game's half-rate)", summary["intro"])
        self.assertIn("work mean 13.5 ms", summary["intro"])
        self.assertIn("61 ticks, 0 at 1/30 s (0.0 %)", summary["entrance 1"])
        self.assertNotIn("half-rate", summary["entrance 1"])

    def test_windows_without_tick_lines_are_left_out(self):
        self.assertEqual(self.summary([]), {})


class HitchNoteTests(unittest.TestCase):
    def test_native_hitch_line_shows_renderer_and_readback_time(self):
        from scripts.analyze_hitches import Window
        line = ("native renderer: hitch at frame 900: 40.0 ms, renderer 31.5 ms, GPU fence 0.0 ms; "
                "pipelines compiled on this thread 0 (0.0 ms), draws waiting for pipelines 0; "
                "textures uploaded 0 (0.00 MB), render thread on textures 0.0 ms (waiting 0.0), "
                "reading on any thread 0.0 ms (write-watch 0.0, hash 0.0); geometry 0.00 MB; new "
                "render targets 0; readbacks queued 1 (0.2 ms), delivered 2 (28.13 MB, 25.6 ms)")
        hitch_at = T0 + datetime.timedelta(seconds=1)
        window = Window("entrance 1", "entrance", [Frame(hitch_at, 40.0, 40.0)])
        events = [(hitch_at - datetime.timedelta(milliseconds=5), "Native hitch line", line)]
        text = "\n".join(smoothness_report([window], set(), events))
        self.assertIn("Native renderer 31.5 ms, readback 25.8 ms", text)


class GameTickNoteTests(unittest.TestCase):
    def test_slow_game_tick_line_shows_work_and_limiter(self):
        from scripts.analyze_hitches import Window
        line = ("game tick: 41.0 ms, work 38.5 ms, limiter 2.5 ms; step 0.01667 s, dilation 1.000, "
                "1/30 flag 0")
        hitch_at = T0 + datetime.timedelta(seconds=1)
        window = Window("intro", "intro", [Frame(hitch_at, 41.0, 41.0)])
        events = [(hitch_at - datetime.timedelta(milliseconds=3), "Game tick line", line)]
        text = "\n".join(smoothness_report([window], set(), events))
        self.assertIn("game work 38.5 ms, limiter 2.5 ms", text)

    def test_slow_game_tick_line_shows_the_wait_on_the_render_thread(self):
        from scripts.analyze_hitches import Window
        line = ("game tick: 41.0 ms, work 2.8 ms, limiter 0.0 ms, render thread 38.2 ms, "
                "other waits 0.0 ms; step 0.01667 s, dilation 1.000, 1/30 flag 0")
        hitch_at = T0 + datetime.timedelta(seconds=1)
        window = Window("intro", "intro", [Frame(hitch_at, 41.0, 41.0)])
        events = [(hitch_at - datetime.timedelta(milliseconds=3), "Game tick line", line)]
        text = "\n".join(smoothness_report([window], set(), events))
        self.assertIn("game work 2.8 ms, limiter 0.0 ms, render thread 38.2 ms", text)


def route(intro=None, entrance=None, match=None, shown_even=False):
    """A made-up route capture: intro, entrance 1, entrance 2 and match windows of 10 s each
    at an even 60 Hz unless a window's intervals are given. With shown_even, every frame was on
    screen for 16.67 ms whatever its present interval."""
    sec = datetime.timedelta(seconds=1)
    intervals = []
    for given in (intro, entrance, entrance, match):
        intervals += given if given is not None else [16.67] * 600
    marks = [T0 + 10 * i * sec for i in range(4)]
    shown = [16.67] * len(intervals) if shown_even else None
    return split_windows(frames(intervals, shown, start=T0 - datetime.timedelta(milliseconds=1)),
                         marks)


def log(*messages):
    return [f"[2026-10-03 17:00:{i:02d}.000] [info] [gpu] [t1] {m}" for i, m in enumerate(messages)]


WARM_LOG = log("native renderer: draw skips at frame 600: none",
               "native renderer: render thread totals at frame 600: pipelines compiled 0, "
               "shader modules created 0")


class FirstRunBarTests(unittest.TestCase):
    def test_a_warm_run_passes_every_bar(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        bars = judge_bars(route(), load_render_thread_counts(WARM_LOG))
        self.assertTrue(bars.first_run_pass)
        self.assertTrue(bars.intro_pass)
        self.assertTrue(bars.match_pass)

    def test_one_pipeline_pending_skip_fails_first_run(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        counts = load_render_thread_counts(WARM_LOG + log(
            "native renderer: draw skips at frame 660: no_texture=3 pipeline_pending=1"))
        self.assertEqual(counts.pipeline_pending, 1)
        bars = judge_bars(route(), counts)
        self.assertFalse(bars.first_run_pass)
        self.assertTrue(bars.intro_pass)
        self.assertTrue(bars.match_pass)

    def test_one_render_thread_compile_fails_first_run(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        counts = load_render_thread_counts(WARM_LOG + log(
            "native renderer: render thread totals at frame 660: pipelines compiled 1, "
            "shader modules created 0"))
        self.assertEqual(counts.pipelines_compiled, 1)
        self.assertFalse(judge_bars(route(), counts).first_run_pass)

    def test_one_render_thread_module_creation_fails_first_run(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        counts = load_render_thread_counts(WARM_LOG + log(
            "native renderer: render thread totals at frame 660: pipelines compiled 0, "
            "shader modules created 1"))
        self.assertEqual(counts.modules_created, 1)
        self.assertFalse(judge_bars(route(), counts).first_run_pass)

    def test_one_render_thread_pipeline_wait_fails_first_run(self):
        # An off-screen draw waited for a worker to build its pipeline: a stall.
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        counts = load_render_thread_counts(WARM_LOG + log(
            "native renderer: render thread totals at frame 660: pipelines compiled 0, "
            "shader modules created 0, pipeline waits 1"))
        self.assertEqual(counts.pipeline_waits, 1)
        bars = judge_bars(route(), counts)
        self.assertFalse(bars.first_run_pass)
        self.assertIn("render-thread pipeline waits 1", "\n".join(bars.lines))

    def test_without_totals_lines_hitch_lines_count_the_compiles(self):
        # Builds before the totals line only logged compiles inside a hitch line.
        from scripts.analyze_hitches import load_render_thread_counts
        hitch = ("native renderer: hitch at frame 900: 40.0 ms, renderer 31.5 ms, GPU fence 0.0 ms; "
                 "pipelines compiled on this thread {} (30.0 ms), draws waiting for pipelines 0; ")
        counts = load_render_thread_counts(log(hitch.format(2), hitch.format(1)))
        self.assertEqual(counts.pipelines_compiled, 3)

    def test_live_shader_conversions_are_reported_without_failing_first_run(self):
        # Ticket 22: shaders the pack lacks are converted in the session.
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        counts = load_render_thread_counts(WARM_LOG + log(
            "shader top-up: converted vs_309cb027862dbde6.bin live in 140 ms (2 shader libraries)",
            "shader top-up: live conversions 1 converted, 0 failed, 1 waiting",
            "shader top-up: could not convert ps_69d28119d5fe8475.bin live: XenosRecomp stopped "
            "with exit code 1; the launcher converts it after the session",
            "shader top-up: live conversions 1 converted, 1 failed, 0 waiting"))
        self.assertEqual(counts.live_conversions, 1)
        self.assertEqual(counts.live_conversion_failures, 1)
        bars = judge_bars(route(), counts)
        self.assertTrue(bars.first_run_pass)
        self.assertIn("live shader conversions 1 (failed 1)", "\n".join(bars.lines))


class IntroBarTests(unittest.TestCase):
    def counts(self):
        from scripts.analyze_hitches import load_render_thread_counts
        return load_render_thread_counts(WARM_LOG)

    def test_a_30_ms_intro_frame_fails_the_intro_and_first_run_bars(self):
        from scripts.analyze_hitches import judge_bars
        bars = judge_bars(route(intro=[16.67] * 300 + [30.0] + [16.67] * 298), self.counts())
        self.assertFalse(bars.intro_pass)
        self.assertFalse(bars.first_run_pass)
        self.assertTrue(bars.match_pass)

    def test_a_30_hz_intro_cadence_without_a_tick_log_fails_the_intro_bar(self):
        # Without --wwe_tick_log lines there is no evidence the game chose the 1/30 s step.
        from scripts.analyze_hitches import judge_bars
        bars = judge_bars(route(intro=[33.33] * 300), self.counts())
        self.assertFalse(bars.intro_pass)

    def ticks(self, at_30, seconds=range(1, 11)):
        """--wwe_tick_log once-a-second lines over the intro window (17:00:00 .. 17:00:10)."""
        from scripts.analyze_hitches import load_tick_seconds
        line = ("[2026-10-03 17:00:{:02d}.000] [info] [core] [t1] game tick 1s: {} ticks ({} at "
                "1/30 s, 0 other steps), work mean 13.00 ms max 15.0 ms, tick max 33.3 ms, "
                "dilation 1.000..1.000")
        ticks = 30 if at_30 else 60
        return load_tick_seconds([line.format(s, ticks, ticks if at_30 else 0) for s in seconds])

    def test_an_even_30_hz_intro_passes_where_the_game_stepped_1_30_s(self):
        from scripts.analyze_hitches import judge_bars
        bars = judge_bars(route(intro=[33.33] * 300), self.counts(), tick_samples=self.ticks(True))
        self.assertTrue(bars.intro_pass)
        self.assertTrue(bars.first_run_pass)
        self.assertIn("the game's own step", "\n".join(bars.lines))

    def test_a_30_hz_intro_fails_where_the_game_stepped_1_60_s(self):
        from scripts.analyze_hitches import judge_bars
        bars = judge_bars(route(intro=[33.33] * 300), self.counts(), tick_samples=self.ticks(False))
        self.assertFalse(bars.intro_pass)

    def test_a_held_game_frame_fails_the_intro_bar_at_the_games_1_30_s_step(self):
        # One 30 Hz frame held a step longer: 66.7 ms on screen instead of 33.3 ms.
        from scripts.analyze_hitches import judge_bars
        bars = judge_bars(route(intro=[33.33] * 150 + [66.67] + [33.33] * 148), self.counts(),
                          tick_samples=self.ticks(True))
        self.assertFalse(bars.intro_pass)
        self.assertFalse(bars.first_run_pass)

    def test_alternating_60_and_30_hz_frames_fail_the_intro_bar_at_the_1_30_s_step(self):
        # No frame is a hitch and the median is within the step, but the frames are not each
        # one game step long.
        from scripts.analyze_hitches import judge_bars
        bars = judge_bars(route(intro=[16.67, 33.33] * 190), self.counts(),
                          tick_samples=self.ticks(True))
        self.assertFalse(bars.intro_pass)

    def test_a_second_where_the_game_switched_step_accepts_either_step(self):
        # 4.5 s at 1/60 s, then the flagged Matinee starts: second 5 holds both steps.
        from scripts.analyze_hitches import load_tick_seconds, judge_bars
        line = ("[2026-10-03 17:00:{:02d}.000] [info] [core] [t1] game tick 1s: {} ticks ({} at "
                "1/30 s, 0 other steps), work mean 13.00 ms max 15.0 ms, tick max 33.3 ms, "
                "dilation 1.000..1.000")
        seconds = [(s, 60, 0) for s in range(1, 5)] + [(5, 45, 15)] + [(s, 30, 30) for s in range(6, 11)]
        ticks = load_tick_seconds([line.format(*s) for s in seconds])
        bars = judge_bars(route(intro=[16.67] * 270 + [33.33] * 165), self.counts(),
                          tick_samples=ticks)
        self.assertTrue(bars.intro_pass)

    def test_a_30_hz_entrance_fails_the_intro_bar(self):
        from scripts.analyze_hitches import judge_bars
        bars = judge_bars(route(entrance=[33.33] * 300), self.counts())
        self.assertFalse(bars.intro_pass)


class WindowStatsTests(unittest.TestCase):
    def test_one_percent_low_is_the_frame_time_only_one_percent_of_frames_exceed(self):
        from scripts.analyze_hitches import window_stats
        # 1000 frames: the slowest 1 % (10 frames) are five 40 ms and five 20 ms frames.
        one_percent_low, worst = window_stats(frames([16.67] * 985 + [20.0] * 10 + [40.0] * 5))
        self.assertAlmostEqual(one_percent_low, 20.0)
        self.assertAlmostEqual(worst, 40.0)

    def test_stats_are_per_window(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        bars = judge_bars(route(entrance=[16.67] * 590 + [20.0] * 10),
                          load_render_thread_counts(WARM_LOG))
        text = "\n".join(bars.lines)
        self.assertIn("intro: 1 % low 16.7 ms (60.0 fps), worst 16.7 ms", text)
        self.assertIn("entrance 1: 1 % low 20.0 ms (50.0 fps), worst 20.0 ms", text)

    def test_a_one_percent_low_over_a_60_hz_frame_fails_the_match_bar(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        bars = judge_bars(route(match=[16.67] * 590 + [20.0] * 10, shown_even=True),
                          load_render_thread_counts(WARM_LOG))
        self.assertFalse(bars.match_pass)
        self.assertTrue(bars.first_run_pass)  # 20 ms is no hitch

    def test_the_match_one_percent_low_must_be_within_16_7_ms(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        slow = judge_bars(route(match=[16.67] * 590 + [17.2] * 10, shown_even=True),
                          load_render_thread_counts(WARM_LOG))
        self.assertFalse(slow.match_pass)
        self.assertIn("1 % low within 16.7 ms", "\n".join(slow.lines))
        even = judge_bars(route(match=[16.67] * 600, shown_even=True),
                          load_render_thread_counts(WARM_LOG))
        self.assertTrue(even.match_pass)


class LoadBoundaryTests(unittest.TestCase):
    def load_route(self):
        # A 400 ms load inside the intro, else an even 60 Hz.
        return route(intro=[16.67] * 300 + [400.0] + [16.67] * 276)

    def load_frame(self, windows):
        return next(f for f in windows[0].frames if f.frame_ms == 400.0)

    def test_a_load_frame_fails_first_run_and_intro_unless_accepted(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        counts = load_render_thread_counts(WARM_LOG)
        bars = judge_bars(self.load_route(), counts)
        self.assertFalse(bars.first_run_pass)
        self.assertFalse(bars.intro_pass)

    def test_an_accepted_load_boundary_is_excluded(self):
        from scripts.analyze_hitches import judge_bars, load_render_thread_counts
        windows = self.load_route()
        at = self.load_frame(windows).at
        # The owner copies the time the report printed, to the millisecond.
        accepted = [at.replace(microsecond=at.microsecond // 1000 * 1000)]
        bars = judge_bars(windows, load_render_thread_counts(WARM_LOG), accepted)
        self.assertTrue(bars.first_run_pass)
        self.assertTrue(bars.intro_pass)
        self.assertIn("accepted load boundaries 1", "\n".join(bars.lines))


class GpuLineTests(unittest.TestCase):
    """Low-end-preset ticket 02: every capture shows GPU headroom per window."""

    TIMER_HEADER = "frame,time,item,label,gpu_ms,cpu_ms\n"

    def windows(self, gpu_busy=None):
        sec = datetime.timedelta(seconds=1)
        out = frames([16.67] * 600)  # 10 s from T0
        for i, frame in enumerate(out):
            frame.gpu_busy_ms = None if gpu_busy is None else gpu_busy[i % len(gpu_busy)]
        return split_windows(out, [T0, T0 + 5 * sec])

    def timer_csv(self, tmp, rows):
        path = Path(tmp) / "gpu_timer.csv"
        path.write_text(self.TIMER_HEADER + "".join(rows), encoding="utf-8")
        return path

    def test_a_capture_with_a_timer_csv_prints_its_gpu_line_per_window(self):
        from scripts.analyze_hitches import gpu_summary, load_gpu_timer
        rows = []
        for i in range(200):
            at = T0 + datetime.timedelta(milliseconds=25 * i)  # all in the intro window
            ms = 9.0 if i == 199 else 2.0  # one slow frame: the max, but above the p99
            rows.append(f"{i},{at:%Y-%m-%d %H:%M:%S}.{at.microsecond // 1000:03d},frame,,{ms:.4f},0.5000\n")
            rows.append(f"{i},,pass,pass 1280x720 RGBA8,{ms:.4f},\n")
        for i in range(200, 250):  # entrance 1
            at = T0 + datetime.timedelta(seconds=6, milliseconds=20 * (i - 200))
            rows.append(f"{i},{at:%Y-%m-%d %H:%M:%S}.{at.microsecond // 1000:03d},frame,,4.0000,1.0000\n")
        with tempfile.TemporaryDirectory() as tmp:
            timer = load_gpu_timer(self.timer_csv(tmp, rows))
        summary = gpu_summary(self.windows(gpu_busy=[1.0]), timer)
        self.assertEqual(summary["intro"],
                         "GPU (Native timer): median 2.00 ms, p99 2.00 ms, max 9.00 ms; "
                         "renderer CPU median 0.50 ms")
        self.assertIn("median 4.00 ms", summary["entrance 1"])

    def test_without_a_timer_csv_the_gpubusy_line_is_labelled_as_such(self):
        from scripts.analyze_hitches import gpu_summary
        summary = gpu_summary(self.windows(gpu_busy=[2.0, 2.0, 2.5]), None)
        self.assertEqual(summary["intro"],
                         "GPU (PresentMon GPUBusy, includes the presenter): median 2.00 ms, "
                         "p99 2.50 ms, max 2.50 ms")

    def test_an_old_capture_with_neither_reports_as_today(self):
        from scripts.analyze_hitches import gpu_summary
        self.assertEqual(gpu_summary(self.windows(), None), {})

    def test_cpu_only_timer_rows_give_the_cpu_line_without_gpu_numbers(self):
        from scripts.analyze_hitches import gpu_summary, load_gpu_timer
        at = T0 + datetime.timedelta(seconds=1)
        stamp = f"{at:%Y-%m-%d %H:%M:%S}.000"
        with tempfile.TemporaryDirectory() as tmp:
            timer = load_gpu_timer(self.timer_csv(tmp, [f"1,{stamp},frame,,,0.7500\n"]))
        summary = gpu_summary(self.windows(gpu_busy=[2.0]), timer)
        # The device had no timestamps: GPUBusy stands in for the GPU numbers.
        self.assertEqual(summary["intro"],
                         "GPU (PresentMon GPUBusy, includes the presenter): median 2.00 ms, "
                         "p99 2.00 ms, max 2.00 ms; renderer CPU median 0.75 ms")

    def test_presentmon_gpubusy_is_read_with_each_frame(self):
        header = ("Application,ProcessID,SwapChainAddress,PresentRuntime,SyncInterval,PresentFlags,"
                  "AllowsTearing,PresentMode,CPUStartDateTime,FrameTime,CPUBusy,GPUBusy,DisplayedTime\n")
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "presentmon.csv"
            path.write_text(header
                            + "x.exe,1,0x1,DXGI,0,0,0,Composed: Flip,2026-10-3 17:00:01.500000000,16.6,1,1.5704,16.7\n"
                            + "x.exe,1,0x1,DXGI,0,0,0,Composed: Flip,2026-10-3 17:00:01.516600000,16.6,1,NA,16.7\n",
                            encoding="utf-8")
            rows, _ = load_presentmon(path, started=T0)
        self.assertAlmostEqual(rows[0].gpu_busy_ms, 1.5704)
        self.assertIsNone(rows[1].gpu_busy_ms)


class SettingsHeaderTests(unittest.TestCase):
    def test_the_capture_settings_and_launcher_state_appear_in_the_header(self):
        from scripts.analyze_hitches import settings_header
        info = {"preset": "low", "launcher_open": False,
                "settings": {"wwe_native_supersample": "false", "wwe_native_resolution_scale": "3",
                             "wwe_native_cas": "0", "wwe_native_anisotropy": "2"}}
        self.assertEqual(settings_header(info),
                         "settings: Graphics preset low (wwe_native_supersample false, "
                         "wwe_native_resolution_scale 3, wwe_native_cas 0, wwe_native_anisotropy 2); "
                         "launcher open: no")

    def test_an_older_capture_without_settings_has_no_settings_line(self):
        from scripts.analyze_hitches import settings_header
        self.assertIsNone(settings_header({"notes": "FreeSync on"}))
