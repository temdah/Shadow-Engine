import pathlib
import unittest
from validate_vehicle_handle_link import PREFIX, TAIL, verify_source, verify_image

ROOT=pathlib.Path(__file__).resolve().parents[1]
D=(ROOT/'src/modules/18_vehicle_light_diagnostics.inc').read_text()
P=(ROOT/'src/modules/05_runtime_profiles.inc').read_text()

class HandleLinkTests(unittest.TestCase):
    def test_independent_source_contract(self):
        verify_source(D,P)
        with self.assertRaises(AssertionError): verify_source(D,P.replace('0x025BB050ULL','0x025BB051ULL'))

    def test_every_proof_byte_rejects_corruption(self):
        class Fixture:
            data=bytearray(0x200)
            def read(self,off,size): return self.data[off:off+size]
        fixture=Fixture()
        fixture.data[:len(PREFIX)]=PREFIX
        fixture.data[0x9E:0x9E+len(TAIL)]=TAIL
        self.assertTrue(verify_image(fixture,0)['pass'])
        for start,span in [(0,PREFIX),(0x9E,TAIL)]:
            for i in range(len(span)):
                fixture.data[start+i]^=1
                with self.assertRaises(AssertionError): verify_image(fixture,0)
                fixture.data[start+i]^=1

    def test_optional_read_and_unchanged_hook_set(self):
        self.assertIn('diagnostic->handle_link_enabled=validate_vehicle_handle_link_sites();',D)
        self.assertIn('if(!g_shadow_engine.vehicle_diagnostics.handle_link_enabled || !spatial',D)
        self.assertEqual(D.count('install_detour('),3)  # Includes the independent batch observer.
        self.assertIn('record->descriptor_handle!=UINT64_MAX',D)
        self.assertIn('descriptorHandleValid=%u',D)

    def test_retry_is_throttled_and_rechecks_identity(self):
        self.assertIn('now-r->last_probe_tick<VEHICLE_OWNER_RETRY_MS',D)
        self.assertIn('spatial_handle!=r->spatial_handle || descriptor_handle!=r->descriptor_handle',D)
        self.assertIn('r->transform_seen || (r->valid&3U)!=3U',D)
        shared=(ROOT/'src/modules/00_shared_config_state.inc').read_text()
        self.assertIn('#define VEHICLE_OWNER_RETRY_MS 16U',shared)
        self.assertIn('#define VEHICLE_SPATIAL_DESCRIPTOR_HANDLE_OFFSET 0x50U',shared)

if __name__=='__main__': unittest.main()
