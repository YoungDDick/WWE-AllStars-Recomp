import unittest

from scripts.shader_pack import build_pack, parse_xenos_cache

XENOS_CACHE = """#include "shader_cache.h"
ShaderCacheEntry g_shaderCacheEntries[] = {
\t{ 0x2222, 0, 0, 4, 6, 5 },
\t{ 0x1111, 0, 0, 0, 4, 2 },
};
const uint8_t g_compressedSpirvCache[] = {40,181,47,253,};
const size_t g_spirvCacheCompressedSize = 4;
const size_t g_spirvCacheDecompressedSize = 10;
const size_t g_shaderCacheEntryCount = 2;
"""


class ShaderPackTest(unittest.TestCase):
    def test_reads_entries_sorted_and_the_compressed_spirv(self):
        cache = parse_xenos_cache(XENOS_CACHE)
        self.assertEqual(cache.entries, [(0x1111, 0, 4, 2), (0x2222, 4, 6, 5)])
        self.assertEqual(cache.compressed, bytes([40, 181, 47, 253]))
        self.assertEqual(cache.decompressed_size, 10)

    def test_rejects_a_spirv_array_of_the_wrong_size(self):
        with self.assertRaises(ValueError):
            parse_xenos_cache(XENOS_CACHE.replace("CompressedSize = 4", "CompressedSize = 5"))

    def test_writes_the_layout_the_game_reads(self):
        pack = build_pack(parse_xenos_cache(XENOS_CACHE), [(0xABCD, 0x1F)], 0x19BC9795A8B5E348)
        expected = bytes.fromhex(
            "57575350" "01000000" "48e3b5a89597bc19" "02000000" "01000000" "04000000" "0a000000"
            "1111000000000000" "00000000" "04000000" "02000000" "00000000"
            "2222000000000000" "04000000" "06000000" "05000000" "00000000"
            "cdab000000000000" "1f000000" "00000000"
            "28b52ffd")
        self.assertEqual(pack, expected)


if __name__ == "__main__":
    unittest.main()
