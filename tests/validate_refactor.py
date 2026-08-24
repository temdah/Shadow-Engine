#!/usr/bin/env python3
"""Verify that a Shadow Engine refactor preserves the v1.2.0 patch contract."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

POLICY_CONSTANTS = {
    "ORIGINAL_LOCAL_MAPS", "EXTRA_LOCAL_MAPS", "TOTAL_LOCAL_MAPS",
    "TARGET_DYNAMIC_B4", "TARGET_CACHE_A8", "OWNER_VECTOR_RESERVE_CAPACITY",
    "RENDER_QUEUE_ENTRY_BYTES", "RENDER_QUEUE_COUNT_OFFSET",
    "RENDER_QUEUE_FIRST_MAP_OFFSET", "RENDER_QUEUE_SECOND_MAP_OFFSET",
    "RENDER_QUEUE_FIRST_MAP_STRIDE", "RENDER_QUEUE_LAYOUT_STRIDE",
    "RENDER_QUEUE_TAIL_OFFSET", "RENDER_QUEUE_RESULT_OFFSET",
    "NATIVE_SLICE_RESULT_COUNT", "REQUIRED_PASS_MAX_KEY",
    "REQUIRED_PASS_TABLE_SLOTS", "FIRST_EXTRA_PASS_SLOT",
    "EXTRA_PASS_SLOT_COUNT",
}

EXPECTED_PROFILES = {
    "supported-04DF": ("0x06595000U", "0x5CD045B4U", "0x01E5793CU", "0x04760FA6U"),
    "a4ee-signed-variant": ("0x06595000U", "0x5CD13864U", "0x01E4FD51U", "0x05824EE5U"),
    "vmpless-1.06": ("0x04ED6000U", "0x5BE5AA1AU", "0x048AF60CU", "0x02F58AC8U"),
    "complete-edition-1.06.329": ("0x041AB000U", "0x55DC1ECDU", "0x03B84DA7U", "0x021FD388U"),
    "asia-miru-1.06.329": ("0x07EE1000U", "0x543589C5U", "0x02B88696U", "0x07C88AFEU"),
}

RETIRED_MODULES = {"40_render_record_repair.inc", "50_resource_lifecycle_trace.inc"}

V200_POLICY_TARGET = {
    "ORIGINAL_LOCAL_MAPS": "16U",
    "EXTRA_LOCAL_MAPS": "14U",
    "TOTAL_LOCAL_MAPS": "30U",
    "TARGET_DYNAMIC_B4": "25U",
    "TARGET_CACHE_A8": "4U",
    "OWNER_VECTOR_RESERVE_CAPACITY": "8U",
    "RENDER_QUEUE_ENTRY_BYTES": "0x24C0U",
    "RENDER_QUEUE_FIRST_MAP_OFFSET": "0x47350U",
    "RENDER_QUEUE_SECOND_MAP_OFFSET": "0x473CCU",
    "RENDER_QUEUE_FIRST_MAP_STRIDE": "0x24C4U",
    "RENDER_QUEUE_LAYOUT_STRIDE": "0x24C8U",
    "RENDER_QUEUE_COUNT_OFFSET": "0x47448U",
    "RENDER_QUEUE_TAIL_OFFSET": "0x47450U",
    "RENDER_QUEUE_RESULT_OFFSET": "0x47460U",
    "NATIVE_SLICE_RESULT_COUNT": "17U",
    "REQUIRED_PASS_MAX_KEY": "0x3D0CU",
    "REQUIRED_PASS_TABLE_SLOTS": "62U",
    "FIRST_EXTRA_PASS_SLOT": "34U",
    "EXTRA_PASS_SLOT_COUNT": "28U",
}


def read_tree(root: pathlib.Path) -> tuple[str, dict[str, str]]:
    files = {
        path.relative_to(root).as_posix(): path.read_text(encoding="utf-8")
        for path in sorted((root / "src").rglob("*"))
        if path.suffix in {".c", ".inc"}
    }
    return "\n".join(files.values()), files


def definitions(source: str) -> dict[str, str]:
    return dict(re.findall(r"^#define\s+([A-Z][A-Z0-9_]+)\s+([^\r\n]+)", source, re.M))


def array_block(source: str, name: str) -> str:
    match = re.search(
        rf"static const [^\n]+\b{re.escape(name)}\[.*?\]\s*=\s*\{{(.*?)\n\}};",
        source, re.S,
    )
    if not match:
        raise AssertionError(f"array not found: {name}")
    return match.group(1)


def hex_pairs(block: str) -> list[tuple[int, int]]:
    return [
        (int(left, 16), int(right, 16))
        for left, right in re.findall(
            r"\{\s*0x([0-9A-Fa-f]+)ULL,\s*0x([0-9A-Fa-f]+)ULL", block
        )
    ]


def byte_array(block: str) -> bytes:
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", block))


def installed_hooks(source: str) -> set[str]:
    return set(re.findall(r"install_detour\([^;]*?\b(hooked_[A-Za-z0-9_]+)\b", source, re.S))


def validate(
    baseline: pathlib.Path,
    candidate: pathlib.Path,
    policy_target_v200: bool,
) -> list[str]:
    base_source, _ = read_tree(baseline)
    candidate_source, candidate_files = read_tree(candidate)
    checks: list[str] = []

    base_defines = definitions(base_source)
    candidate_defines = definitions(candidate_source)
    for name in sorted(POLICY_CONSTANTS):
        expected = (
            V200_POLICY_TARGET[name]
            if policy_target_v200
            else base_defines.get(name)
        )
        assert candidate_defines.get(name) == expected, (
            f"policy constant changed: {name}: "
            f"expected {expected!r}, got {candidate_defines.get(name)!r}"
        )
    checks.append(
        f"policy constants: {len(POLICY_CONSTANTS)} "
        + ("match v2.0.0 policy target" if policy_target_v200 else "unchanged")
    )

    base_rvas = {key: value for key, value in base_defines.items() if key.endswith("_RVA")}
    candidate_rvas = {key: value for key, value in candidate_defines.items() if key.endswith("_RVA")}
    assert candidate_rvas == base_rvas, "engine RVA constant set changed"
    checks.append(f"engine RVA constants: {len(base_rvas)} unchanged")

    tail_pattern = r"\{\s*0x([0-9A-Fa-f]+)ULL,\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\s*\}"
    base_tails = re.findall(tail_pattern, array_block(base_source, "g_tail_patches"))
    candidate_tails = re.findall(tail_pattern, array_block(candidate_source, "g_tail_patches"))
    if policy_target_v200:
        assert len(base_tails) == len(candidate_tails) == 46, (
            f"queue-tail count changed: baseline={len(base_tails)} "
            f"candidate={len(candidate_tails)}"
        )
        for base_tail, candidate_tail in zip(base_tails, candidate_tails):
            base_rva, base_old, base_new = (int(value, 16) for value in base_tail)
            candidate_rva, candidate_old, candidate_new = (
                int(value, 16) for value in candidate_tail
            )
            assert candidate_rva == base_rva and candidate_old == base_old, (
                f"queue-tail source changed at RVA 0x{base_rva:X}"
            )
            if base_old == 0x270D0:
                stride = 0x24C0
            elif base_old == 0x27114:
                stride = 0x24C4
            else:
                stride = 0x24C8
            expected_new = base_new + (30 - 24) * stride
            assert candidate_new == expected_new, (
                f"queue-tail target mismatch at RVA 0x{base_rva:X}: "
                f"expected 0x{expected_new:X}, got 0x{candidate_new:X}"
            )
        checks.append(
            "queue-tail relocations: 46 follow record/mapping/post-mapping "
            "strides 0x24C0/0x24C4/0x24C8"
        )
    else:
        assert candidate_tails == base_tails and len(candidate_tails) == 46, (
            f"queue-tail table changed: baseline={len(base_tails)} "
            f"candidate={len(candidate_tails)}"
        )
        checks.append("queue-tail relocations: 46 unchanged")

    base_asia = hex_pairs(array_block(base_source, "g_asia_rva_map"))
    candidate_asia = hex_pairs(array_block(candidate_source, "g_asia_rva_map"))
    assert candidate_asia == base_asia and len(candidate_asia) == 77, (
        f"Asia map changed: baseline={len(base_asia)} candidate={len(candidate_asia)}"
    )
    assert candidate_asia == sorted(candidate_asia), "Asia map is not sorted"
    checks.append("Asia explicit map: 77 sorted entries unchanged")

    prologues = (
        "g_resource_prologue", "g_register_pass_prologue", "g_manager_prologue",
        "g_renderer_queue_prologue", "g_resource_wrapper_prologue",
        "g_face_cost_reader_prologue", "g_frame_builder_prologue",
        "g_frame_builder_asia_prologue", "g_post_call_object_renderers_prologue",
        "g_render_record_release_prologue", "g_render_record_constructor_prologue",
    )
    for name in prologues:
        assert byte_array(array_block(candidate_source, name)) == byte_array(
            array_block(base_source, name)
        ), f"hook signature changed: {name}"
    checks.append(f"active hook signatures: {len(prologues)} unchanged")

    assert installed_hooks(candidate_source) == installed_hooks(base_source), "installed detour target set changed"
    checks.append(f"installed detour targets: {len(installed_hooks(candidate_source))} unchanged")

    profile_source = candidate_files["src/modules/05_runtime_profiles.inc"]
    for name, identity in EXPECTED_PROFILES.items():
        start = profile_source.find(f'"{name}"')
        assert start >= 0, f"profile missing: {name}"
        window = profile_source[start : start + 500]
        for marker in identity:
            assert marker in window, f"profile identity mismatch: {name} lacks {marker}"
    checks.append("runtime profiles: five exact PE identities present")

    aggregator = candidate_files["src/shadow_engine_patch.c"]
    assert '"modules/05_runtime_profiles.inc"' in aggregator
    assert '"modules/15_patch_transaction.inc"' in aggregator
    assert '"modules/40_external_slice_results.inc"' in aggregator
    assert '"modules/65_runtime_preflight.inc"' in aggregator
    assert '"modules/80_runtime_entry.inc"' in aggregator
    for retired in RETIRED_MODULES:
        assert retired not in aggregator and not any(path.endswith(retired) for path in candidate_files), (
            f"retired module still compiled: {retired}"
        )
    assert not re.search(r"\bg_runtime_profile\b", candidate_source)
    assert not re.search(r"runtime_profile_id\(\)\s*==\s*5", candidate_source)
    checks.append("architecture: no profile-number branch or retired module")

    transaction_path = "src/modules/15_patch_transaction.inc"
    primitives_path = "src/modules/10_runtime_primitives.inc"
    bootstrap_path = "src/modules/70_bootstrap_orchestration.inc"
    for path, source in candidate_files.items():
        if path != transaction_path:
            assert "VirtualAlloc(" not in source, (
                f"executable allocation bypasses transaction: {path}"
            )
        if path not in {transaction_path, primitives_path}:
            assert "raw_write_bytes(" not in source, (
                f"executable write bypasses transaction: {path}"
            )
    bootstrap = candidate_files[bootstrap_path]
    assert bootstrap.count("patch_transaction_begin(") == 2
    assert bootstrap.count("patch_transaction_commit(") == 2
    assert bootstrap.count("patch_transaction_rollback(") == 2
    checks.append("transactions: all executable writes/allocations routed; two phased rollbacks")

    shared = candidate_files["src/modules/00_shared_config_state.inc"]
    if policy_target_v200:
        assert '#define PATCH_VERSION "2.0.0"' in shared
        assert "#define PHYSICAL_QUEUE_ENTRIES (TOTAL_LOCAL_MAPS+1U)" in shared
        assert "#define QUEUE_ARRAY_CLEAR_BYTES (4U+TOTAL_LOCAL_MAPS*4U)" in shared
        expansion = candidate_files["src/modules/60_engine_expansion.inc"]
        assert "(unsigned char)TOTAL_LOCAL_MAPS" in expansion
        assert "(unsigned char)TARGET_DYNAMIC_B4" in expansion
        assert "EXTRA_SLICE_RESULT_COUNT" in candidate_source
        checks.append(
            "v2.0.0 policy: 30 maps, 31 queue entries, 14 external maps, "
            "28 pass slots, 13 external results, B4=25, A8=4"
        )
    for state_type in (
        "BootstrapState", "HookBindings", "ManagerRendererState",
        "ResourcePassState", "ExternalResultState", "ShadowEngineContext",
    ):
        assert f"typedef struct {state_type}" in shared, (
            f"subsystem context missing: {state_type}"
        )
    assert "static ShadowEngineContext g_shadow_engine;" in shared
    assert not re.search(r"^static\s+volatile\s+LONG\s+g_", shared, re.M)
    assert not re.search(r"^#define\s+g_", shared, re.M)
    assert candidate_source.count("g_shadow_engine.") >= 400
    checks.append("state ownership: one explicit root context with five cohesive subsystem states")

    preflight = candidate_files["src/modules/65_runtime_preflight.inc"]
    orchestration = candidate_files[bootstrap_path]
    runtime_entry = candidate_files["src/modules/80_runtime_entry.inc"]
    assert "select_runtime_profile(" in preflight
    assert "run_signature_probe(" in preflight
    assert "DllMain(" not in preflight
    assert "prepare_early_patch_plan(" in orchestration
    assert "commit_early_patch_plan(" in orchestration
    assert "stage_e_worker(" not in orchestration
    assert len(orchestration.splitlines()) < 350
    assert "stage_e_worker(" in runtime_entry
    assert "DllMain(" in runtime_entry
    assert "select_runtime_profile(" not in runtime_entry
    checks.append("cohesion: preflight, patch orchestration, and runtime entry separated")
    return checks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", required=True, type=pathlib.Path)
    parser.add_argument("--policy-target-v200", action="store_true")
    args = parser.parse_args()
    try:
        checks = validate(
            args.baseline.resolve(),
            args.candidate.resolve(),
            args.policy_target_v200,
        )
    except (AssertionError, KeyError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    for check in checks:
        print(f"PASS: {check}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
