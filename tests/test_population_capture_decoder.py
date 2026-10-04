"""Independent wire fixtures and rejection tests for the offline population reader."""

import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

FIXTURE_DIRECTORY = None

spec = importlib.util.spec_from_file_location('population_decoder', Path(__file__).resolve().parents[1] / 'tools/decode_population_capture.py')
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)


def fixture(buckets=(0, 1199), early=False):
    """Construct bytes at documented offsets independently of decoder structs."""
    header = bytearray(192)
    header[:8] = b'SEPOP01\0'
    struct.pack_into('<18I', header, 8, 1, 192, 0x01020304, 24, 576, 168, 72, 112,
                     512, 31, 8, 1200, 50, 60000, 42, 3, 7, 2)
    elapsed = 51 if early else 60000
    expected = 2 if early else 1200
    struct.pack_into('<3qQ', header, 80, 1000000, 1000000, 1000000 + elapsed * 1000, 0x123456789abcdef0)
    struct.pack_into('<12I', header, 112, elapsed, 2 if early else 1, len(buckets), len(buckets),
                     9000, 9000, 4000, 4000, 0, 0, expected - len(buckets), expected - len(buckets))
    struct.pack_into('<I', header, 172, expected)
    header[176:183] = b'2.0.87\0'
    data = header
    for sequence, bucket in enumerate(buckets, 1):
        begin = 1000000 + bucket * 50000
        manager = bytearray(576 + 168)
        struct.pack_into('<i', manager, 0, 1)
        struct.pack_into('<Q', manager, 8, (3 << 32) | (bucket + 1))
        struct.pack_into('<16I2i3q', manager, 16, sequence, 6, 1, 1, 1, 1, 1, 1,
                         1, 0, 0, 0, 0, 0, 0, 1, -200, -300, begin, begin + 50, 40)
        # A complete cache with a signed age and full-width numeric address token.
        struct.pack_into('<8IQ', manager, 112, 7, 1, 1, 8, 4, 512, 0, 21, 0xfedcba9876543210)
        struct.pack_into('<3IiQ', manager, 152, 1, 0xfedcba98, 1, -9, 0xf000000000000001)
        struct.pack_into('<6Q', manager, 576, 0x100000001, 0x200000002, 0x300000003,
                         0x400000004, 0x500000005, 0xffffffffffffffff)
        struct.pack_into('<4I2i7Ii', manager, 624, 1, 1, 1, 20, -10, -20, 1, 1, 1, 1, 0, 0, 0, -1)
        struct.pack_into('<16I', manager, 680, 63, 3, 1, 0x3f800000, 0x3f800000, 0, 1,
                         0, 0, 1000, 1920, 1080, 0x3a83126f, 0x3f800000, 0x1f80, 0)
        queue = bytearray(72 + 112)
        struct.pack_into('<i', queue, 0, 1)
        struct.pack_into('<Q', queue, 8, (3 << 32) | 0x80000000 | (bucket + 1))
        struct.pack_into('<7Ii3q', queue, 16, sequence, 6, 1, 1, 1, 1, 1, -50, begin, begin + 25, 20)
        struct.pack_into('<7Q14I', queue, 72, 1, 2, 3, 0xf000000000000004, 5, 6, 7,
                         7, 1, 1, 1, 4096, 4096, 4096, 4096, 7, 512, 512, 512, 512, 1)
        for kind, payload in ((1, manager), (2, queue)):
            data += struct.pack('<6I', kind, 24, bucket, len(payload), sequence, 0) + payload
    footer = bytearray(64)
    footer[:8] = b'SEPOEND\0'
    struct.pack_into('<6I2Q2IQ', footer, 8, 1, 64, len(buckets) * 2, len(buckets), len(buckets),
                     zlib.crc32(data), len(data), len(data) + 64, 2, expected, 0)
    return bytes(data + footer)


def recalculate_crc(data):
    struct.pack_into('<I', data, len(data) - 36, zlib.crc32(data[:-64]))
    return bytes(data)


