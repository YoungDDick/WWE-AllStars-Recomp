import tempfile
import unittest
from pathlib import Path

from scripts.pgo_profile import NoProfiles, find_raw_profiles, merge_command


class FindRawProfilesTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, name, size):
        path = self.dir / name
        path.write_bytes(b"\0" * size)
        return path

    def test_lists_the_non_empty_raw_profiles_in_name_order(self):
        b = self.write("default_222.profraw", 64)
        a = self.write("default_111.profraw", 32)
        self.write("default_333.profraw", 0)  # a run killed before it wrote its counters
        self.write("notes.txt", 10)
        self.assertEqual(find_raw_profiles(self.dir), [a, b])

    def test_no_usable_profile_is_an_error_that_says_how_to_get_one(self):
        self.write("default_333.profraw", 0)
        with self.assertRaises(NoProfiles) as caught:
            find_raw_profiles(self.dir)
        self.assertIn("window's X", str(caught.exception))


class MergeCommandTests(unittest.TestCase):
    def test_merges_every_raw_profile_into_the_output_with_the_pinned_llvm_profdata(self):
        command = merge_command([Path("r/a.profraw"), Path("r/b.profraw")], Path("o/route.profdata"),
                                llvm_profdata=Path("llvm/llvm-profdata.exe"))
        self.assertEqual(command, ["llvm/llvm-profdata.exe", "merge", "-o", "o/route.profdata",
                                   "r/a.profraw", "r/b.profraw"])
