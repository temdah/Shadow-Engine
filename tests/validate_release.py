#!/usr/bin/env python3
"""Read-only release checks against the compiled ASI, not export declarations."""
from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys

from validate_refactor import definitions, read_tree, validate_policy_constants, V30_B4_21_POLICY_TARGET
from validate_runtime_corpus import Image

EXPECTED_EXPORTS = {
    "Init": 1,
    "OnBeforeEngineInit": 2,
    "ShadowEngine_GetControlApiVersion": 3,
    "ShadowEngine_GetVehicleHeadlightLimiterEnabled": 4,
    "ShadowEngine_SetVehicleHeadlightLimiterEnabled": 5,
}

INTERNAL_DIAGNOSTIC_MARKERS = (
    b"STAGE_Z_COMPLETION",
    b"STAGE_INTERSECTION_READY",
    b"STAGE_J_MANUAL_CAPTURE_BEGIN",
)


def read_exports(image: Image) -> dict[str, int]:
    data = image.data
    if data[:2] != b"MZ":
        raise ValueError("missing DOS signature")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    optional = pe + 24
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    if struct.unpack_from("<H", data, pe + 4)[0] != 0x8664:
        raise ValueError("ASI is not x64")
    if not struct.unpack_from("<H", data, pe + 22)[0] & 0x2000:
        raise ValueError("ASI is not a DLL")
    if struct.unpack_from("<H", data, pe + 20)[0] < 120:
        raise ValueError("truncated optional header")
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError("ASI is not PE32+")
    if struct.unpack_from("<I", data, optional + 108)[0] < 1:
        raise ValueError("missing export data directory")
    export_rva, export_size = struct.unpack_from("<II", data, optional + 112)
    if not export_rva or export_size < 40:
        raise ValueError("missing export table")
    directory = image.read(export_rva, 40)
    base, functions, names, function_table, name_table, ordinal_table = struct.unpack_from(
        "<IIIIII", directory, 16)
    if functions != len(EXPECTED_EXPORTS) or names != len(EXPECTED_EXPORTS) or base != 1:
        raise ValueError(f"expected exactly {len(EXPECTED_EXPORTS)} exports at ordinal base 1")
    result = {}
    used_ordinals = set()
    for index in range(names):
        name_rva = struct.unpack("<I", image.read(name_table + 4 * index, 4))[0]
        ordinal_index = struct.unpack("<H", image.read(ordinal_table + 2 * index, 2))[0]
        if ordinal_index >= functions or ordinal_index in used_ordinals:
            raise ValueError("invalid or duplicate export ordinal")
        used_ordinals.add(ordinal_index)
        name_bytes = bytearray()
        for offset in range(128):
            byte = image.read(name_rva + offset, 1)[0]
            if not byte:
                break
            name_bytes.append(byte)
        else:
            raise ValueError("unterminated export name")
        name = name_bytes.decode("ascii")
        if name in result:
            raise ValueError("duplicate export name")
        target = struct.unpack("<I", image.read(function_table + 4 * ordinal_index, 4))[0]
        if not target or export_rva <= target < export_rva + export_size:
            raise ValueError("null or forwarded export target")
        image.read(target, 1)  # Must resolve to actual file-backed bytes.
        result[name] = base + ordinal_index
    if result != EXPECTED_EXPORTS:
        raise ValueError(f"unexpected compiled exports: {result}")
    return result


def validate_release(project: pathlib.Path, asi: pathlib.Path,
                     profile: str = "Internal") -> list[str]:
    source, _ = read_tree(project)
    validate_policy_constants(definitions(source), V30_B4_21_POLICY_TARGET)
    version = re.search(r'^#define PATCH_VERSION "(\d+\.\d+\.\d+)"$', source, re.M)
    if not version:
        raise ValueError("source version missing")
    image = Image(asi, mapped=False)
    exports = read_exports(image)
    if version[1].encode("ascii") + b"\0" not in image.data:
        raise ValueError("compiled version differs from source")
    if b"STAGE_SHADOW_ONSET" in image.data:
        raise ValueError("compiled baseline contains the retired onset experiment")
    marker_presence = [marker in image.data for marker in INTERNAL_DIAGNOSTIC_MARKERS]
    if profile == "Internal" and not all(marker_presence):
        raise ValueError("internal diagnostic markers were compiled out")
    if profile == "Release" and any(marker_presence):
        raise ValueError("release contains internal diagnostic execution markers")
    return ["release policy: 30 maps / B4=21 / A8=4",
            f"compiled x64 DLL exports: {exports}",
            f"compiled version matches source: {version[1]}",
            f"compiled profile markers match: {profile}"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--asi", type=pathlib.Path, required=True)
    parser.add_argument("--profile", choices=("Internal", "Release"),
                        default="Internal")
    args = parser.parse_args()
    try:
        for check in validate_release(args.project, args.asi, args.profile):
            print(f"PASS: {check}")
    except (AssertionError, OSError, ValueError, IndexError, struct.error) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
