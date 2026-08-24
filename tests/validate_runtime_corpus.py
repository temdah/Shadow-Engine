#!/usr/bin/env python3
"""Validate capacity-sensitive Shadow Engine sites across regional runtimes.

The proprietary runtime images remain outside Git.  This test accepts the
private corpus manifest and unpacked images explicitly; it never loads a DLL or
writes a fixture.  Packed A4EE can use a frozen successful-preflight log until
one mapped image is captured, at which point --a4ee-runtime-image makes all five
profiles byte-exact.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
from dataclasses import dataclass


@dataclass(frozen=True)
class Profile:
    name: str
    sha256: str
    image_size: int
    timestamp: int
    checksum: int
    entry_rva: int
    deltas: tuple[int, int, int] = (0, 0, 0)
    explicit: bool = False


PROFILES = {
    "supported-04DF": Profile(
        "supported-04DF", "04DF823E1613076549C8B6F66FB507E6CDAF00425757C140D4D43364ADAC3D92",
        0x06595000, 0x5CD045B4, 0x01E5793C, 0x04760FA6),
    "a4ee-signed-variant": Profile(
        "a4ee-signed-variant", "A4EEAD7A645AB4340A67622DA4BFC91789E8CECFD53F605499A0E4B09B3EE60A",
        0x06595000, 0x5CD13864, 0x01E4FD51, 0x05824EE5,
        (0x00597690, 0x005FDC60, 0x00524650)),
    "vmpless-1.06": Profile(
        "vmpless-1.06", "BA6B7E004E1F9B69C1659C08DE510404BE21EAB2A14D788D5A87305B89A68742",
        0x04ED6000, 0x5BE5AA1A, 0x048AF60C, 0x02F58AC8,
        (0x005B83F0, 0x0061E710, 0x00545D80)),
    "complete-edition-1.06.329": Profile(
        "complete-edition-1.06.329", "9DBC60118086D14A5AD32EB606459BAC32BAED022A9DB84B4D1E4B6606A7F7C6",
        0x041AB000, 0x55DC1ECD, 0x03B84DA7, 0x021FD388,
        (0x019FFDE0, 0x01A66680, 0x0198C410)),
    "asia-miru-1.06.329": Profile(
        "asia-miru-1.06.329", "E1505F9F7F130BACD449EB74D96C30619CDF0FC10F1CAD97E01E4397E688A541",
        0x07EE1000, 0x543589C5, 0x02B88696, 0x07C88AFE,
        explicit=True),
}

INLINE_SIGNATURES = {
    0x002E2236: "BE10000000",
    0x00306ECD: "41B844000000",
    0x00306EEF: "448D4244",
    0x002CC300: "498B8D70710200488B9424D8000000",
    0x002CC31C: "498B8570710200488B9424D8000000",
    0x00307168: "8B94871004000044895554C7455801000000",
    0x003071AB: "428B8CB710040000488B5358894A48",
    0x00308389: "488D948F10040000488B4F084C89AD00040000",
    0x003073E2: "418D040E41C7874C050000FFFF7FFF",
    0x00308441: "418D040E49899C34F0070000B904000000",
    0x002E8E89: "C785B400000004000000",
    0x002E9C21: "BA04000000",
    0x002E9C3B: "41B808000000",
    0x003090B9: "83FF100F82EEFEFFFF",
    0x0030ACC9: "488B4CCA084885C97429488B09488D94",
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


class Image:
    def __init__(self, path: pathlib.Path, mapped: bool):
        self.path = path
        self.data = path.read_bytes()
        self.mapped = mapped
        self.sections: list[tuple[int, int, int, int]] = []
        if not mapped:
            pe = struct.unpack_from("<I", self.data, 0x3C)[0]
            if self.data[pe:pe + 4] != b"PE\0\0":
                raise AssertionError(f"invalid PE: {path}")
            sections = struct.unpack_from("<H", self.data, pe + 6)[0]
            opt_size = struct.unpack_from("<H", self.data, pe + 20)[0]
            optional = pe + 24
            self.timestamp = struct.unpack_from("<I", self.data, pe + 8)[0]
            self.entry_rva = struct.unpack_from("<I", self.data, optional + 16)[0]
            self.image_size = struct.unpack_from("<I", self.data, optional + 56)[0]
            self.checksum = struct.unpack_from("<I", self.data, optional + 64)[0]
            section_table = optional + opt_size
            for index in range(sections):
                off = section_table + index * 40
                virtual_size, virtual_rva, raw_size, raw_off = struct.unpack_from(
                    "<IIII", self.data, off + 8)
                self.sections.append((virtual_rva, virtual_size, raw_off, raw_size))

    def offset(self, rva: int, size: int) -> int:
        if self.mapped:
            if rva < 0 or rva + size > len(self.data):
                raise AssertionError(f"mapped RVA outside image: 0x{rva:X}")
            return rva
        for virtual_rva, virtual_size, raw_off, raw_size in self.sections:
            delta = rva - virtual_rva
            if 0 <= delta and delta + size <= raw_size:
                return raw_off + delta
        raise AssertionError(f"RVA has no raw bytes in {self.path.name}: 0x{rva:X}")

    def read(self, rva: int, size: int) -> bytes:
        off = self.offset(rva, size)
        return self.data[off:off + size]


def array_block(source: str, name: str) -> str:
    match = re.search(
        rf"static const (?:RuntimeRvaMap|TailPatch) {re.escape(name)}\[\]\s*=\s*\{{(.*?)\n\}};",
        source, re.S)
    if not match:
        raise AssertionError(f"array missing: {name}")
    return match.group(1)


def parse_asia_map(profile_source: str) -> dict[int, int]:
    pairs = [(int(a, 16), int(b, 16)) for a, b in re.findall(
        r"\{\s*0x([0-9A-Fa-f]+)ULL,\s*0x([0-9A-Fa-f]+)ULL\s*\}",
        array_block(profile_source, "g_asia_rva_map"))]
    assert len(pairs) == 77 and pairs == sorted(pairs) and len(dict(pairs)) == 77
    return dict(pairs)


def integer_define(source: str, name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+([0-9]+)U\s*$", source, re.M)
    if not match:
        raise AssertionError(f"integer define missing: {name}")
    return int(match.group(1))


def parse_tail_patches(state: str, extra_maps: int) -> list[tuple[int, int, int]]:
    patches = [(int(a, 16), int(b, 16), int(c, 16)) for a, b, c in re.findall(
        r"\{\s*0x([0-9A-Fa-f]+)ULL,\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\s*\}",
        array_block(state, "g_tail_patches"))]
    assert len(patches) == 46
    for _, old, new in patches:
        stride = 0x24C0 if old == 0x270D0 else 0x24C4 if old == 0x27114 else 0x24C8
        assert new == old + extra_maps * stride, (
            hex(old), hex(new), hex(stride), extra_maps)
    return patches


def resolve(profile: Profile, supported: int, asia_map: dict[int, int]) -> int:
    if profile.explicit:
        assert supported in asia_map, f"Asia mapping missing 0x{supported:X}"
        return asia_map[supported]
    a, b, c = profile.deltas
    delta = c if supported >= 0x00420000 else b if supported >= 0x003C0000 else a if supported >= 0x002B0000 else 0
    return supported + delta


def manifest_records(path: pathlib.Path) -> dict[str, dict]:
    records = json.loads(path.read_text(encoding="utf-8"))["files"]
    by_hash = {str(record["sha256"]).upper(): record for record in records}
    assert len(by_hash) == 5, "corpus must contain five distinct Disrupt hashes"
    return by_hash


def validate_exact(profile: Profile, image: Image, asia_map: dict[int, int],
                   tails: list[tuple[int, int, int]]) -> dict:
    checks = 0
    for supported, signature_hex in INLINE_SIGNATURES.items():
        signature = bytes.fromhex(signature_hex)
        actual = resolve(profile, supported, asia_map)
        assert image.read(actual, len(signature)) == signature, (
            f"{profile.name} signature mismatch supported=0x{supported:X} actual=0x{actual:X}")
        checks += 1

    simulated = bytearray(image.data)
    for index, (supported, old, new) in enumerate(tails):
        actual = resolve(profile, supported, asia_map)
        window = image.read(actual, 16)
        needle = struct.pack("<I", old)
        offsets = [offset for offset in range(13) if window[offset:offset + 4] == needle]
        assert len(offsets) == 1, f"{profile.name} tail[{index}] old immediate count={len(offsets)}"
        file_offset = image.offset(actual + offsets[0], 4)
        simulated[file_offset:file_offset + 4] = struct.pack("<I", new)
        assert simulated[file_offset:file_offset + 4] == struct.pack("<I", new)
        checks += 1

    map_loop = resolve(profile, 0x00309050, asia_map)
    loop = image.read(map_loop, 27)
    assert loop[:11] == bytes.fromhex("448B9D3008000045891C24")
    assert loop[11] == 0xE8 and loop[16:19] == bytes.fromhex("0F28F0")
    assert loop[19] == 0xE8 and loop[24:27] == bytes.fromhex("0F28F8")
    checks += 1

    return {"mode": "byte-exact", "checks": checks, "tailWritesSimulated": len(tails)}


def validate_a4ee_attestation(path: pathlib.Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    required = [
        "RUNTIME_PROFILE_SELECTED profile=a4ee-signed-variant",
        "clusterA=0x00597690 clusterB=0x005FDC60 clusterC=0x00524650",
        "STAGE_M_PREFLIGHT_COMPLETE",
        "tailRelocations=46 noEngineMutationBeforeThisMarker=1",
        "STAGE_M_COMPLETE resourceConstructorReached=1 passConstructorReached=1",
    ]
    missing = [marker for marker in required if marker not in text]
    assert not missing, f"A4EE attestation missing markers: {missing}"
    return {"mode": "frozen-runtime-attestation", "checks": len(required), "sha256": sha256(path)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus-manifest", type=pathlib.Path, required=True)
    parser.add_argument("--global-runtime-image", type=pathlib.Path, required=True)
    parser.add_argument("--asia-runtime-image", type=pathlib.Path, required=True)
    parser.add_argument("--a4ee-attestation-log", type=pathlib.Path)
    parser.add_argument("--a4ee-runtime-image", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[1]
    state = (root / "src/modules/00_shared_config_state.inc").read_text(encoding="utf-8")
    profile_source = (root / "src/modules/05_runtime_profiles.inc").read_text(encoding="utf-8")
    asia_map = parse_asia_map(profile_source)
    original_maps = integer_define(state, "ORIGINAL_LOCAL_MAPS")
    extra_maps = integer_define(state, "EXTRA_LOCAL_MAPS")
    total_maps = integer_define(state, "TOTAL_LOCAL_MAPS")
    assert total_maps == original_maps + extra_maps
    tails = parse_tail_patches(state, extra_maps)
    records = manifest_records(args.corpus_manifest)

    results: dict[str, dict] = {}
    for profile in PROFILES.values():
        literal_markers = [
            f'"{profile.name}"', f"0x{profile.image_size:08X}U",
            f"0x{profile.timestamp:08X}U", f"0x{profile.checksum:08X}U",
            f"0x{profile.entry_rva:08X}U",
        ]
        assert all(marker in profile_source for marker in literal_markers), f"profile source drift: {profile.name}"
        record = records[profile.sha256]
        raw_path = pathlib.Path(record["absoluteResearchPath"])
        assert sha256(raw_path) == profile.sha256
        raw = Image(raw_path, mapped=False)
        assert (raw.image_size, raw.timestamp, raw.checksum, raw.entry_rva) == (
            profile.image_size, profile.timestamp, profile.checksum, profile.entry_rva)

        if profile.name == "supported-04DF":
            image = Image(args.global_runtime_image, mapped=True)
        elif profile.name == "asia-miru-1.06.329":
            image = Image(args.asia_runtime_image, mapped=True)
        elif profile.name == "a4ee-signed-variant":
            if args.a4ee_runtime_image:
                image = Image(args.a4ee_runtime_image, mapped=True)
            else:
                assert args.a4ee_attestation_log, "A4EE needs a mapped image or frozen attestation log"
                results[profile.name] = validate_a4ee_attestation(args.a4ee_attestation_log)
                continue
        else:
            image = raw
        if image.mapped:
            assert len(image.data) >= profile.image_size
        results[profile.name] = validate_exact(profile, image, asia_map, tails)

    exact = sum(result["mode"] == "byte-exact" for result in results.values())
    attested = len(results) - exact
    output = {
        "schemaVersion": 1,
        "status": "pass",
        "profiles": results,
        "byteExactProfiles": exact,
        "attestedProfiles": attested,
        "tailRelocations": len(tails),
        "asiaMappings": len(asia_map),
        "originalMaps": original_maps,
        "extraMaps": extra_maps,
        "totalMaps": total_maps,
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(
        f"PASS profiles=5 byteExact={exact} attested={attested} "
        f"maps={original_maps}+{extra_maps}={total_maps} "
        f"tails={len(tails)} asiaMappings={len(asia_map)}")
    for name, result in results.items():
        print(f"  {name}: {result['mode']} checks={result['checks']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
