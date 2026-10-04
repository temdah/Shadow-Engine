#!/usr/bin/env python3
"""Static ownership and passthrough checks for the F10 diagnostic."""

from __future__ import annotations

import pathlib
import re
import unittest

from validate_vehicle_resolution import verify_source


ROOT = pathlib.Path(__file__).resolve().parents[1]
SHARED = (ROOT / "src/modules/00_shared_config_state.inc").read_text(encoding="utf-8")
DIAGNOSTIC = (ROOT / "src/modules/18_vehicle_light_diagnostics.inc").read_text(encoding="utf-8")
MANAGER = (ROOT / "src/modules/20_manager_owner_profile.inc").read_text(encoding="utf-8")
RENDERER = (ROOT / "src/modules/30_renderer_queue_diagnostics.inc").read_text(encoding="utf-8")
RESOURCE_DIAGNOSTIC = (ROOT / "src/modules/18_shadow_resolution_diagnostics.inc").read_text(encoding="utf-8")


class VehicleLightDiagnosticTests(unittest.TestCase):
    def test_capture_is_bounded(self) -> None:
        self.assertIn("#define VEHICLE_CAPTURE_SAMPLES 4U", SHARED)
        self.assertIn("#define VEHICLE_CAPTURE_MAX_CANDIDATES 256U", SHARED)
        self.assertIn("index<VEHICLE_CAPTURE_MAX_CANDIDATES", DIAGNOSTIC)
        self.assertIn("sample_index>=(LONG)VEHICLE_CAPTURE_SAMPLES", DIAGNOSTIC)

    def test_manager_uses_transactional_limiter_and_retains_capture_bracketing(self) -> None:
        original = "g_shadow_engine.hooks.original_manager(manager,vehicle_limiter.head,"
        self.assertEqual(MANAGER.count(original), 1)
        self.assertLess(MANAGER.index("begin_vehicle_candidate_sample("), MANAGER.index(original))
        self.assertLess(MANAGER.index("prepare_vehicle_limiter_chain("), MANAGER.index(original))
        self.assertGreater(MANAGER.index("restore_vehicle_limiter_chain("), MANAGER.index(original))
        self.assertGreater(MANAGER.index("finish_vehicle_candidate_sample("), MANAGER.index(original))
        assignment = re.compile(r"\*\s*\(\s*void\s*\*\*\s*\)\s*[^;=]+(?<![!<>=])=(?!=)")
        limiter = DIAGNOSTIC[DIAGNOSTIC.index("static void prepare_vehicle_limiter_chain"):]
        self.assertRegex(limiter, assignment)
        capture = DIAGNOSTIC[DIAGNOSTIC.index("static VehicleCaptureTicket begin_vehicle_candidate_sample"):]
        self.assertNotRegex(capture, assignment)
        self.assertRegex('*(void **)target = value;', assignment)
        self.assertNotRegex('*(void **)target != value;', assignment)
        self.assertNotRegex('*(void **)target == value;', assignment)

    def test_flush_uses_copied_records(self) -> None:
        flush = DIAGNOSTIC[
            DIAGNOSTIC.index("flush_vehicle_candidate_capture") :
            DIAGNOSTIC.index("schedule_vehicle_candidate_capture_flush")
        ]
        self.assertIn("diagnostic->samples", flush)
        self.assertNotIn("readable_memory", flush)
        self.assertNotIn("bindings", flush)

    def test_menu_request_is_consumed_once(self) -> None:
        self.assertNotIn("get_async_key_state(VK_F10)", DIAGNOSTIC)
        self.assertIn("InterlockedExchange(&g_shadow_engine.internal_tools.capture_requests,0)", RENDERER)
        self.assertIn("if(requests&SHADOW_CAPTURE_VEHICLE) arm_vehicle_candidate_capture();", RENDERER)
        self.assertIn("poll_vehicle_candidate_capture_key();", RENDERER)

    def test_resolution_proof_data_is_independent(self) -> None:
        verify_source(SHARED)
        with self.assertRaises(AssertionError):
            verify_source(SHARED.replace('VEHICLE_QUEUE_WIDTH_OFFSET 0x20A4U',
                                         'VEHICLE_QUEUE_WIDTH_OFFSET 0x20A8U'))

    def test_renderer_sample_brackets_only_native_call(self) -> None:
        original = 'g_shadow_engine.hooks.original_renderer_queue('
        self.assertEqual(RENDERER.count(original), 1)
        self.assertLess(RENDERER.index('begin_vehicle_queue_sample('), RENDERER.index(original))
        self.assertGreater(RENDERER.index('finish_vehicle_queue_sample('), RENDERER.index(original))
        sample = DIAGNOSTIC[DIAGNOSTIC.index('static VehicleQueueTicket begin_vehicle_queue_sample'):]
        self.assertNotIn('install_detour(', sample)
        self.assertNotIn('WaitFor', DIAGNOSTIC)
        self.assertNotIn('VirtualProtect', DIAGNOSTIC)

    def test_resolution_is_opt_in_and_preserves_f10(self) -> None:
        sample = DIAGNOSTIC[DIAGNOSTIC.index('static VehicleQueueTicket begin_vehicle_queue_sample'):]
        self.assertLess(sample.index('d->state'), sample.index('readable_memory'))
        self.assertLess(sample.index('!d->resolution_enabled'), sample.index('readable_memory'))
        self.assertIn('!d->resolution_enabled || d->queue_sample_count', DIAGNOSTIC)
        self.assertIn('index<limit', sample)
        self.assertIn('PHYSICAL_QUEUE_ENTRIES', sample)

    def test_frozen_worker_does_not_follow_engine_pointers(self) -> None:
        flush = DIAGNOSTIC[DIAGNOSTIC.index('flush_vehicle_candidate_capture'):
                           DIAGNOSTIC.index('schedule_vehicle_candidate_capture_flush')]
        self.assertNotIn('readable_memory', flush)
        self.assertNotRegex(flush, r'\*\s*\(\s*(?:void|uint32_t|float)\s*\*')
        self.assertIn('gpuDescriptorRead=0 ownerProven=0', flush)
        resource_flush = RESOURCE_DIAGNOSTIC[RESOURCE_DIAGNOSTIC.index('static const char *shadow_resource_transition'):]
        self.assertNotIn('readable_memory', resource_flush)
        self.assertNotIn('memcpy', resource_flush)
        self.assertNotRegex(resource_flush, r'\*\s*\(\s*(?:void|uint32_t|float)\s*\*')
        self.assertIn('allocationGenerationKnown=0', resource_flush)
        sampler = DIAGNOSTIC[DIAGNOSTIC.index('static VehicleQueueTicket begin_vehicle_queue_sample'):]
        self.assertLess(sampler.index('record->submitted && copy_vehicle_candidate'),
                        sampler.index('copy_shadow_resource_before'))
        self.assertNotIn('copy_shadow_resource_before', sampler[sampler.index('static void finish_vehicle_queue_sample'):])

if __name__ == "__main__":
    unittest.main()
