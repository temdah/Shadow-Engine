import pathlib
import unittest
from validate_vehicle_batch import SPANS, verify_source, verify_image

ROOT=pathlib.Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'src/modules/18_vehicle_light_diagnostics.inc').read_text()
PROFILES=(ROOT/'src/modules/05_runtime_profiles.inc').read_text()

class VehicleBatchTests(unittest.TestCase):
    def test_source_mapping_and_instruction_contract(self):
        verify_source(SOURCE,PROFILES)
        with self.assertRaises(AssertionError):
            verify_source(SOURCE,PROFILES.replace('0x0093D730ULL','0x0093D731ULL'))

    def test_all_proof_bytes_reject_corruption(self):
        class Synthetic:
            def __init__(self): self.data=bytearray(0x1EE)
            def read(self,addr,size): return self.data[addr:addr+size]
        image=Synthetic()
        for offset, expected in SPANS.values(): image.data[offset:offset+len(expected)]=expected
        verify_image(image,0)
        for offset, expected in SPANS.values():
            for i in range(len(expected)):
                image.data[offset+i]^=1
                with self.assertRaises(AssertionError): verify_image(image,0)
                image.data[offset+i]^=1

    def test_batch_passthrough_has_bounded_position_copy_and_no_waits(self):
        hook=SOURCE.split('static void __fastcall hooked_vehicle_light_batch')[1].split('/* Native spatial copy')[0]
        self.assertEqual(hook.count('d->original_light_batch(vehicle,delta,skeleton,world_transform,distance_squared);'),1)
        self.assertIn('if(d->player_root_address && vehicle && readable_memory(vehicle,24))',hook)
        self.assertEqual(hook.count('copy_entity_reference_position('),1)
        for forbidden in ['append_log','Sleep(', 'WaitFor', 'malloc(', 'for(', 'while(']:
            self.assertNotIn(forbidden,hook)
        self.assertIn('TlsSetValue(d->batch_tls,previous)',hook)

    def test_copied_membership_does_not_scan_or_resolve(self):
        copy=SOURCE.split('static void copy_vehicle_batch_evidence')[1].split('static int vehicle_selection_lock')[0]
        self.assertIn('scope->serial!=d->serial',copy)
        self.assertIn('distance%VEHICLE_BATCH_ELEMENT_BYTES',copy)
        self.assertIn('distance/VEHICLE_BATCH_ELEMENT_BYTES>=r->vehicle_count',copy)
        self.assertNotIn('for(',copy)
        self.assertNotIn('original_',copy)

    def test_worker_only_logs_copies(self):
        worker=SOURCE.split('static DWORD WINAPI flush_vehicle_candidate_capture')[1].split('static void schedule_vehicle_candidate_capture_flush')[0]
        self.assertIn('STAGE_VEHICLE_BATCH_LINK',worker)
        self.assertIn('STAGE_VEHICLE_BATCH_SUMMARY',worker)
        self.assertNotIn('readable_memory',worker)
        self.assertNotIn('TlsGetValue',worker)

if __name__=='__main__': unittest.main()
