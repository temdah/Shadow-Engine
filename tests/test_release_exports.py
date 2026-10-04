"""Synthetic PE export regressions; no proprietary fixtures or DLL execution."""
import struct
import unittest

from validate_release import read_exports, EXPECTED_EXPORTS
from validate_runtime_corpus import Image


def fixture():
    image = Image.__new__(Image)
    image.path = type("PathLabel", (), {"name": "synthetic.asi"})()
    image.mapped = False
    image.sections = [(0x1000, 0x600, 0x200, 0x600)]
    image.data = bytearray(0x800)
    image.data[:2] = b"MZ"
    struct.pack_into("<I", image.data, 0x3C, 0x80)
    image.data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", image.data, 0x84, 0x8664)
    struct.pack_into("<HH", image.data, 0x94, 0xF0, 0x2000)
    struct.pack_into("<H", image.data, 0x98, 0x20B)
    struct.pack_into("<I", image.data, 0x98 + 108, 16)
    struct.pack_into("<II", image.data, 0x98 + 112, 0x1000, 0x100)
    names = list(EXPECTED_EXPORTS)
    struct.pack_into("<IIIIII", image.data, 0x210, 1, len(names), len(names),
                     0x1040, 0x1060, 0x1080)
    struct.pack_into("<" + "I" * len(names), image.data, 0x240,
                     *(0x1200 + 0x10 * index for index in range(len(names))))
    string_rvas = []
    cursor = 0x10A0
    for name in names:
        encoded = name.encode("ascii") + b"\0"
        offset = 0x200 + cursor - 0x1000
        image.data[offset:offset + len(encoded)] = encoded
        string_rvas.append(cursor)
        cursor += len(encoded)
    struct.pack_into("<" + "I" * len(names), image.data, 0x260, *string_rvas)
    struct.pack_into("<" + "H" * len(names), image.data, 0x280, *range(len(names)))
    return image


class ExportTests(unittest.TestCase):
    def test_exact_exports_pass(self):
        self.assertEqual(read_exports(fixture()), EXPECTED_EXPORTS)

    def test_header_mismatches_reject(self):
        for offset, fmt, value in ((0x84, "H", 0x14C), (0x96, "H", 0),
                                   (0x98, "H", 0x10B), (0x94, "H", 16),
                                   (0x98 + 108, "I", 0)):
            with self.subTest(offset=offset):
                image = fixture()
                struct.pack_into("<" + fmt, image.data, offset, value)
                with self.assertRaises(ValueError):
                    read_exports(image)

    def test_missing_or_extra_exports_reject(self):
        for offset, value in ((0x210, 0), (0x214, 6), (0x218, 1)):
            image = fixture()
            struct.pack_into("<I", image.data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                read_exports(image)

    def test_wrong_names_or_ordinals_reject(self):
        image = fixture()
        image.data[0x2A0] = ord("X")
        with self.assertRaises(ValueError):
            read_exports(image)
        image = fixture()
        struct.pack_into("<HH", image.data, 0x280, 1, 0)
        with self.assertRaises(ValueError):
            read_exports(image)

    def test_duplicate_ordinal_rejects(self):
        image = fixture()
        struct.pack_into("<H", image.data, 0x282, 0)
        with self.assertRaises(ValueError):
            read_exports(image)

    def test_null_forwarded_or_unmapped_target_rejects(self):
        for target in (0, 0x1080, 0x9000):
            image = fixture()
            struct.pack_into("<I", image.data, 0x240, target)
            with self.subTest(target=target), self.assertRaises((ValueError, AssertionError)):
                read_exports(image)

    def test_truncated_directory_rejects(self):
        image = fixture()
        struct.pack_into("<I", image.data, 0x98 + 116, 20)
        with self.assertRaises(ValueError):
            read_exports(image)


if __name__ == "__main__":
    unittest.main()
