from runtime_profile_fields import runtime_profile_field
#!/usr/bin/env python3
"""Independent read-only proof of the optional vehicle-update observation chain."""
import argparse
import json
import pathlib
import re
import struct
from validate_runtime_corpus import Image, PROFILES, sha256

UPDATES = {'supported-04DF': 0xBA7570, 'a4ee-signed-variant': 0,
           'vmpless-1.06': 0x78E1A0, 'complete-edition-1.06.329': 0x1BB5420,
           'asia-miru-1.06.329': 0x77E560}
TRANSFORMS = {'supported-04DF': 0x79E0B0, 'vmpless-1.06': 0x2B47A0,
              'complete-edition-1.06.329': 0x1719810, 'asia-miru-1.06.329': 0x1F6FB0}
PROOFS = {
    'update_prefix': bytes.fromhex('48895c24084889742410574881ec200100004883791000498bf0488bda488bf90f841201000048837908000f840701000080793100740680793200740a807930000f84f1000000488b010f28cbff5040'),
    'call_prefix': bytes.fromhex('488b4f10488d542460e8'),
    'wrapper_prefix': bytes.fromhex('48895c2408574883ec20488bf94883c120488bdae8'),
    'wrapper_tail': bytes.fromhex('4c8bc3488bcf488bd0488b5c24304883c4205fe9'),
    'transform_prefix': bytes.fromhex('488bc4488958084889681048897018574881ecd00000000f2970e80f2978d8440f2940c8488bf1498bd8488bfa'),
    'transform_fields': bytes.fromhex('f30f11464cf30f114e50f30f115654'),
}

def verify_source(diagnostic, profiles):
    for name, expected in PROOFS.items():
        match = re.search(r'static const unsigned char g_vehicle_owner_' + name + r'\[\]\s*=([^;]+);', diagnostic)
        assert match, name
        actual = bytes(int(h, 16) for h in re.findall(r'\\x([0-9a-fA-F]{2})', match[1]))
        assert actual == expected, name
    for name, expected in UPDATES.items():
        match = re.search('"' + re.escape(name) + r'"([^}]+)}', profiles)
        assert match, name
        value = runtime_profile_field(profiles,name,'vehicle_light_update_rva').replace('ULL','')
        assert int(value, 0) == expected, name

def verify_image(image, update):
    def check(address, name):
        assert image.read(address, len(PROOFS[name])) == PROOFS[name], name
    check(update, 'update_prefix')
    check(update + 0x12A, 'call_prefix')
    wrapper = update + 0x138 + struct.unpack('<i', image.read(update + 0x134, 4))[0]
    check(wrapper, 'wrapper_prefix')
    check(wrapper + 0x19, 'wrapper_tail')
    transform = wrapper + 0x31 + struct.unpack('<i', image.read(wrapper + 0x2D, 4))[0]
    check(transform, 'transform_prefix')
    check(transform + 0x73, 'transform_fields')
    return {'updateRva': hex(update), 'wrapperRva': hex(wrapper), 'transformRva': hex(transform),
            'instructionSpans': 6, 'updateOverwrite': 18, 'transformOverwrite': 15, 'pass': True}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus-manifest', type=pathlib.Path, required=True)
    parser.add_argument('--global-runtime-image', type=pathlib.Path, required=True)
    parser.add_argument('--asia-runtime-image', type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    verify_source((root/'src/modules/18_vehicle_light_diagnostics.inc').read_text(),
                  (root/'src/modules/05_runtime_profiles.inc').read_text())
    manifest = json.loads(args.corpus_manifest.read_text())
    paths = {e['sha256']: pathlib.Path(e['absoluteResearchPath']) for e in manifest['files']}
    results = []
    for profile in PROFILES.values():
        update = UPDATES[profile.name]
        if not update:
            results.append({'profile': profile.name, 'status': 'deferred-observer-disabled'})
            continue
        mapped = profile.explicit or profile.name == 'supported-04DF'
        path = args.global_runtime_image if profile.name == 'supported-04DF' else (
            args.asia_runtime_image if profile.explicit else paths[profile.sha256])
        if not mapped: assert sha256(path) == profile.sha256
        result = verify_image(Image(path, mapped), update)
        assert int(result['transformRva'], 16) == TRANSFORMS[profile.name]
        result.update(profile=profile.name, imageSha256=sha256(path))
        results.append(result)
    print(json.dumps({'sourceContract': 'pass', 'profiles': results}, indent=2))

if __name__ == '__main__': main()
