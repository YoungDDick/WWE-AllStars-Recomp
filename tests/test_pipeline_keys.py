import struct
import unittest

from scripts.pipeline_keys import (KEY_SIZE, check_against_pack, format_list, merge, parse_list,
                                   read_store, retag)

CONVERTER = 0x19BC9795A8B5E348
DEVICE_TAG = struct.pack("<III16s", 0x1002, 0x744C, 0x8000, bytes(range(16)))


def key(vertex_shader: int, pixel_shader: int, topology: int = 3) -> bytes:
    body = struct.pack("<QQII", vertex_shader, pixel_shader, 0, topology)
    return body + bytes(KEY_SIZE - len(body))


def store_v3(keys: list[bytes], converter: int = CONVERTER, magic: bytes = b"WWEP") -> bytes:
    header = magic + struct.pack("<IIIQ", 3, KEY_SIZE, len(keys), converter) + DEVICE_TAG
    return header + bytes(4) + b"".join(keys)


def store_v2(keys: list[bytes], converter: int = CONVERTER) -> bytes:
    return b"SVRP" + struct.pack("<IIIQ", 2, KEY_SIZE, len(keys), converter) + b"".join(keys)


def pack(hashes: list[int]) -> bytes:
    out = bytearray(b"WWSP") + struct.pack("<IQIIII", 1, CONVERTER, len(hashes), 0, 0, 0)
    for shader_hash in sorted(hashes):
        out += struct.pack("<QIIII", shader_hash, 0, 0, 0, 0)
    return bytes(out)


class PipelineKeysTest(unittest.TestCase):
    def test_reads_the_keys_of_a_tagged_store_and_of_an_untagged_one(self):
        self.assertEqual(read_store(store_v3([key(1, 2)])), (CONVERTER, [key(1, 2)]))
        self.assertEqual(read_store(store_v2([key(1, 2), key(3, 0)])),
                         (CONVERTER, [key(1, 2), key(3, 0)]))

    def test_reads_a_store_written_before_the_magic_was_this_games(self):
        self.assertEqual(read_store(store_v3([key(1, 2)], magic=b"SVRP")), (CONVERTER, [key(1, 2)]))
        with self.assertRaisesRegex(ValueError, "not a pipeline store"):
            read_store(store_v3([key(1, 2)], magic=b"XXXX"))

    def test_refuses_a_store_of_another_format(self):
        old = b"SVRP" + struct.pack("<IIIQ", 1, KEY_SIZE, 1, CONVERTER) + key(1, 2)
        with self.assertRaisesRegex(ValueError, "version 1"):
            read_store(old)

    def test_merging_stores_into_the_list_keeps_each_key_once(self):
        text, report = merge(None, [store_v3([key(1, 2), key(3, 4)]), store_v2([key(3, 4)])],
                             CONVERTER)
        self.assertEqual(parse_list(text), (CONVERTER, sorted([key(1, 2), key(3, 4)])))
        self.assertEqual(report.added, 2)

    def test_merging_the_same_stores_again_changes_nothing(self):
        stores = [store_v3([key(1, 2)]), store_v2([key(5, 6)])]
        first, _ = merge(None, stores, CONVERTER)
        second, report = merge(first, stores, CONVERTER)
        self.assertEqual(first, second)
        self.assertEqual(report.added, 0)

    def test_a_store_of_another_converter_is_skipped_and_reported(self):
        text, report = merge(None, [store_v3([key(1, 2)], converter=0x1234)], CONVERTER)
        self.assertEqual(parse_list(text), (CONVERTER, []))
        self.assertEqual(report.other_converter, 1)

    def test_the_list_holds_only_fixed_size_hex_keys(self):
        text = format_list(CONVERTER, [key(1, 2), key(3, 0)])
        lines = [line for line in text.splitlines() if line and not line.startswith("#")]
        self.assertEqual(lines[:3], ["wwe-pipeline-keys 1", f"key-size {KEY_SIZE}",
                                     f"converter {CONVERTER:016x}"])
        for line in lines[3:]:
            self.assertRegex(line, f"^[0-9a-f]{{{2 * KEY_SIZE}}}$")

    def test_a_list_line_that_is_not_a_key_is_refused(self):
        text = format_list(CONVERTER, [key(1, 2)]) + "03021e0f\n"
        with self.assertRaises(ValueError):
            parse_list(text)

    def test_reports_keys_whose_shaders_left_the_pack(self):
        text = format_list(CONVERTER, [key(1, 2), key(1, 0), key(1, 9), key(8, 2)])
        missing = check_against_pack(text, pack([1, 2]))
        self.assertEqual(sorted(missing), sorted([key(1, 9), key(8, 2)]))

    def test_shaders_in_top_up_packs_count_like_the_game_merges_them(self):
        # The game merges <cache_root>/shader-topup/*.shaderpack into the pack it loads.
        text = format_list(CONVERTER, [key(1, 2), key(1, 9), key(8, 2)])
        missing = check_against_pack(text, pack([1, 2]), [pack([9])])
        self.assertEqual(missing, [key(8, 2)])

    def test_retag_carries_the_list_to_a_converter_whose_keys_mean_the_same(self):
        # colour-original 02: the patch only adds a spec-constant bit no listed key sets.
        text = format_list(CONVERTER, [key(1, 2), key(3, 0)])
        new = 0x0123456789ABCDEF
        self.assertEqual(parse_list(retag(text, CONVERTER, new)), (new, [key(1, 2), key(3, 0)]))

    def test_retag_refuses_a_list_of_another_converter(self):
        text = format_list(0x1111, [key(1, 2)])
        with self.assertRaisesRegex(ValueError, "converter 0000000000001111"):
            retag(text, CONVERTER, 0x2222)


if __name__ == "__main__":
    unittest.main()
