import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
D = (ROOT/'src/modules/18_vehicle_light_diagnostics.inc').read_text()
S = (ROOT/'src/modules/00_shared_config_state.inc').read_text()


class VehicleIdentityLineageTests(unittest.TestCase):
    def test_registry_is_bounded_and_owned_by_module18(self):
        self.assertIn('#define VEHICLE_IDENTITY_MAX_RECORDS 256U', S)
        self.assertIn('VehicleIdentityRecord identity_records[VEHICLE_IDENTITY_MAX_RECORDS];', S)
        update = D.split('static void update_vehicle_identity_registry')[1].split('static DWORD WINAPI flush_')[0]
        self.assertIn('d->identity_count>=VEHICLE_IDENTITY_MAX_RECORDS', update)
        self.assertIn('++d->identity_overflow', update)

    def test_worker_retains_tokens_without_dereference(self):
        update = D.split('static void update_vehicle_identity_registry')[1].split('static DWORD WINAPI flush_')[0]
        self.assertNotIn('readable_memory', update)
        self.assertNotIn('TlsGetValue', update)
        self.assertNotRegex(update, r'\*\s*\(\s*(?:void|uint32_t|uint64_t)\s*\*')
        self.assertIn('opaqueTokensOnly=1 nativeLifetimeProven=0', update)

    def test_registry_persists_between_capture_arms(self):
        arm = D.split('static void arm_vehicle_candidate_capture')[1].split('static void poll_')[0]
        self.assertNotIn('identity_records', arm)
        self.assertNotIn('identity_count=0', arm)
        self.assertIn('update_vehicle_identity_registry(diagnostic,serial);', D)

    def test_absence_and_reuse_are_not_promoted_to_native_truth(self):
        self.assertIn('absenceMeansNotObserved=1 destructionProven=0 ownerProven=0', D)
        self.assertIn('currentComponentVariants=', D)
        self.assertIn('priorComponentVariants=', D)
        self.assertIn('currentHandleIdentities=', D)
        self.assertIn('priorHandleIdentities=', D)
        self.assertIn('groupingLeadOnly=1 ownerProven=0', D)

    def test_no_new_hook_or_render_policy(self):
        self.assertEqual(D.count('install_detour('), 3)  # Lineage itself still installs none.
        self.assertNotIn('WaitFor', D)
        self.assertNotIn('VirtualProtect', D)


if __name__ == '__main__':
    unittest.main()
