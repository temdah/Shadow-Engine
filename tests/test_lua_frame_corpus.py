"""Reject incomplete, misrouted and modified Lua repair manifests."""
from pathlib import Path
import json
import unittest
from lua_frame_corpus import REFERENCE, parse_sites, verify_lua_frame

ROOT=Path(__file__).resolve().parents[1]

class NoImageReads:
    def read(self,*args):
        raise AssertionError('A malformed first site must be rejected before image access')

class LuaFrameCorpusTests(unittest.TestCase):
    def setUp(self):
        self.source=(ROOT/'src/internal/lua_frame_sites.inc').read_text()
        self.profiles=(ROOT/'src/modules/05_runtime_profiles.inc').read_text()
        self.reference=json.loads(REFERENCE.read_text())['profiles']

    def test_each_region_has_complete_unique_recipe(self):
        for name,ref in self.reference.items():
            sites=parse_sites(self.source,ref['table'])
            self.assertEqual(set(sites),set(ref['sites']),name)
            self.assertEqual(len(sites),17)

    def test_profile_must_not_borrow_global_table(self):
        source=self.profiles.replace('LUA_FRAME_A4EE_SITES','LUA_FRAME_GLOBAL_SITES')
        with self.assertRaisesRegex(ValueError,'Wrong regional'):
            verify_lua_frame(NoImageReads(),source,self.source,'a4ee-signed-variant')

    def test_missing_or_duplicate_role_rejected(self):
        for replacement in ('removed_prototype_initialize','compiler_initial_two'):
            source=self.source.replace('"prototype_initialize"','"'+replacement+'"',1)
            with self.assertRaises(ValueError):
                verify_lua_frame(NoImageReads(),self.profiles,source,'supported-04DF')

    def test_address_and_replacement_changes_rejected(self):
        for old,new in [('0x1c11ffU','0x1c1200U'),('\\x44\\x89\\x5f\\x0a\\x90','\\x44\\x89\\x5f\\x0c\\x90')]:
            source=self.source.replace(old,new,1)
            self.assertNotEqual(source,self.source)
            with self.assertRaises(ValueError):
                verify_lua_frame(NoImageReads(),self.profiles,source,'supported-04DF')

if __name__=='__main__':unittest.main()
