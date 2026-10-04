import pathlib
import unittest
from validate_vehicle_retirement import SPANS,verify_source,verify_image

ROOT=pathlib.Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'src/modules/18_vehicle_owner_lifetime.inc').read_text()
PROFILES=(ROOT/'src/modules/05_runtime_profiles.inc').read_text()

class VehicleRetirementTests(unittest.TestCase):
    def test_independent_contract_and_profile_rejection(self):
        verify_source(SOURCE,PROFILES)
        with self.assertRaises(AssertionError):
            verify_source(SOURCE,PROFILES.replace('0x0098EFD0ULL','0x0098EFD1ULL'))

    def test_every_native_proof_byte_rejects_corruption(self):
        class Synthetic:
            def __init__(self): self.data=bytearray(0x37C)
            def read(self,addr,size): return self.data[addr:addr+size]
        image=Synthetic()
        for offset,expected in SPANS.values(): image.data[offset:offset+len(expected)]=expected
        verify_image(image,0)
        for offset,expected in SPANS.values():
            for i in range(len(expected)):
                image.data[offset+i]^=1
                with self.assertRaises(AssertionError): verify_image(image,0)
                image.data[offset+i]^=1

    def test_callback_path_is_numeric_only_nonwaiting(self):
        callbacks=SOURCE.split('static int vehicle_lifetime_lock')[1].split('static int install_vehicle_lifetime_observer')[0]
        for forbidden in ['readable_memory','append_log','malloc(', 'Sleep(', 'WaitFor', 'VirtualQuery']:
            self.assertNotIn(forbidden,callbacks)
        self.assertEqual(callbacks.count('original_retire(vehicle);'),1)
        self.assertIn('sample->vehicle_flags!=VEHICLE_BATCH_MATCH_FLAGS',callbacks)

    def test_copied_snapshot_releases_token_before_logging(self):
        flush=SOURCE.split('static void flush_vehicle_lifetime_snapshot')[1]
        self.assertIn('memcpy(s->records,d->records',flush)
        self.assertLess(flush.index('vehicle_lifetime_unlock();'),flush.index('"STAGE_VEHICLE_LIFETIME_SUMMARY serial=%ld snapshot=1'))
        self.assertNotIn('readable_memory',flush)

    def test_lifetime_installs_exactly_one_optional_hook(self):
        self.assertEqual(SOURCE.count('install_detour('),1)
        self.assertIn('g_vehicle_retire_prefix,18U',SOURCE)
        self.assertIn('if(!d->retire_target)',SOURCE)

if __name__=='__main__': unittest.main()