class DecoderTests(unittest.TestCase):
    def test_full_pass_keeps_first_and_last_and_exact_scalars(self):
        capture = d.Capture(fixture())
        rows = list(capture.records())
        self.assertEqual([(r['stream'], r['bucket']) for r in rows], [('manager', 0), ('queue', 0), ('manager', 1199), ('queue', 1199)])
        manager = rows[0]
        self.assertEqual(manager['before']['owners'][0]['age'], -9)
        self.assertEqual(manager['before']['base'], 0xfedcba9876543210)
        self.assertEqual(manager['rows'][0]['entity_id'], 0xffffffffffffffff)
        self.assertEqual(manager['rows'][0]['handle'], 0x400000004)
        self.assertEqual(manager['rows'][0]['published_manager'], -10)
        self.assertEqual(manager['rows'][0]['gate']['epsilon_bits'], 0x3a83126f)
        self.assertEqual(manager['rows'][0]['cache_slot'], -1)
        self.assertEqual(manager['rows'][0]['stored_gate_result'], 0)
        self.assertEqual(manager['rows'][0]['reconstructed_gate_result'], 9)
        summary = capture.summary()
        self.assertEqual(summary['totals']['ordinary_eligible_kept_unbound'], 2)
        self.assertEqual(summary['totals']['cpu_resource_dimension_mismatches'], 0)
        self.assertEqual(summary['header']['manager_missing'], 1198)

    def test_early_stop_and_empty(self):
        self.assertEqual(d.Capture(fixture((0, 1), early=True)).header['elapsed_ms'], 51)
        self.assertEqual(list(d.Capture(fixture(())).records()), [])
        with self.assertRaisesRegex(d.CaptureError, 'order regressed'):
            d.Capture(fixture((1199, 0)))

    def test_reservation_can_cross_time_bucket(self):
        data = bytearray(fixture())
        struct.pack_into('<3q', data, 216 + 88, 1050001, 1050100, 99)
        self.assertEqual(list(d.Capture(recalculate_crc(data)).records())[0]['bucket'], 0)

    def test_corruptions_rejected_even_with_fresh_crc(self):
        original = fixture()
        edits = ((8, '<I', 2), (16, '<I', 0), (40, '<I', 513), (76, '<I', 8), (76, '<I', 0),
                 (120, '<I', 3), (152, '<I', 0), (172, '<I', 1201),
                 (192, '<I', 3), (196, '<I', 20), (200, '<I', 1200),
                 (204, '<I', 999999), (208, '<I', 0), (212, '<I', 1),
                 (216, '<i', 0), (220, '<I', 1), (224, '<Q', 999),
                 (244, '<I', 0), (112, '<I', 59999))
        for offset, fmt, value in edits:
            with self.subTest(offset=offset):
                data = bytearray(original)
                struct.pack_into(fmt, data, offset, value)
                with self.assertRaises(d.CaptureError):
                    d.Capture(recalculate_crc(data))

    def test_crc_footer_and_truncation(self):
        data = bytearray(fixture())
        data[700] ^= 1
        with self.assertRaisesRegex(d.CaptureError, 'CRC32'):
            d.Capture(bytes(data))
        for cut in (0, 7, 191, 200, 400, len(data) - 1, len(data) - 64):
            with self.subTest(cut=cut), self.assertRaises(d.CaptureError):
                d.Capture(fixture()[:cut])
        for offset in (-64, -56, -48, -32, -24, -16, -8):
            with self.subTest(footer_offset=offset):
                data = bytearray(fixture())
                data[offset] ^= 1
                with self.assertRaises(d.CaptureError):
                    d.Capture(bytes(data))

    def test_clock_anomaly_keeps_raw_values_without_claiming_timing(self):
        data = bytearray(fixture())
        struct.pack_into('<I', data, 76, 6)
        struct.pack_into('<q', data, 96, 0)
        struct.pack_into('<I', data, len(data) - 16, 6)
        capture = d.Capture(recalculate_crc(data))
        self.assertEqual(capture.header['stopped'], 0)
        self.assertIsNone(capture.summary()['timeline'][0]['elapsed_ms'])

    def test_summary_separates_policy_filtering_and_unknown_evidence(self):
        for changes, expected in (({876: 0}, 'ordinary_eligible_policy_filtered'),
                                  ({872: 0}, 'ordinary_eligible_keep_unknown'),
                                  ({264: 0}, 'unknown_native_binding_packets')):
            data = bytearray(fixture())
            for offset, value in changes.items():
                struct.pack_into('<I', data, offset, value)
            totals = d.Capture(recalculate_crc(data)).summary()['totals']
            self.assertEqual(totals[expected], 1)
            self.assertEqual(totals['ordinary_eligible_kept_unbound'], 1)

    def test_actual_native_export_fixtures(self):
        if FIXTURE_DIRECTORY is None:
            self.skipTest('pass --fixtures for actual C export integration')
        native = d.Capture.read(FIXTURE_DIRECTORY / 'population-native-copies.bin')
        rows = list(native.records())[0]['rows']
        self.assertEqual([r['handle'] for r in rows], [0x1000000aa, 0x2000000aa, 0x3000000aa])
        self.assertEqual([r['admitted'] for r in rows], [1, 0, 1])
        self.assertEqual([r['cache_slot'] for r in rows], [1, -1, -1])
        minute = d.Capture.read(FIXTURE_DIRECTORY / 'population-full-minute.bin')
        self.assertEqual(len(minute.locations), 2400)
        self.assertEqual(minute.locations[0][2], 0)
        self.assertEqual(minute.locations[-1][2], 1199)
        maximum = d.Capture.read(FIXTURE_DIRECTORY / 'population-maximum.bin')
        self.assertEqual(len(maximum.data), 108221056)
        self.assertEqual(len(maximum.locations), 2400)
        last_manager = d.decode_record(maximum.data, *maximum.locations[-2])
        self.assertEqual(last_manager['bucket'], 1199)
        self.assertEqual(len(last_manager['rows']), 512)
        for j, row in enumerate(last_manager['rows']):
            self.assertEqual(row['handle'], (1199 << 32) | j)
            self.assertEqual(row['entity_id'], 0xffffffffffffffff - j)
            self.assertEqual(row['gate']['candidate_factor_bits'], 0x7f800001 + j)
        self.assertEqual(len(d.decode_record(maximum.data, *maximum.locations[-1])['rows']), 31)
        anomaly = d.Capture.read(FIXTURE_DIRECTORY / 'population-clock-anomaly.bin')
        self.assertEqual(anomaly.header['flags'] & 6, 6)
        self.assertTrue(all(r['elapsed_ms'] is None for r in anomaly.summary()['timeline']))

    def test_gate_boundaries_and_unknown_numerics(self):
        g = list(d.Capture(fixture()).records())[0]['rows'][0]['gate']
        for changes, expected in (({'valid': 0}, 0), ({'enabled': 0}, 1),
                                  ({'descriptor_factor_bits': 0}, 2), ({'candidate_factor_bits': 0}, 3),
                                  ({'raw_type': 2}, 4), ({'raw_type': 4}, 5), ({'type3_enabled': 0}, 6),
                                  ({'raw_type': 0}, 8), ({'candidate_factor_bits': 0x7f800001}, 0),
                                  ({'threshold_bits': 0x3f800000, 'coverage': 0}, 7),
                                  ({'threshold_bits': 0x3f800000, 'coverage': 2073600}, 9),
                                  ({'threshold_bits': 0x3f800000, 'mxcsr': 0x3f80}, 0),
                                  ({'threshold_bits': 0x3f800000, 'width': 0x80000000, 'height': 1}, 9)):
            with self.subTest(changes=changes):
                self.assertEqual(d.gate_result({**g, **changes}), expected)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--fixtures', type=Path)
    args, remaining = parser.parse_known_args()
    FIXTURE_DIRECTORY = args.fixtures
    unittest.main(argv=['test_population_capture_decoder.py', *remaining])
