import pathlib
import unittest
from validate_vehicle_ownership import verify_source, verify_image, PROOFS

ROOT = pathlib.Path(__file__).resolve().parents[1]
DIAGNOSTIC = (ROOT/'src/modules/18_vehicle_light_diagnostics.inc').read_text()
PROFILES = (ROOT/'src/modules/05_runtime_profiles.inc').read_text()
BOOTSTRAP = (ROOT/'src/modules/70_bootstrap_orchestration.inc').read_text()

class VehicleOwnershipTests(unittest.TestCase):
    def test_explicit_policy_hook_allowance_stays_strict(self):
        from validate_refactor import validate_hook_targets
        base = {'hooked_existing', 'hooked_external_post_call_object_renderers'}
        stable = {'hooked_existing', 'hooked_shadow_face_scheduler', 'hooked_intersection_lookup'}
        observer = {'hooked_vehicle_light_update', 'hooked_vehicle_light_transform',
                    'hooked_vehicle_light_batch', 'hooked_vehicle_retire'}
        validate_hook_targets(base, stable, True, False)
        validate_hook_targets(base, stable | observer, True, True)
        for candidate, enabled in [(stable | observer, False), (stable, True),
                                   (stable | observer | {'hooked_unexpected'}, True),
                                   (stable | {'hooked_vehicle_light_update'}, True)]:
            with self.assertRaises(AssertionError):
                validate_hook_targets(base, candidate, True, enabled)

    def test_independent_source_contract(self):
        verify_source(DIAGNOSTIC, PROFILES)
        with self.assertRaises(AssertionError):
            verify_source(DIAGNOSTIC, PROFILES.replace('0x00BA7570ULL', '0x00BA7571ULL'))

    def test_original_call_abi_and_passthrough(self):
        self.assertEqual(DIAGNOSTIC.count('d->original_light_update(component,skeleton,world_transform,delta,force_update);'), 1)
        self.assertEqual(DIAGNOSTIC.count('d->original_light_transform(light,spatial,matrix);'), 1)
        self.assertIn('void *world_transform,float delta,unsigned char force_update)', DIAGNOSTIC)

    def test_capture_scope_and_frozen_worker(self):
        self.assertIn('scope->serial==d->serial', DIAGNOSTIC)
        self.assertIn('scope->light!=light', DIAGNOSTIC)
        self.assertIn('recorded?&scope:NULL', DIAGNOSTIC)
        self.assertIn('TlsSetValue(d->owner_tls,previous)', DIAGNOSTIC)
        flush = DIAGNOSTIC.split('static DWORD WINAPI flush_vehicle_candidate_capture')[1].split('static void schedule_')[0]
        self.assertIn('diagnostic->owner_records', flush)
        self.assertNotIn('TlsGetValue', flush)
        self.assertNotIn('readable_memory', flush)

    def test_bounded_no_wait_no_live_resolver_call(self):
        self.assertIn('d->owner_count==VEHICLE_OWNER_MAX_RECORDS', DIAGNOSTIC)
        self.assertIn('epoch>=VEHICLE_CAPTURE_SAMPLES', DIAGNOSTIC)
        self.assertNotIn('Sleep(', DIAGNOSTIC)
        self.assertNotIn('WaitFor', DIAGNOSTIC)
        self.assertEqual(DIAGNOSTIC.count('install_detour('), 3)

    def test_copied_field_offsets(self):
        import re
        shared = (ROOT/'src/modules/00_shared_config_state.inc').read_text()
        for field, value in {'COMPONENT_BYTES': 0x120, 'SETTINGS_OFFSET': 8,
                             'LIGHT_OFFSET': 0x10, 'BONE_OFFSET': 0x11C,
                             'LIGHT_BYTES': 0x3A, 'SPATIAL_HANDLE_OFFSET': 0x20,
                             'DESCRIPTOR_HANDLE_OFFSET': 0x28, 'ENABLED_OFFSET': 0x38,
                             'SECONDARY_OFFSET': 0x39}.items():
            match = re.search(r'#define VEHICLE_OWNER_' + field + r' 0x([0-9A-F]+)U', shared)
            self.assertIsNotNone(match)
            self.assertEqual(int(match[1], 16), value)

    def test_preflight_before_mutation_and_rollback(self):
        install = BOOTSTRAP[BOOTSTRAP.index('validate_vehicle_owner_sites();'):]
        self.assertLess(install.index('validate_vehicle_owner_sites();'), install.index('patch_transaction_begin('))
        self.assertIn('rollback_vehicle_owner_observer();', install)

    def test_each_byte_span_rejects_corruption(self):
        import struct
        class Synthetic:
            def __init__(self): self.data = bytearray(0x1000)
            def read(self, addr, size):
                assert 0 <= addr <= len(self.data)-size
                return self.data[addr:addr+size]
        image = Synthetic()
        sites = [(0x100, 'update_prefix'), (0x22A, 'call_prefix'), (0x500, 'wrapper_prefix'),
                 (0x519, 'wrapper_tail'), (0x800, 'transform_prefix'), (0x873, 'transform_fields')]
        for addr, name in sites: image.data[addr:addr+len(PROOFS[name])] = PROOFS[name]
        struct.pack_into('<i', image.data, 0x234, 0x500-0x238)
        struct.pack_into('<i', image.data, 0x52D, 0x800-0x531)
        self.assertTrue(verify_image(image, 0x100)['pass'])
        for addr, name in sites:
            for offset in range(len(PROOFS[name])):
                image.data[addr+offset] ^= 1
                with self.assertRaises(AssertionError): verify_image(image, 0x100)
                image.data[addr+offset] ^= 1
        struct.pack_into('<i', image.data, 0x234, 0x7fffffff)
        with self.assertRaises(AssertionError): verify_image(image, 0x100)

if __name__ == '__main__': unittest.main()
