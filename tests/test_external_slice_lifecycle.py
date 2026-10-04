#!/usr/bin/env python3
"""Regression guards for external SliceExecute ownership transfer."""

from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


class ExternalSliceLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.shared = (ROOT / "src/modules/00_shared_config_state.inc").read_text(
            encoding="utf-8")
        cls.lifecycle = (ROOT / "src/modules/40_external_slice_results.inc").read_text(
            encoding="utf-8")
        cls.expansion = (ROOT / "src/modules/60_engine_expansion.inc").read_text(
            encoding="utf-8")
        cls.bootstrap = (ROOT / "src/modules/70_bootstrap_orchestration.inc").read_text(
            encoding="utf-8")

    def test_every_rejected_store_retires_its_result(self) -> None:
        body = function_body(self.lifecycle, "store_external_slice_result(")
        failures = body.count("count_external_slice_rejection(")
        retirements = body.count("retire_rejected_external_slice_result(")
        self.assertEqual(failures, 6)
        self.assertEqual(retirements, failures)

    def test_cycle_opens_before_producer_and_survives_builder(self) -> None:
        renderer = (ROOT / "src/modules/30_renderer_queue_diagnostics.inc").read_text()
        self.assertLess(renderer.index("begin_external_slice_producer_cycle(result_record)"),
                        renderer.index("producer_ticket=completion_diagnostic_begin_producer_batch"))
        builder = function_body(self.lifecycle, "hooked_external_frame_builder(")
        self.assertNotIn("reset_external_slice_cycle", builder)
        self.assertNotIn("producer_cycle", builder)
        store = function_body(self.lifecycle, "store_external_slice_result(")
        self.assertNotIn("builder_call", store)
        self.assertIn("extra_slice_result_cycle", store)

    def test_reset_failure_preserves_owned_results_without_wait(self) -> None:
        reset = function_body(self.lifecycle, "reset_external_slice_cycle(")
        self.assertNotIn("wait_for_external_slice_writers", reset)
        self.assertNotIn("InterlockedExchangePointer", reset)
        self.assertNotIn("&slot->extra_slice_result_written_mask,0)", reset)
        self.assertIn("return 0", reset)
        self.assertIn("producer_cycle_ready,0", reset)

    def test_cycle_identity_is_bound_before_publication(self) -> None:
        body = function_body(self.lifecycle, "begin_external_slice_producer_cycle(")
        self.assertLess(body.index("extra_slice_result_generation"),
                        body.index("producer_cycle_ready,1"))
        self.assertLess(body.index("extra_slice_result_cycle,cycle"),
                        body.index("producer_cycle_ready,1"))
        self.assertIn("producer_cycle_open_failures", body)
        self.assertNotIn("Sleep(", body)

    def test_rejection_reasons_are_aggregate_only(self) -> None:
        self.assertIn("STAGE_Y_REASONS", self.lifecycle)
        store = function_body(self.lifecycle, "store_external_slice_result(")
        self.assertNotIn("append_log", store)
        for reason in ("ZERO_GENERATION", "UNOPENED_CYCLE", "GENERATION_MISMATCH",
                       "CYCLE_MISMATCH", "QUARANTINED_CYCLE"):
            self.assertIn("EXTERNAL_REJECT_" + reason, store)

    def test_rejected_nonnull_uses_native_release_wrapper(self) -> None:
        body = function_body(
            self.lifecycle, "retire_rejected_external_slice_result(")
        self.assertIn(
            "submit_external_resources_with_context(0,resource_context,resources,1,",
            body)
        self.assertIn("slice_result_external_rejected_nonnull", body)
        self.assertIn("slice_result_external_orphan_release_resources", body)
        self.assertIn("slice_result_external_orphan_release_failures", body)

    def test_relay_passes_exact_native_resource_context(self) -> None:
        def constant(name: str) -> int:
            match = re.search(rf"^#define {name}\s+(0x[0-9A-Fa-f]+|\d+)U$",
                              self.shared, re.M)
            self.assertIsNotNone(match)
            return int(match.group(1), 0)
        self.assertEqual(constant("RENDER_QUEUE_RESOURCE_CONTEXT_OFFSET"),
                         0x27168 + constant("EXTRA_LOCAL_MAPS") * 0x24C8)
        self.assertIn(
            "#if RENDER_QUEUE_RESULT_OFFSET != "
            "RENDER_QUEUE_RESOURCE_CONTEXT_OFFSET+8U",
            self.shared)
        self.assertEqual(
            self.expansion.count(
                "memcpy(body+n,&resource_context_offset,4); n+=4;"), 2)
        self.assertEqual(
            self.expansion.count(
                "/* mov r9,[r13+resource_context_offset] */"), 2)

    def test_duplicate_index_is_not_allowed_to_overwrite_owner(self) -> None:
        body = function_body(self.lifecycle, "store_external_slice_result(")
        claim = body.index(
            "prior_mask=atomic_or_long(&slot->extra_slice_result_written_mask,bit);")
        duplicate = body.index("if(prior_mask&bit)", claim)
        publish = body.index("InterlockedExchangePointer(", duplicate)
        self.assertLess(claim, duplicate)
        self.assertLess(duplicate, publish)

    def test_normal_drain_precedes_frame_graph_finalizer(self) -> None:
        body = function_body(
            self.lifecycle,
            "consume_external_slice_results_at_frame_graph_tail(")
        self.assertIn("consume_external_slice_results(renderer,slot)", body)
        self.assertIn("if(slot)", body)
        self.assertNotIn("if(slot && pending", body)
        relay = function_body(
            self.lifecycle, "install_frame_graph_tail_relay(")
        helper_call = relay.index("memcpy(body+n,&helper,8)")
        finalizer_call = relay.index("memcpy(body+n,&finalizer,8)")
        self.assertLess(helper_call, finalizer_call)
        self.assertIn("/* movzx r8d,[rbp+68] */", relay)
        self.assertIn("/* mov rdx,[rbp+60] */", relay)
        self.assertIn("/* mov rcx,rbx */", relay)
        release = function_body(
            self.lifecycle, "hooked_external_render_record_release(")
        self.assertNotIn("consume_external_slice_results(", release)
        self.assertIn("hooked_external_render_record_release", self.bootstrap)
        self.assertIn("original_render_record_release", self.shared)
        self.assertNotIn("hooked_external_execute_frame_graph", self.bootstrap)
        self.assertNotIn("original_execute_frame_graph", self.shared)
        self.assertNotIn("original_post_call_object_renderers", self.shared)
        self.assertNotIn(
            "install_detour(ENGINE_ADDRESS(POST_CALL_OBJECT_RENDERERS_RVA)",
            self.bootstrap,
        )

    def test_frame_graph_tail_is_the_only_normal_consumer(self) -> None:
        body = function_body(
            self.lifecycle,
            "consume_external_slice_results_at_frame_graph_tail(")
        self.assertEqual(
            self.lifecycle.count("consume_external_slice_results("), 2)
        self.assertIn("consume_external_slice_results(renderer,slot)", body)
        self.assertNotIn("release_fallback", self.shared)
        self.assertNotIn("releaseFallback", self.lifecycle)

    def test_release_wrapper_is_published_before_slice_relays(self) -> None:
        wrapper = self.bootstrap.index(
            "g_shadow_engine.hooks.original_resource_wrapper=(ResourceWrapperFn)")
        native = self.bootstrap.index("install_frame_graph_tail_relay()")
        relays = self.bootstrap.index("install_slice_result_bounds_relays()")
        self.assertLess(wrapper, native)
        self.assertLess(native, relays)
        self.assertLess(wrapper, relays)

    def test_face_clamps_have_no_per_event_file_write(self) -> None:
        renderer = (ROOT / "src/modules/30_renderer_queue_diagnostics.inc").read_text(
            encoding="utf-8")
        self.assertNotIn("STAGE_F_FACE_BUDGET_CLAMP", renderer)


if __name__ == "__main__":
    unittest.main()
