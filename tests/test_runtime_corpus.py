#!/usr/bin/env python3
"""Regression tests for capacity-layout derivation."""

from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import validate_runtime_corpus as corpus


class CapacityLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.state = (
            pathlib.Path(__file__).resolve().parents[1]
            / "src/modules/00_shared_config_state.inc"
        ).read_text(encoding="utf-8")
        cls.extra_maps = corpus.integer_define(cls.state, "EXTRA_LOCAL_MAPS")

    def test_current_three_region_layout(self) -> None:
        self.assertEqual(
            len(corpus.parse_tail_patches(self.state, self.extra_maps)), 46)

    def test_cached_owner_ceiling_fits_pre_reserved_vector(self) -> None:
        logical = corpus.integer_define(self.state, "TARGET_CACHE_A8")
        physical = corpus.integer_define(
            self.state, "OWNER_VECTOR_RESERVE_CAPACITY")
        self.assertLessEqual(logical, physical)

    def test_rejects_uniform_stride_for_first_mapping_region(self) -> None:
        correct = 0x270D0 + self.extra_maps * 0x24C0
        uniform = 0x270D0 + self.extra_maps * 0x24C8
        corrupted = self.state.replace(
            f"0x270D0, 0x{correct:X}", f"0x270D0, 0x{uniform:X}", 1)
        self.assertNotEqual(corrupted, self.state)
        with self.assertRaises(AssertionError):
            corpus.parse_tail_patches(corrupted, self.extra_maps)

    def test_rejects_uniform_stride_for_second_mapping_region(self) -> None:
        correct = 0x27114 + self.extra_maps * 0x24C4
        uniform = 0x27114 + self.extra_maps * 0x24C8
        corrupted = self.state.replace(
            f"0x27114, 0x{correct:X}", f"0x27114, 0x{uniform:X}", 1)
        self.assertNotEqual(corrupted, self.state)
        with self.assertRaises(AssertionError):
            corpus.parse_tail_patches(corrupted, self.extra_maps)


if __name__ == "__main__":
    unittest.main()
