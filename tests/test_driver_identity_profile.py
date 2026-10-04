"""Validate the optional Global driver's immutable proof against captured bytes.

This is a static profile gate, not a runtime ownership or lifetime test. The
private capture is optional outside the workspace; no binary is distributed.
"""

from pathlib import Path
import hashlib
import re
import struct
import unittest

from validate_runtime_corpus import Image


PROJECT = Path(__file__).resolve().parents[1]
HEADER = PROJECT / "src/internal/driver_identity_profile.inc"
PROFILES = PROJECT / "src/modules/05_runtime_profiles.inc"
CAPTURE = PROJECT.parent / "Support/Evidence/RuntimeCaptures/Disrupt_b64_runtime_rebuilt.dll"


def load_profile():
    source = HEADER.read_text(encoding="utf-8")
    arrays = {}
    for name, body in re.findall(
        r"static const unsigned char (\w+)\[\]\s*=\s*\{(.*?)\};", source, re.S
    ):
        arrays[name] = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})\b", body))
    spans = []
    for rva, name, size_name in re.findall(
        r"\{\s*0x([0-9A-Fa-f]+)ULL,\s*(\w+),\s*sizeof\((\w+)\)\s*\}", source
    ):
        if name != size_name:
            raise AssertionError("Proof span has another array's size")
        spans.append((int(rva, 16), arrays[name]))
    profile = re.search(
        r"static const DriverIdentityProfile g_global_driver_identity_profile\s*=\s*\{(.*?)\};",
        source, re.S,
    ).group(1)
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)U(?:LL)?", profile)]
    return arrays, spans, values


def proof_matches(read, spans, values):
    """Captured proof plus initialized class IDs; startup may precede those IDs."""
    return all(read(rva, len(blob)) == blob for rva, blob in spans) and all(
        read(values[index], 4) == struct.pack("<I", values[index + 2])
        for index in (0, 1)
    )


class DriverIdentityProfileTests(unittest.TestCase):
    def setUp(self):
        self.arrays, self.spans, self.values = load_profile()

    def capture(self):
        if not CAPTURE.is_file():
            self.skipTest("Private Global captured runtime is unavailable")
        return Image(CAPTURE, False)

    def test_only_global_profile_enables_identity(self):
        source = PROFILES.read_text(encoding="utf-8")
        from runtime_profile_fields import runtime_profile_field
        names = re.findall(r'\{\s*\d+,\s*"([^"]+)"',source)
        self.assertEqual(len(names),5)
        for name in names:
            self.assertEqual(runtime_profile_field(source,name,"driver_identity"),
                             "&g_global_driver_identity_profile" if name=="supported-04DF" else "NULL")
        self.assertIn("const DriverIdentityProfile *driver_identity;", source)

    def test_complete_proof_matches_captured_global(self):
        image = self.capture()
        self.assertEqual(hashlib.sha256(CAPTURE.read_bytes()).hexdigest(),
                         "d8004145fb17bd6a9073466ebb9bfc19589aad7d08d8ec3fd45733a5d8ad0855")
        self.assertEqual([(rva, len(blob)) for rva, blob in self.spans], [
            (0x00948370, 226), (0x00AE6530, 73),
            (0x009B5A10, 68), (0x009B6630, 68),
            (0x00849270, 116), (0x0086DF50, 233),
            (0x0093DEF0, 98), (0x00B9B3E0, 11),
        ])
        self.assertEqual(sum(len(blob) for _, blob in self.spans), 893)
        self.assertEqual(len(self.arrays), 8)
        self.assertEqual(self.values, [
            0x03B874C4, 0x03B87A34, 0x911DD85F, 0x8B611803,
        ])
        self.assertTrue(proof_matches(image.read, self.spans, self.values))

    def test_direct_predicate_receiver_and_consumed_helpers(self):
        image = self.capture()
        for call, target in ((0x009483B9, 0x009B6630),
                             (0x009483C8, 0x00849270),
                             (0x0094842A, 0x00B9B3E0),
                             (0x00AE6555, 0x009B5A10),
                             (0x00AE6564, 0x00849270),
                             (0x00AE6571, 0x00948370)):
            code = image.read(call, 5)
            self.assertEqual(code[0], 0xE8)
            self.assertEqual(call + 5 + struct.unpack("<i", code[1:])[0], target)
        for instruction, class_rva in ((0x009483BE, self.values[1]),
                                       (0x00AE655A, self.values[0])):
            code = image.read(instruction, 7)
            self.assertEqual(code[:3], b"\x48\x8d\x15")
            self.assertEqual(instruction + 7 + struct.unpack("<i", code[3:])[0], class_rva)
        self.assertEqual(image.read(0x0094837A, 7), bytes.fromhex("488b8190000000"))
        self.assertEqual(image.read(0x00948391, 3), bytes.fromhex("8b5360"))
        self.assertEqual(image.read(0x009483D2, 4), bytes.fromhex("488b5f10"))
        self.assertEqual(image.read(0x00948451, 1), b"\xc3")
        # Finish the instruction after the predicate call, not its lone prefix.
        self.assertEqual(self.arrays["g_driver_proof_direct_driver_caller"][-3:],
                         bytes.fromhex("488b1b"))

    def test_every_proof_byte_and_metadata_change_is_rejected(self):
        image = self.capture()
        cases = [(rva + offset, 1) for rva, blob in self.spans for offset in range(len(blob))]
        cases += [(self.values[index] + offset, 1) for index in (0, 1) for offset in range(4)]
        self.assertEqual(len(cases), 901)
        for corrupt_rva, mask in cases:
            def corrupted_read(rva, size):
                blob = bytearray(image.read(rva, size))
                if rva <= corrupt_rva < rva + size:
                    blob[corrupt_rva - rva] ^= mask
                return bytes(blob)
            with self.subTest(corrupt_rva=hex(corrupt_rva)):
                self.assertFalse(proof_matches(corrupted_read, self.spans, self.values))

if __name__ == "__main__":
    unittest.main()
