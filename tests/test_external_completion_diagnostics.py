#!/usr/bin/env python3
"""Regression guards for the bounded v2.0.10 completion diagnostic."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ExternalCompletionDiagnosticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.shared = (ROOT / "src/modules/00_shared_config_state.inc").read_text(
            encoding="utf-8")
        cls.diagnostic = (
            ROOT / "src/modules/19_external_completion_diagnostics.inc"
        ).read_text(encoding="utf-8")
        cls.lifecycle = (
            ROOT / "src/modules/40_external_slice_results.inc"
        ).read_text(encoding="utf-8")
        cls.bootstrap = (
            ROOT / "src/modules/70_bootstrap_orchestration.inc"
        ).read_text(encoding="utf-8")
        cls.renderer = (
            ROOT / "src/modules/30_renderer_queue_diagnostics.inc"
        ).read_text(encoding="utf-8")

    def test_scheduler_hook_is_pass_through(self) -> None:
        self.assertIn("hooked_shadow_face_scheduler", self.diagnostic)
        self.assertIn(
            "g_shadow_engine.hooks.original_shadow_face_scheduler(renderer,queue,",
            self.diagnostic)
        self.assertNotIn("Sleep(", self.diagnostic)
        self.assertNotIn("WaitFor", self.diagnostic)

    def test_scheduler_uses_first_seen_entry_without_memory_query(self) -> None:
        self.assertIn(
            "entry_index-NATIVE_SLICE_RESULT_COUNT", self.diagnostic)
        self.assertIn("atomic_or_long(&entry->scheduled_external_mask,bit)",
                      self.diagnostic)
        self.assertIn("if(!(prior&bit))", self.diagnostic)
        self.assertNotIn("readable_memory(", self.diagnostic)
        self.assertNotIn("VirtualQuery(", self.diagnostic)
        self.assertNotIn("ordinal=InterlockedIncrement", self.diagnostic)

    def test_scheduler_records_marker_order(self) -> None:
        self.assertIn("first_seen_after_pre_finalizer", self.diagnostic)
        self.assertIn("first_seen_before_pre_finalizer", self.diagnostic)
        self.assertIn("first_seen_after_reset", self.diagnostic)
        self.assertIn("first_seen_before_reset", self.diagnostic)
        self.assertIn("scheduled_after_pre_finalizer_mask", self.diagnostic)
        self.assertIn("scheduled_after_reset_mask", self.diagnostic)

    def test_producer_epoch_wraps_complete_native_renderer_queue(self) -> None:
        begin = self.renderer.index(
            "producer_ticket=completion_diagnostic_begin_producer_batch")
        original = self.renderer.index(
            "g_shadow_engine.hooks.original_renderer_queue", begin)
        seal = self.renderer.index(
            "completion_diagnostic_seal_producer_batch", original)
        self.assertLess(begin, original)
        self.assertLess(original, seal)
        self.assertIn("argument7-0x70U", self.renderer)

    def test_scheduler_requires_active_producer_epoch(self) -> None:
        self.assertIn("active_producer_serial", self.diagnostic)
        self.assertIn("scheduler_calls_outside_producer", self.diagnostic)
        self.assertNotIn("completion_diagnostic_begin_generation", self.lifecycle)

    def test_producer_diagnostic_is_observation_only(self) -> None:
        self.assertIn("completions_before_producer_seal", self.diagnostic)
        self.assertIn("completions_after_producer_seal", self.diagnostic)
        self.assertIn("completions_changed_lifecycle", self.diagnostic)
        self.assertIn("STAGE_Z_PRODUCER_MANUAL", self.diagnostic)
        self.assertNotIn("Sleep(", self.diagnostic)
        self.assertNotIn("WaitFor", self.diagnostic)

    def test_completion_is_observed_before_lifecycle_rejection(self) -> None:
        observe = self.lifecycle.index(
            "completion_diagnostic_record_completion(inline_results,index,")
        rejection = self.lifecycle.index("if(!slot)", observe)
        self.assertLess(observe, rejection)

    def test_pre_finalizer_and_reset_masks_are_bounded(self) -> None:
        self.assertIn("completion_diagnostic_mark_pre_finalizer(record)", self.lifecycle)
        self.assertIn("completion_diagnostic_mark_reset((void *)slot->record)", self.lifecycle)
        self.assertIn("logged<32U", self.diagnostic)
        self.assertIn("STAGE_Z_COMPLETION_MANUAL_END", self.diagnostic)

    def test_diagnostic_does_not_change_resource_ownership(self) -> None:
        for forbidden in (
            "submit_external_resources", "retire_rejected_external_slice_result",
            "consume_external_slice_results", "extra_slice_results",
        ):
            self.assertNotIn(forbidden, self.diagnostic)
        self.assertIn("policyChange=0 waitAdded=0", self.diagnostic)

    def test_scheduler_site_is_preflighted_and_transactional(self) -> None:
        self.assertIn(
            "#define SHADOW_FACE_SCHEDULER_RVA 0x002D6070ULL", self.shared)
        install = self.bootstrap.index(
            "install_detour(ENGINE_ADDRESS(SHADOW_FACE_SCHEDULER_RVA)")
        commit = self.bootstrap.index("patch_transaction_commit(&transaction)", install)
        self.assertLess(install, commit)


if __name__ == "__main__":
    unittest.main()
