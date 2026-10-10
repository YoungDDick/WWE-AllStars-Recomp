import re
import unittest

from scripts.tu_addrmap import ROOT, load_image

TU2_IMAGE = ROOT / "analysis/xex/tu2.pe"
HEADER = (ROOT / "src/image_check.h").read_text(encoding="utf-8")

try:
    import xxhash
except ImportError:  # pip install xxhash
    xxhash = None


def constant(name):
    return int(re.search(rf"{name} = (0x[0-9A-Fa-f]+)", HEADER).group(1), 16)


@unittest.skipUnless(TU2_IMAGE.exists() and xxhash, "needs analysis/xex/tu2.pe and the xxhash module")
class ImageCheckTests(unittest.TestCase):
    """src/image_check.h must describe the TU2 image (build.ps1 -Action Image)."""

    def test_text_range_is_the_tu2_text_section(self):
        _, start, size = next(s for s in load_image(TU2_IMAGE).sections if s[0] == ".text")
        self.assertEqual((constant("kTextStart"), constant("kTextSize")), (start, size))

    def test_text_hash_is_the_tu2_text(self):
        data = TU2_IMAGE.read_bytes()
        offset = constant("kTextStart") - 0x82000000
        text = data[offset:offset + constant("kTextSize")]
        self.assertEqual(xxhash.xxh3_64_intdigest(text), constant("kTextHash"))


if __name__ == "__main__":
    unittest.main()
