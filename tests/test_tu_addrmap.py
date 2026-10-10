import unittest

from scripts.tu_addrmap import ROOT, AddressMap, load_image, longest_increasing, rewrite

BASE_IMAGE = ROOT / "analysis/xex/default.pe"
TU2_IMAGE = ROOT / "analysis/xex/tu2.pe"


class RewriteTests(unittest.TestCase):
    def test_keeps_prefix_and_hex_case(self):
        text = "0x8241A8E0 = {}  # sub_8241A8E0, loc_8241a8e0"
        self.assertEqual(rewrite(text, {0x8241A8E0: 0x8241A7E0}),
                         "0x8241A7E0 = {}  # sub_8241A7E0, loc_8241a7e0")

    def test_maps_every_address_once(self):
        # A chain a->b, b->c must not turn a into c.
        self.assertEqual(rewrite("0x82000010 0x82000020", {0x82000010: 0x82000020, 0x82000020: 0x82000030}),
                         "0x82000020 0x82000030")

    def test_maps_bare_upper_case_hook_arguments_but_not_identifiers(self):
        text = "WWE_PROBE_HOOK(82000010, x) // 82000010\nTrace_82000010 Draw82000010 82000010ab"
        self.assertEqual(rewrite(text, {0x82000010: 0x82000020}),
                         "WWE_PROBE_HOOK(82000020, x) // 82000020\nTrace_82000010 Draw82000010 82000010ab")

    def test_maps_the_imp_name_a_hook_calls(self):
        self.assertEqual(rewrite("__imp__sub_82000010(ctx, base);", {0x82000010: 0x82000020}),
                         "__imp__sub_82000020(ctx, base);")

    def test_leaves_unmapped_and_longer_numbers_alone(self):
        self.assertEqual(rewrite("0x82000010 0x8200001000 0x82000099", {0x82000010: 0x82000020}),
                         "0x82000020 0x8200001000 0x82000099")


class LongestIncreasingTests(unittest.TestCase):
    def test_drops_crossing_pairs(self):
        self.assertEqual(longest_increasing([(0, 0), (1, 5), (2, 1), (3, 2), (4, 6)]),
                         [(0, 0), (2, 1), (3, 2), (4, 6)])


@unittest.skipUnless(BASE_IMAGE.exists() and TU2_IMAGE.exists(),
                     "needs analysis/xex/default.pe and tu2.pe (build.ps1 -Action Image)")
class TwoImageTests(unittest.TestCase):
    """Against the real base v5 and TU2 images (game data, never in Git)."""

    @classmethod
    def setUpClass(cls):
        cls.map = AddressMap(load_image(BASE_IMAGE), load_image(TU2_IMAGE))

    def test_moved_function(self):
        mapping = self.map.map(0x8337B858)
        self.assertEqual((mapping.new, mapping.method), (0x8337E780, "function"))

    def test_hook_inside_a_function_keeps_its_offset(self):
        # The enhanced-vibration hit hook. Its function has switch jump tables inline,
        # whose entries move with the function and are not operand changes.
        mapping = self.map.map(0x82AE392C)
        self.assertEqual((mapping.new, mapping.method), (0x82AE63D4, "function"))

    def test_matched_function_with_a_changed_constant_is_flagged(self):
        # li r5,4 became li r5,3 at +0x1C.
        mapping = self.map.map(0x8241EF80)
        self.assertEqual((mapping.new, mapping.method), (0x8241EE80, "function*"))
        self.assertEqual(mapping.note, "1 operand(s) differ in sub_8241EF80")

    def test_changed_function_maps_by_slot_only_at_its_start(self):
        mapping = self.map.map(0x82A5F000)
        self.assertEqual((mapping.new, mapping.method), (0x82A617D8, "function*"))
        self.assertEqual(mapping.note, "matched by slot, code changed")

    def test_data_address(self):
        mapping = self.map.map(0x82197B78)  # a vtable the vibration code compares against
        self.assertEqual((mapping.new, mapping.method), (0x82197DE8, "data-ref"))

    def test_vtable_thunk_maps_to_the_copy_whose_branch_reaches_the_mapped_target(self):
        # addi r3,r3,-4; b 0x833706C8 (itself addi r3,r3,4; b 0x8337B858). With immediates
        # masked every thunk in the run looks alike; only the branch chain tells them apart.
        self.assertEqual(self.map.map(0x83370748).new, 0x83373588)
        self.assertEqual(self.map.map(0x833706C8).new, 0x8336D5E0)

    def test_code_removed_in_tu2_is_unmappable(self):
        mapping = self.map.map(0x82410278 + 0x20)
        self.assertEqual((mapping.new, mapping.method), (None, "unmapped"))

    def test_address_outside_the_image(self):
        self.assertEqual(self.map.map(0x83F00000).method, "outside image")


if __name__ == "__main__":
    unittest.main()
