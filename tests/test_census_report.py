import unittest

from scripts.census_report import parse, phase_of, report, window_sources

PHASES = [("menus", 0), ("entrance", 6000), ("in-ring", 8700), ("finisher", 16600), ("result", 17000)]


def guest(presents, indexed=0, draw_vertices=0, up=0, begin_indexed=0, clear_rect=0, resolve=0,
          d1a468=0, other_draws=0):
    return (f"[info] [core] draw-census guest presents={presents} indexed={indexed} "
            f"draw_vertices={draw_vertices} up={up} begin_indexed={begin_indexed} "
            f"clear_rect={clear_rect} other_draws={other_draws + d1a468} (14F68=0 150C0=0 1A468={d1a468} "
            f"21230=0 29158=0 2F338=0) indirect=0 resolve={resolve} set_rt=0")


def gpu(swaps, draw_indx=0, draw_indx_2=0):
    return (f"[info] [gpu] draw-census gpu swaps={swaps} draw_indx={draw_indx} "
            f"draw_indx_2={draw_indx_2} indirect=0 indirect_ptr=00000000..00000000")


def vulkan(swaps, points=0):
    return (f"[info] [gpu] draw-census vulkan swaps={swaps} draws=0 copies=0 no_effect=0 "
            f"memexport_vs=0 memexport_ps=0 points={points} points_memexport=0")


class WindowSourcesTests(unittest.TestCase):
    def test_every_draw_attributed_leaves_no_residual(self):
        log = parse([
            guest(60), gpu(60), vulkan(60),
            guest(120, indexed=600, draw_vertices=60, up=120, begin_indexed=60, clear_rect=300,
                  resolve=60, d1a468=60),
            gpu(120, draw_indx=900, draw_indx_2=360 + 2880), vulkan(120, points=2880),
        ])
        s = window_sources(log, 60, 120)
        self.assertEqual(s["hooked_d3d"], 12.0)        # (600 + 60 + 60) / 60
        self.assertEqual(s["user_memory"], 3.0)        # (120 + 60) / 60
        self.assertEqual(s["d3d_internal"], 54.0)      # (300 + 60 + 2880) / 60
        self.assertEqual(s["unattributed_draw_indx"], 0.0)
        self.assertEqual(s["unattributed_draw_indx_2"], 0.0)

    def test_gpu_draws_beyond_the_hooks_are_unattributed(self):
        log = parse([
            guest(60), gpu(60), vulkan(60),
            guest(120, indexed=60), gpu(120, draw_indx=60 + 3000, draw_indx_2=120), vulkan(120),
        ])
        s = window_sources(log, 60, 120)
        self.assertEqual(s["unattributed_draw_indx"], 50.0)
        self.assertEqual(s["unattributed_draw_indx_2"], 2.0)

    def test_old_logs_without_the_new_counters_read_as_zero(self):
        old = ("draw-census guest presents=120 indexed=60 up=60 clear_rect=0 other_draws=0 "
               "(14F68=0 150C0=0 1A468=0 21230=0 29158=0 2F338=0) indirect=0 resolve=0")
        log = parse([guest(60), gpu(60), vulkan(60), old, gpu(120, draw_indx=120), vulkan(120)])
        self.assertEqual(window_sources(log, 60, 120)["unattributed_draw_indx"], 0.0)


class PhaseTests(unittest.TestCase):
    def test_phase_is_the_last_one_started(self):
        self.assertEqual(phase_of(5999, PHASES), "menus")
        self.assertEqual(phase_of(6000, PHASES), "entrance")
        self.assertEqual(phase_of(16980, PHASES), "finisher")
        self.assertEqual(phase_of(99999, PHASES), "result")


class ReportTests(unittest.TestCase):
    def test_report_averages_windows_per_phase_and_keeps_peak_residual(self):
        lines = [guest(5940), gpu(5940), vulkan(5940)]
        lines += [guest(6000, indexed=60), gpu(6000, draw_indx=60), vulkan(6000)]
        lines += [guest(6060, indexed=120), gpu(6060, draw_indx=120 + 600), vulkan(6060)]
        rows = {r["phase"]: r for r in report(parse(lines), PHASES)}
        self.assertEqual(rows["entrance"]["windows"], 2)
        self.assertEqual(rows["entrance"]["hooked_d3d"], 1.0)
        self.assertEqual(rows["entrance"]["unattributed_draw_indx"], 5.0)   # (0 + 10) / 2
        self.assertEqual(rows["entrance"]["peak_unattributed"], 10.0)

    def test_callers_lines_are_collected_per_builder(self):
        log = parse(["draw-census callers DrawIndexedVertices: 82888358=1200 82EA3028=40",
                     "draw-census callers DrawIndexedVertices: 82888358=1300"])
        self.assertEqual(log.callers["DrawIndexedVertices"], {"82888358": 1300, "82EA3028": 40})


if __name__ == "__main__":
    unittest.main()
