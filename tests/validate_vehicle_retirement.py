from runtime_profile_fields import runtime_profile_field
"""Independent read-only proof of the optional common vehicle teardown boundary."""
import argparse
import json
import pathlib
import re
import struct
from validate_runtime_corpus import Image,PROFILES,sha256

RETIREMENTS={'supported-04DF':0x98EFD0,'a4ee-signed-variant':0,
             'vmpless-1.06':0x569460,'complete-edition-1.06.329':0x19B3300,
             'asia-miru-1.06.329':0x610DA0}
SPANS={
 'prefix':(0,bytes.fromhex('48895c241048897424185557415441554156488d6c24c04881ec40010000488bb918010000')),
 'array':(0x2BF,bytes.fromhex('488d8348010000488d4d10488945184883c0084c89752048894528')),
 'stride':(0x2E1,bytes.fromhex('48c745303001000048894510e8')),
 'tail':(0x355,bytes.fromhex('488bcb488943304c8d9c2440010000498b5b38498b7340498be3415e415d415c5f5de9')),
}

def verify_source(source,profiles):
    for name,(offset,expected) in SPANS.items():
        match=re.search(r'static const unsigned char g_vehicle_retire_'+name+r'\[\]\s*=([^;]+);',source)
        assert match,name
        assert bytes(int(h,16) for h in re.findall(r'\\x([0-9a-fA-F]{2})',match[1]))==expected,name
        if offset: assert f'memcmp(target+0x{offset:X}U,g_vehicle_retire_{name}' in source
    for name,rva in RETIREMENTS.items():
        match=re.search('"'+re.escape(name)+r'"([^}]+)}',profiles)
        assert match,name
        # Read the declared field; optional profile capabilities may follow it.
        assert int(runtime_profile_field(profiles,name,'vehicle_retire_rva').replace('ULL',''),0)==rva,name

def verify_image(image,rva):
    for name,(offset,expected) in SPANS.items():
        assert image.read(rva+offset,len(expected))==expected,name
    cleanup=rva+0x2F2+struct.unpack('<i',image.read(rva+0x2EE,4))[0]
    base=rva+0x37C+struct.unpack('<i',image.read(rva+0x378,4))[0]
    return {'retireRva':hex(rva),'arrayCleanupRva':hex(cleanup),'baseTeardownRva':hex(base),
            'instructionSpans':4,'overwriteBytes':18,'pass':True}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus-manifest',type=pathlib.Path,required=True)
    parser.add_argument('--global-runtime-image',type=pathlib.Path,required=True)
    parser.add_argument('--asia-runtime-image',type=pathlib.Path,required=True)
    args=parser.parse_args(); root=pathlib.Path(__file__).resolve().parents[1]
    verify_source((root/'src/modules/18_vehicle_owner_lifetime.inc').read_text(),
                  (root/'src/modules/05_runtime_profiles.inc').read_text())
    paths={e['sha256']:pathlib.Path(e['absoluteResearchPath']) for e in json.loads(args.corpus_manifest.read_text())['files']}
    results=[]
    for p in PROFILES.values():
        rva=RETIREMENTS[p.name]
        if not rva:
            results.append({'profile':p.name,'status':'unverified-observer-disabled'}); continue
        mapped=p.explicit or p.name=='supported-04DF'
        path=args.global_runtime_image if p.name=='supported-04DF' else (args.asia_runtime_image if p.explicit else paths[p.sha256])
        if not mapped: assert sha256(path)==p.sha256
        result=verify_image(Image(path,mapped),rva)
        result.update(profile=p.name,imageSha256=sha256(path)); results.append(result)
    print(json.dumps({'sourceContract':'pass','profiles':results},indent=2))

if __name__=='__main__': main()
