"""Independent regional Lua frame sites and surrounding-code fingerprint gate."""
from pathlib import Path
import hashlib
import json
import re
from runtime_profile_fields import runtime_profile_field

REFERENCE = Path(__file__).parent / 'reference/lua_frame_profiles.json'

def parse_sites(text, table):
    match = re.search(r'static const LuaFramePatchSite '+re.escape(table)+r'\[\] = \{(.*?)\n\};', text, re.S)
    if not match:
        raise ValueError('Missing Lua frame table: '+table)
    result = {}
    for role, address, size, before, after in re.findall(
            r'\{\s*"([^"]+)",\s*0x([0-9a-f]+)U,\s*(\d+)U,\s*"([^"]+)",\s*"([^"]+)"\s*\}', match[1]):
        decode = lambda value: bytes(int(b,16) for b in re.findall(r'\\x([0-9a-f]{2})',value))
        if role in result:
            raise ValueError('Duplicate Lua frame role: '+role)
        result[role] = dict(rva=int(address,16),size=int(size),before=decode(before),after=decode(after))
    return result

def verify_lua_frame(image, profile_source, site_source, name):
    reference = json.loads(REFERENCE.read_text())['profiles'][name]
    if runtime_profile_field(profile_source,name,'lua_frame_sites') != reference['macro']:
        raise ValueError('Wrong regional Lua frame table: '+name)
    sites = parse_sites(site_source,reference['table'])
    if set(sites) != set(reference['sites']) or len(sites) != 17:
        raise ValueError('Incomplete regional Lua frame roles: '+name)
    spans=[]
    for role,s in sites.items():
        expected=reference['sites'][role]
        if s['rva']!=expected['rva'] or s['size']!=expected['size']:
            raise ValueError('Lua frame coordinate mismatch: '+role)
        for kind in ('before','after'):
            if len(s[kind])!=s['size'] or hashlib.sha256(s[kind]).hexdigest()!=expected[kind+'_sha256']:
                raise ValueError('Lua frame instruction mismatch: '+role)
        if image.read(s['rva'],s['size'])!=s['before']:
            raise ValueError('Lua frame runtime mismatch: '+role)
        spans.append((s['rva'],s['rva']+s['size']))
    spans.sort()
    if any(a[1]>b[0] for a,b in zip(spans,spans[1:])):
        raise ValueError('Overlapping Lua frame sites')
    for block in reference['blocks']:
        if hashlib.sha256(image.read(block['rva'],block['size'])).hexdigest()!=block['sha256']:
            raise ValueError('Lua frame surrounding-code mismatch: '+name)
    return {'sites':len(sites),'surrounding_blocks':len(reference['blocks']),
            'regional_mapping':'independent frozen function/block fingerprints',
            'runtime_acceptance':False}
