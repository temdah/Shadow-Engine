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
        cls.profiles = (
            pathlib.Path(__file__).resolve().parents[1]
            / "src/modules/05_runtime_profiles.inc"
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

    def test_frame_graph_tail_is_mapped_for_every_profile(self) -> None:
        asia = corpus.parse_asia_map(self.profiles)
        self.assertEqual(len(asia), 80)
        self.assertEqual(asia[0x002D6070], 0x008F0E90)
        self.assertEqual(asia[0x003E0E70], 0x00A57940)
        self.assertEqual(asia[0x003E19E3], 0x00A584E3)
        self.assertIn(
            "#define FRAME_GRAPH_FINALIZER_RVA 0x003E0E70ULL",
            self.state)
        self.assertIn(
            "#define FRAME_GRAPH_PRE_FINALIZER_RVA 0x003E19E3ULL",
            self.state)
        self.assertIn(
            "#define SHADOW_FACE_SCHEDULER_RVA 0x002D6070ULL",
            self.state)
        self.assertNotIn("EXECUTE_FRAME_GRAPH_RVA", self.state)
        self.assertNotIn("EXECUTE_FRAME_GRAPH_RESULT_LOOP_RVA", self.state)

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
