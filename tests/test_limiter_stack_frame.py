#!/usr/bin/env python3
"""Read-only TinyCC PE proof that disabled limiter calls avoid the large frame.

Run explicitly with --asi and optional --baseline/--capstone-path. The default
layout preserves the .77/.79 gate; use --candidate-layout v80 or --layout-probe
with the compiled limiter_layout_probe.exe for .80. The ASI is never loaded;
the optional layout probe only prints compiler offsetof results.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import subprocess
import sys

from validate_runtime_corpus import Image

KNOWN_LAYOUTS = {
    'v77': {'schema': 1, 'limiter_nodes': 0x78, 'candidate_records': 0x168,
            'limiter_enabled': 0x28},
    'v80': {'schema': 1, 'limiter_nodes': 0x78, 'candidate_records': 0x188,
            'limiter_enabled': 0x28},
    'v81': {'schema': 1, 'limiter_nodes': 0x78, 'candidate_records': 0x1B0,
            'limiter_enabled': 0x28},
}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


class Code:
    def __init__(self, path, decoder):
        self.image = Image(path, mapped=False)
        data = self.image.data
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        require(struct.unpack_from('<H', data, pe + 4)[0] == 0x8664, 'expected x64 PE')
        table, size = struct.unpack_from('<II', data, pe + 24 + 136)
        require(table and size and size % 12 == 0, 'missing exception records')
        self.functions = {}
        for offset in range(0, size, 12):
            body, end, _ = struct.unpack('<III', self.image.read(table + offset, 12))
            require(end > body, 'invalid function bounds')
            instructions = list(decoder.disasm(self.image.read(body, end - body), body))
            self.functions[body - 11] = (body, end, instructions)

    def frame(self, entry):
        raw = self.image.read(entry, 11)
        if raw[:7] == bytes.fromhex('554889e54881ec'):
            return struct.unpack_from('<I', raw, 7)[0], 0
        require(raw[0] == 0xB8 and raw[5] == 0xE8 and raw[10] == 0x90,
                f'unrecognized TinyCC prologue at {entry:#x}')
        frame = struct.unpack_from('<I', raw, 1)[0]
        helper = entry + 10 + struct.unpack_from('<i', raw, 6)[0]
        # The shipped compiler helper probes every page, then the remainder.
        signature = bytes.fromhex(
            '48872c2455488d6c2408514889e94863c04881e900100000488501'
            '482d00100000483d001000007de84829c14885014889e04889cc'
            '488b08ff6008')
        require(self.image.read(helper, len(signature)) == signature,
                'unrecognized stack probe helper')
        return frame, (frame + 4095) // 4096

    def wrapper(self, layout):
        matches = []
        for entry, (_, _, instructions) in self.functions.items():
            calls = [i for i in instructions if i.mnemonic == 'call']
            if len(calls) < 2 or calls[0].op_str != calls[1].op_str:
                continue
            prefix = [i for i in instructions if i.address < calls[1].address]
            operands = {i.op_str for i in prefix if i.mnemonic == 'movabs'}
            # Two exact compiler-layout header memsets before any policy call.
            # Layout selection is bounded to the independently recorded versions.
            if (f"rax, {layout['limiter_nodes']:#x}" in operands and
                    f"rcx, {layout['candidate_records']:#x}" in operands):
                matches.append(entry)
        require(len(matches) == 1, f'expected one limiter header initializer, found {len(matches)}')
        return matches[0]

    def path_calls(self, entry, address):
        instructions = self.functions[entry][2]
        by_address = {i.address: i for i in instructions}
        calls, visited = [], set()
        while address in by_address and address not in visited:
            visited.add(address)
            instruction = by_address[address]
            if instruction.mnemonic == 'ret':
                return calls
            if instruction.mnemonic == 'call':
                require(instruction.op_str.startswith('0x'), 'indirect wrapper call')
                calls.append(int(instruction.op_str, 16))
            if instruction.mnemonic == 'jmp':
                address = int(instruction.op_str, 16)
                continue
            require(not instruction.mnemonic.startswith('j'), 'unexpected second policy branch')
            address += instruction.size
        raise AssertionError('wrapper path does not reach a bounded return')


def inspect(path, decoder, candidate, layout):
    code = Code(path, decoder)
    wrapper = code.wrapper(layout)
    frame, probes = code.frame(wrapper)
    result = {'sha256': hashlib.sha256(code.image.data).hexdigest().upper(),
              'wrapper_rva': hex(wrapper), 'wrapper_frame_bytes': frame,
              'wrapper_probe_touches': probes, 'layout': layout}
    if not candidate:
        require(probes > 0, 'baseline no longer demonstrates unconditional page probing')
        return result
    require(probes == 0 and frame < 4096, 'disabled wrapper still probes a large frame')
    instructions = code.functions[wrapper][2]
    branches = [i for i in instructions if i.mnemonic.startswith('j') and i.mnemonic != 'jmp']
    require(len(branches) == 1 and branches[0].mnemonic == 'je', 'unexpected enabled-field branch')
    branch = branches[0]
    index = instructions.index(branch)
    require(instructions[index - 1].mnemonic == 'cmp' and
            instructions[index - 1].op_str == 'ecx, 0' and
            instructions[index - 2].op_str == 'ecx, dword ptr [rax]' and
            instructions[index - 3].op_str == f"rax, {layout['limiter_enabled']:#x}",
            'branch is not the unchanged chain.enabled zero check')
    disabled = code.path_calls(wrapper, int(branch.op_str, 16))
    enabled = code.path_calls(wrapper, branch.address + branch.size)
    require(len(disabled) == 1 and len(enabled) == 1, 'unexpected wrapper path calls')
    require(disabled[0] != enabled[0], 'enabled and disabled paths call the same helper')
    helper_frame, helper_probes = code.frame(enabled[0])
    require(helper_probes > 0 and helper_frame >= 65536, 'large preparation frame not isolated')
    for instruction in instructions:
        if instruction.mnemonic != 'call' or instruction.address >= branch.address:
            continue
        target = int(instruction.op_str, 16)
        if target in code.functions:
            require(code.frame(target)[1] == 0, 'pre-policy callee probes stack pages')
        else:
            require(code.image.read(target, 2) == b'\xff\x25', 'unknown pre-policy import thunk')
    require(code.frame(disabled[0])[1] == 0, 'disabled counter callee probes stack pages')
    result.update(enabled_helper_rva=hex(enabled[0]), helper_frame_bytes=helper_frame,
                  enabled_helper_probe_touches=helper_probes,
                  disabled_path_reaches_heavy_helper=False)
    return result


def candidate_layout(args):
    if args.layout_probe:
        process = subprocess.run([str(args.layout_probe.resolve())], check=True,
                                 capture_output=True, text=True)
        observed = json.loads(process.stdout)
        require(observed in KNOWN_LAYOUTS.values(),
                f'layout probe returned an unreviewed layout: {observed!r}')
        if args.candidate_layout:
            require(observed == KNOWN_LAYOUTS[args.candidate_layout],
                    'compiled probe does not match the explicitly selected layout')
        return observed
    return KNOWN_LAYOUTS[args.candidate_layout or 'v77']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asi', required=True, type=pathlib.Path)
    parser.add_argument('--baseline', type=pathlib.Path)
    parser.add_argument('--capstone-path', type=pathlib.Path)
    parser.add_argument('--candidate-layout', choices=KNOWN_LAYOUTS)
    parser.add_argument('--layout-probe', type=pathlib.Path,
                        help='TinyCC-built tests/limiter_layout_probe.exe')
    parser.add_argument('--baseline-layout', choices=KNOWN_LAYOUTS, default='v77')
    args = parser.parse_args()
    if args.capstone_path:
        sys.path.insert(0, str(args.capstone_path.resolve()))
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    result = {'candidate': inspect(args.asi, decoder, True, candidate_layout(args))}
    if args.baseline:
        result['baseline'] = inspect(args.baseline, decoder, False,
                                     KNOWN_LAYOUTS[args.baseline_layout])
    print(json.dumps(result, indent=2))
    print('PASS compiled limiter bypass: small wrapper; large frame only on enabled path')


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, ValueError, struct.error, subprocess.CalledProcessError) as error:
        print(f'FAIL {error}', file=sys.stderr)
        sys.exit(1)
