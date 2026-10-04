import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
D = (ROOT/'src/modules/18_vehicle_light_diagnostics.inc').read_text()
S = (ROOT/'src/modules/00_shared_config_state.inc').read_text()


class OwnerWindowTests(unittest.TestCase):
    def test_quota_preserves_total_capacity_and_later_windows(self):
        self.assertIn('#define VEHICLE_OWNER_MAX_RECORDS 256U', S)
        self.assertIn('(VEHICLE_OWNER_MAX_RECORDS/VEHICLE_CAPTURE_SAMPLES)', S)
        self.assertIn('d->owner_window_records[epoch]>=VEHICLE_OWNER_RECORDS_PER_WINDOW', D)
        self.assertIn('++d->owner_window_records[epoch]', D)

    def test_native_probes_have_time_and_count_bounds(self):
        self.assertIn('#define VEHICLE_OWNER_WINDOW_MS 100U', S)
        self.assertIn('#define VEHICLE_OWNER_PROBES_PER_WINDOW 4096U', S)
        begin = D.split('static int begin_vehicle_owner_record')[1].split('static void __fastcall')[0]
        self.assertLess(begin.index('owner_window_probes[epoch]>=VEHICLE_OWNER_PROBES_PER_WINDOW'),
                        begin.index('readable_memory'))
        self.assertIn('now-d->owner_window_tick[epoch]>=VEHICLE_OWNER_WINDOW_MS', begin)

    def test_focus_is_copied_full_handle_not_type_policy(self):
        helper = D.split('static uint32_t vehicle_owner_handle_matches')[1].split('static int begin_vehicle_owner_record')[0]
        self.assertIn('handle==UINT64_MAX', helper)
        self.assertIn('r->descriptor_handle==handle', helper)
        self.assertIn('i<PHYSICAL_QUEUE_ENTRIES', helper)
        self.assertNotIn('renderer_type', helper)
        self.assertNotIn('readable_memory', helper)
        self.assertIn('diagnostic->owner_enabled && diagnostic->resolution_enabled && diagnostic->handle_link_enabled', D)

    def test_last_window_and_explicit_ambiguity_survive(self):
        self.assertIn('GetTickCount()-d->owner_window_tick[VEHICLE_CAPTURE_SAMPLES-1U]>=VEHICLE_OWNER_WINDOW_MS', D)
        self.assertIn('STAGE_VEHICLE_OWNER_WINDOW', D)
        self.assertIn('componentMatches=%u rendererInstances=%u identityOnly=1 ownerProven=0', D)
        self.assertIn('r->epoch==(uint32_t)sample_index', D)
        self.assertEqual(D.count('install_detour('), 3)  # Existing sampling plus batch observer.


if __name__ == '__main__':
    unittest.main()
