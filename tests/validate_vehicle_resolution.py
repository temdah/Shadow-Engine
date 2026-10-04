#!/usr/bin/env python3
"""Read-only regional proof of fields observed by the extended F10 diagnostic."""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import struct

from validate_runtime_corpus import Image, PROFILES, parse_asia_map, resolve, sha256

# Independently specified, complete field-consumer instructions. No wildcard
# regional byte guesses, GPU allocations or calls into the inspected DLLs.
QUEUE_PROOFS = (
    (0x219, bytes.fromhex('4869c0c0240000488d741810')),
    (0x225, bytes.fromhex('488b5c1810')),
    (0x231, bytes.fromhex('488b43104c8b7b08488b08')),
    (0x2D2, bytes.fromhex('488b43588b8e94200000448b9698200000488b4040')),
    (0x36F, bytes.fromhex('488986a0200000')),
)
RESOURCE_PROOFS = (
    (0xBC, bytes.fromhex('448b5504448b4500'), 0xE0, bytes.fromhex('4489464044895644')),
    (0xA6, bytes.fromhex('458b50048b4e10458b00'), 0xCC, bytes.fromhex('4489434044895344')),
)


def verify_source(shared: str) -> None:
    for name, value in {
        'VEHICLE_QUEUE_CANDIDATE_OFFSET': 0x10,
        'VEHICLE_QUEUE_FACE_COUNT_OFFSET': 0x20A0,
        'VEHICLE_QUEUE_WIDTH_OFFSET': 0x20A4,
        'VEHICLE_QUEUE_HEIGHT_OFFSET': 0x20A8,
        'VEHICLE_QUEUE_RESOURCE_OFFSET': 0x20B0,
        'VEHICLE_RESOURCE_WIDTH_OFFSET': 0x40,
        'VEHICLE_RESOURCE_HEIGHT_OFFSET': 0x44,
        'SHADOW_CANDIDATE_BACKING_OFFSET': 0x58,
        'SHADOW_BACKING_RESOURCE_OFFSET': 0x40,
    }.items():
        match = re.search(rf'^#define {name} 0x([0-9A-F]+)U$', shared, re.M)
        assert match and int(match[1], 16) == value, name
    parsed = []
    for offset, length, values in re.findall(r'\{0x([0-9A-F]+)U,(\d+)U,\{([^}]+)\}\}', shared):
        data = bytes(int(v.strip(), 0) for v in values.split(','))
        assert len(data) == int(length)
        parsed.append((int(offset, 16), data))
    assert tuple(parsed) == QUEUE_PROOFS
    parsed_resources = []
    for load, store, loads, stores, length in re.findall(
        r'\{0x([0-9A-F]+)U,0x([0-9A-F]+)U,\{([^}]+)\},\s*\{([^}]+)\},(\d+)U\}', shared
    ):
        load_bytes = bytes(int(v.strip(), 0) for v in loads.split(','))
        store_bytes = bytes(int(v.strip(), 0) for v in stores.split(','))
        assert len(load_bytes) == int(length) and len(store_bytes) == 8
        parsed_resources.append((int(load, 16), load_bytes, int(store, 16), store_bytes))
    assert tuple(parsed_resources) == RESOURCE_PROOFS


def verify_image(image: Image, renderer: int) -> dict:
    for offset, expected in QUEUE_PROOFS:
        assert image.read(renderer + offset, len(expected)) == expected, hex(renderer + offset)
    call = renderer + 0x339
    assert image.read(call, 1) == b'\xe8'
    lookup = call + 5 + struct.unpack('<i', image.read(call + 1, 4))[0]
    matches = []
    for index, (load, loads, store, stores) in enumerate(RESOURCE_PROOFS):
        if image.read(lookup + load, len(loads)) == loads and image.read(lookup + store, len(stores)) == stores:
            matches.append(index)
    assert len(matches) == 1, 'unknown/ambiguous metadata instruction variant'
    return {'rendererRva': hex(renderer), 'lookupRva': hex(lookup),
            'queueSpans': len(QUEUE_PROOFS), 'resourceVariant': matches[0], 'pass': True}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus-manifest', required=True, type=pathlib.Path)
    parser.add_argument('--global-runtime-image', required=True, type=pathlib.Path)
    parser.add_argument('--asia-runtime-image', required=True, type=pathlib.Path)
    args = parser.parse_args()
    project = pathlib.Path(__file__).resolve().parents[1]
    verify_source((project / 'src/modules/00_shared_config_state.inc').read_text())
    asia_map = parse_asia_map((project / 'src/modules/05_runtime_profiles.inc').read_text())
    manifest = json.loads(args.corpus_manifest.read_text())
    paths = {entry['sha256']: pathlib.Path(entry['absoluteResearchPath']) for entry in manifest['files']}
    results = []
    for profile in PROFILES.values():
        if profile.name == 'a4ee-signed-variant':
            results.append({'profile': profile.name, 'status': 'deferred-no-mapped-image'})
            continue
        path = args.global_runtime_image if profile.name == 'supported-04DF' else (
            args.asia_runtime_image if profile.explicit else paths[profile.sha256])
        mapped = profile.explicit or profile.name == 'supported-04DF'
        if not mapped:
            assert sha256(path) == profile.sha256
        result = verify_image(Image(path, mapped), resolve(profile, 0x306E60, asia_map))
        result.update(profile=profile.name, imageSha256=sha256(path))
        results.append(result)
    print(json.dumps({'sourceContract': 'pass', 'profiles': results,
                      'scope': 'CPU field-consumer bytes, not GPU dimensions or live visual proof'}, indent=2))


if __name__ == '__main__':
    main()
