"""Fail-closed regression tests for the local compatibility release gate."""
from pathlib import Path
import copy
import json
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/compatibility'))
import matrix
import record_baseline

class CompatibilityBuilderTests(unittest.TestCase):
    def setUp(self):
        self.profiles={p.name:{'status':'pass','features':{'driver':p.name=='supported-04DF'}}
                       for p in matrix.corpus.PROFILES.values()}
        self.steps=[{'name':n,'exit_code':0} for n in sorted(matrix.required_steps())]
        self.contract={'native':'a'}
        self.baseline={'schema':1,'contracts':self.contract,'compiler_sha256':'c',
            'profile_features':{n:r['features'] for n,r in self.profiles.items()},
            'manual_observations':[{'profile':'supported-04DF','build_profile':'Internal',
                'observation':'Observed expected controls and no new visual regressions.',
                'asi_sha256':'a'*64,'report_sha256':'b'*64}]}

    def evaluate(self,baseline=True):
        return matrix.evaluate(self.profiles,self.steps,self.contract,'c',self.baseline if baseline else None)

    def test_complete_calibrated_gate(self):
        self.assertTrue(self.evaluate()['automatic_release_gate'])

    def test_unrecorded_manual_test_is_not_assumed(self):
        self.assertEqual(self.evaluate(False)['status'],'incomplete')

    def test_missing_image_is_not_pass(self):
        self.profiles['a4ee-signed-variant']['status']='missing-evidence'
        self.assertEqual(self.evaluate()['status'],'incomplete')

    def test_omitted_profile_is_failure(self):
        del self.profiles['a4ee-signed-variant']
        self.assertEqual(self.evaluate()['status'],'failed')

    def test_omitted_or_duplicate_gate_is_failure(self):
        self.steps.pop()
        self.assertEqual(self.evaluate()['status'],'failed')
        self.steps.append(copy.deepcopy(self.steps[0]))
        self.assertEqual(self.evaluate()['status'],'failed')

    def test_regression_failure_blocks(self):
        self.steps[0]['exit_code']=1
        self.assertEqual(self.evaluate()['status'],'failed')

    def test_changed_boundary_or_compiler_requires_review(self):
        for field in ('contracts','compiler_sha256','profile_features'):
            with self.subTest(field=field):
                saved=self.baseline[field];self.baseline[field]={}
                self.assertFalse(self.evaluate()['automatic_release_gate'])
                self.baseline[field]=saved

    def test_empty_or_unknown_manual_observation_rejected(self):
        self.baseline['manual_observations'][0]['profile']='invented'
        self.assertFalse(self.evaluate()['automatic_release_gate'])

    def test_unexpected_matrix_state_rejected(self):
        self.profiles['supported-04DF']['status']='skipped'
        self.assertEqual(self.evaluate()['status'],'failed')

    def test_pe_identity_rejects_non_x64(self):
        data=bytearray(512);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,128)
        data[128:132]=b'PE\0\0';struct.pack_into('<H',data,132,0x14c)
        with self.assertRaisesRegex(ValueError,'not x64'):matrix.pe_identity(data)

    def test_hook_body_change_invalidates_native_boundary(self):
        source='static int hooked_test(int x) { note("}"); if(x) { native(x); } return x; }'
        extracted=matrix.hook_definitions(source)
        self.assertEqual(extracted,[source])
        self.assertNotEqual(extracted,matrix.hook_definitions(source.replace('native(x)','native(x+1)')))

    def test_image_tampering_rejected_before_pe_parse(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'image';path.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'hash changed'):
                matrix.checked_image({'path':str(path),'sha256':'0'*64},None,True)

    def test_calibration_rejects_stale_source_and_modified_artifact(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);artifact=root/'test.asi';artifact.write_bytes(b'candidate')
            report={'schema':1,'status':'incomplete','contracts':self.contract,'source_sha256':{'x':'1'},
                    'compiler_sha256':'c','steps':self.steps,'profiles':self.profiles,
                    'artifacts':{'Internal':{'path':str(artifact),'sha256':matrix.digest(artifact)}}}
            path=root/'report.json';path.write_text(json.dumps(report));output=root/'baseline.json'
            with patch.object(record_baseline,'contracts',return_value=self.contract), \
                 patch.object(record_baseline,'source_hashes',return_value={'x':'2'}):
                with self.assertRaisesRegex(ValueError,'Source changed'):
                    record_baseline.record(path,output,'supported-04DF','Internal','tested')
            with patch.object(record_baseline,'contracts',return_value=self.contract), \
                 patch.object(record_baseline,'source_hashes',return_value={'x':'1'}):
                result=record_baseline.record(path,output,'supported-04DF','Internal','tested')
                self.assertEqual(len(result['manual_observations']),1)
                with self.assertRaises(FileExistsError):
                    record_baseline.record(path,output,'supported-04DF','Internal','tested')
                artifact.write_bytes(b'different')
                with self.assertRaisesRegex(ValueError,'artifact changed'):
                    record_baseline.record(path,root/'other.json','supported-04DF','Internal','tested')

if __name__=='__main__':unittest.main()
