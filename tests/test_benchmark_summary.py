import unittest

from scripts.benchmark_summary import (WindowMetrics, compare_variants, format_summary,
                                       min_spec_verdict)


def run(displayed=16.67, work=5.0, gpu: float | None = 2.0, low=16.8, hitches=0, work_p99=8.0, gpu_p99=3.0,
        deck=False, label="match"):
    """One capture's metrics for one window."""
    return {"deck": deck, "windows": {label: WindowMetrics(displayed, work, gpu, low, hitches,
                                                           work_p99, gpu_p99)}}


class CompareTests(unittest.TestCase):
    def test_a_difference_inside_the_baseline_spread_is_not_flagged(self):
        # Baseline first and last: GPU 2.0 and 2.4 ms, so the spread is 0.4 ms.
        summary = compare_variants([run(gpu=2.0), run(gpu=2.4)],
                                   {"low": [run(gpu=2.5), run(gpu=2.3)]}, runs_required=2)
        row = summary.rows["low"]["match"]["gpu_ms"]
        self.assertAlmostEqual(row.value, 2.4)
        self.assertAlmostEqual(row.baseline, 2.2)
        self.assertAlmostEqual(row.spread, 0.4)
        self.assertFalse(row.flagged)

    def test_a_difference_outside_the_baseline_spread_is_flagged(self):
        summary = compare_variants([run(gpu=2.0), run(gpu=2.2)],
                                   {"low": [run(gpu=1.2), run(gpu=1.3)]}, runs_required=2)
        row = summary.rows["low"]["match"]["gpu_ms"]
        self.assertTrue(row.flagged)
        self.assertAlmostEqual(row.value - row.baseline, -0.85)

    def test_identical_baselines_flag_any_difference(self):
        summary = compare_variants([run(work=5.0), run(work=5.0)],
                                   {"v3": [run(work=4.9), run(work=4.9)]}, runs_required=2)
        self.assertTrue(summary.rows["v3"]["match"]["work_ms"].flagged)

    def test_a_variant_with_too_few_runs_is_refused_with_a_reason(self):
        summary = compare_variants([run(), run()], {"low": [run()]}, runs_required=2)
        self.assertNotIn("low", summary.rows)
        self.assertEqual(summary.refused["low"], "1 run; the summary needs 2 per variant")

    def test_a_single_baseline_capture_refuses_the_whole_summary(self):
        summary = compare_variants([run()], {"low": [run(), run()]}, runs_required=2)
        self.assertEqual(summary.rows, {})
        self.assertIn("baseline", summary.refused)
        self.assertIn("first and last", summary.refused["baseline"])

    def test_a_metric_missing_from_a_capture_is_left_out_not_flagged(self):
        summary = compare_variants([run(gpu=None), run(gpu=None)],
                                   {"low": [run(gpu=1.0), run(gpu=1.0)]}, runs_required=2)
        self.assertIsNone(summary.rows["low"]["match"]["gpu_ms"])


class MinimumSpecTests(unittest.TestCase):
    def test_a_window_inside_every_bar_passes(self):
        passed, reasons = min_spec_verdict(WindowMetrics(16.67, 6.0, 9.0, 16.7, 0, 12.0, 13.5))
        self.assertTrue(passed)
        self.assertEqual(reasons, [])

    def test_each_bar_fails_on_its_own(self):
        cases = {
            "1 % low 17.5 ms is over 16.7 ms": WindowMetrics(16.67, 6.0, 9.0, 17.5, 0, 12.0, 13.0),
            "1 hitch": WindowMetrics(16.67, 6.0, 9.0, 16.7, 1, 12.0, 13.0),
            "guest work p99 14.5 ms is not under 14 ms": WindowMetrics(16.67, 6.0, 9.0, 16.7, 0, 14.5, 13.0),
            "GPU p99 15.0 ms is not under 14 ms": WindowMetrics(16.67, 6.0, 9.0, 16.7, 0, 12.0, 15.0),
        }
        for reason, metrics in cases.items():
            with self.subTest(reason):
                passed, reasons = min_spec_verdict(metrics)
                self.assertFalse(passed)
                self.assertEqual(reasons, [reason])

    def test_without_a_number_the_verdict_cannot_be_given(self):
        passed, reasons = min_spec_verdict(WindowMetrics(16.67, None, 9.0, 16.7, 0, None, 13.0))
        self.assertIsNone(passed)
        self.assertEqual(reasons, ["no guest work numbers (capture without --wwe_tick_log)"])


class FormatTests(unittest.TestCase):
    def test_captures_not_from_a_deck_are_marked_estimates(self):
        summary = compare_variants([run(), run()], {"low": [run(), run()]}, runs_required=2)
        text = format_summary(summary)
        self.assertIn("not from a Steam Deck", text)
        self.assertIn("estimate", text)

    def test_deck_captures_say_so(self):
        summary = compare_variants([run(deck=True), run(deck=True)],
                                   {"low": [run(deck=True), run(deck=True)]}, runs_required=2)
        text = format_summary(summary)
        self.assertNotIn("not from a Steam Deck", text)
        self.assertIn("Steam Deck captures", text)

    def test_flagged_differences_are_marked_in_the_table(self):
        summary = compare_variants([run(gpu=2.0), run(gpu=2.2)],
                                   {"low": [run(gpu=1.2), run(gpu=1.3)]}, runs_required=2)
        text = format_summary(summary)
        self.assertIn("| low | match | GPU | 1.25 ms | 2.10 ms | 0.20 ms | -0.85 ms * |", text)


if __name__ == "__main__":
    unittest.main()
