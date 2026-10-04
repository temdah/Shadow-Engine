from runtime_profile_fields import runtime_profile_field
"""Independent native spatial descriptor-handle copy proof; never invokes it."""
import argparse
import json
import pathlib
import re
from validate_runtime_corpus import Image, PROFILES, sha256

COPIES = {'supported-04DF': 0x25BB050, 'a4ee-signed-variant': 0,
          'vmpless-1.06': 0x819E00, 'complete-edition-1.06.329': 0x1C631F0,
          'asia-miru-1.06.329': 0x7F12E0}
PREFIX = bytes.fromhex('48895c24084889742410574883ec308b4208488bfa488bf1')
TAIL = bytes.fromhex('440fb65f48488b5c244044885e480fb647498846490fb6474a88464a8b474c89464c488b475048894650488bc6488b7424484883c4305fc3')

def verify_source(diagnostic, profiles):
    for name, expected in [('prefix', PREFIX), ('tail', TAIL)]:
        match = re.search(r'static const unsigned char g_vehicle_link_copy_' + name + r'\[\]\s*=([^;]+);', diagnostic)
        assert match, name
        assert bytes(int(h,16) for h in re.findall(r'\\x([0-9a-fA-F]{2})', match[1])) == expected, name
    for name, expected in COPIES.items():
        match = re.search('"'+re.escape(name)+r'"([^}]+)}', profiles)
        assert match, name
        # Read the declared field; optional profile capabilities may follow it.
        assert int(runtime_profile_field(profiles,name,'vehicle_spatial_copy_rva').replace('ULL',''),0)==expected, name

def verify_image(image, rva):
    assert image.read(rva,len(PREFIX))==PREFIX, 'spatial copy prefix'
    assert image.read(rva+0x9E,len(TAIL))==TAIL, 'spatial descriptor handle copy tail'
    return {'copyRva':hex(rva), 'instructionSpans':2, 'descriptorHandleOffset':'0x50', 'pass':True}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus-manifest',type=pathlib.Path,required=True)
    parser.add_argument('--global-runtime-image',type=pathlib.Path,required=True)
    parser.add_argument('--asia-runtime-image',type=pathlib.Path,required=True)
    args=parser.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    verify_source((root/'src/modules/18_vehicle_light_diagnostics.inc').read_text(),
                  (root/'src/modules/05_runtime_profiles.inc').read_text())
    paths={x['sha256']:pathlib.Path(x['absoluteResearchPath']) for x in json.loads(args.corpus_manifest.read_text())['files']}
    results=[]
    for p in PROFILES.values():
        if not COPIES[p.name]:
            results.append({'profile':p.name,'status':'deferred-handle-link-disabled'}); continue
        mapped=p.explicit or p.name=='supported-04DF'
        path=args.global_runtime_image if p.name=='supported-04DF' else (args.asia_runtime_image if p.explicit else paths[p.sha256])
        if not mapped: assert sha256(path)==p.sha256
        result=verify_image(Image(path,mapped),COPIES[p.name])
        result.update(profile=p.name,imageSha256=sha256(path)); results.append(result)
    print(json.dumps({'sourceContract':'pass','profiles':results,'scope':'Native copied identity, not persistent ownership or a callable lookup'},indent=2))

if __name__=='__main__': main()
