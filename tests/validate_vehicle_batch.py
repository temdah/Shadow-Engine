from runtime_profile_fields import runtime_profile_field
"""Independent optional CVehicle batch boundary proof; never loads an engine DLL."""
import argparse
import json
import pathlib
import re
from validate_runtime_corpus import Image, PROFILES, sha256

BATCHES = {'supported-04DF': 0x93D730, 'a4ee-signed-variant': 0,
           'vmpless-1.06': 0x517BC0, 'complete-edition-1.06.329': 0x1961A60,
           'asia-miru-1.06.329': 0x5EF450}
SPANS = {
    'prefix': (0, bytes.fromhex('4c894c24204c8944241853555657415641574883ec680f297424500f57f64532ff0f297c2440498bd9498bf8488be90f28f9')),
    'array': (0xD9, bytes.fromhex('488bb548010000c6852d01000000448bb5500100004d69f6300100004c03f6493bf60f84d6000000')),
    'restore': (0x16D, bytes.fromhex('488b9c24b8000000488bbc24b00000000f28ce450fb6c7488bce')),
    'dispatch': (0x18C, bytes.fromhex('80bddc01000000750d80bd2c01000000750433c9eb02b101488b06884c24200f28df4c8bc3488bd7488bceff50304881c630010000493bf60f8546ffffff4c8b6c24604c8ba424a80000000f287424500f287c24404883c468415f415e5f5e5d5bc3')),
}

def verify_source(diagnostic, profiles):
    for name, (offset, expected) in SPANS.items():
        match = re.search(r'static const unsigned char g_vehicle_batch_' + name + r'\[\]\s*=([^;]+);', diagnostic)
        assert match, name
        assert bytes(int(h,16) for h in re.findall(r'\\x([0-9a-fA-F]{2})',match[1])) == expected, name
        if offset:
            assert f'memcmp(batch+0x{offset:X}U,g_vehicle_batch_{name}' in diagnostic
    for name, expected in BATCHES.items():
        match = re.search('"'+re.escape(name)+r'"([^}]+)}', profiles)
        assert match, name
        # Read the declared field; optional profile capabilities may follow it.
        assert int(runtime_profile_field(profiles,name,'vehicle_light_batch_rva').replace('ULL',''),0)==expected, name
    assert '#define VEHICLE_BATCH_RETURN_OFFSET 0x1BAU' in diagnostic

def verify_image(image, rva):
    for name, (offset, expected) in SPANS.items():
        assert image.read(rva+offset,len(expected))==expected, name
    return {'batchRva':hex(rva), 'returnRva':hex(rva+0x1BA), 'instructionSpans':4,
            'overwriteBytes':18, 'arrayOffset':'0x148', 'countOffset':'0x150',
            'elementBytes':'0x130', 'pass':True}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus-manifest',type=pathlib.Path,required=True)
    parser.add_argument('--global-runtime-image',type=pathlib.Path,required=True)
    parser.add_argument('--asia-runtime-image',type=pathlib.Path,required=True)
    args=parser.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    verify_source((root/'src/modules/18_vehicle_light_diagnostics.inc').read_text(),
                  (root/'src/modules/05_runtime_profiles.inc').read_text())
    paths={e['sha256']:pathlib.Path(e['absoluteResearchPath']) for e in json.loads(args.corpus_manifest.read_text())['files']}
    results=[]
    for p in PROFILES.values():
        rva=BATCHES[p.name]
        if not rva:
            results.append({'profile':p.name,'status':'unverified-observer-disabled'}); continue
        mapped=p.explicit or p.name=='supported-04DF'
        path=args.global_runtime_image if p.name=='supported-04DF' else (args.asia_runtime_image if p.explicit else paths[p.sha256])
        if not mapped: assert sha256(path)==p.sha256
        result=verify_image(Image(path,mapped),rva)
        result.update(profile=p.name,imageSha256=sha256(path)); results.append(result)
    print(json.dumps({'sourceContract':'pass','profiles':results},indent=2))

if __name__=='__main__': main()
