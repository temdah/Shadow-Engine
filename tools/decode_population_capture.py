"""Validate and decode Internal SEPOP schema 1 captures using only the standard library."""

import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import sys
import zlib


HEADER_BYTES = 192
FOOTER_BYTES = 64
RECORD = struct.Struct('<6I')
MANAGER = struct.Struct('<i4xQ16I2i3q')
QUEUE = struct.Struct('<i4xQ7Ii3q')
CANDIDATE = struct.Struct('<6Q4I2i7Ii16I')
QUEUE_ROW = struct.Struct('<7Q14I')
CACHE = struct.Struct('<8IQ')
OWNER = struct.Struct('<3IiQ')
MAX_BYTES = HEADER_BYTES + FOOTER_BYTES + 1200 * (24 + 576 + 512 * 168 + 24 + 72 + 31 * 112)
GATE_NAMES = ('unknown', 'disabled', 'descriptorFactorBelowEpsilon',
              'candidateFactorBelowEpsilon', 'type2FlagsRejected', 'type4Excluded',
              'type3ConfigDisabled', 'coverageBelowThreshold', 'specialEligible', 'ordinaryEligible')
GATE_FIELDS = ('valid raw_type enabled descriptor_factor_bits candidate_factor_bits '
               'descriptor_flags type3_enabled threshold_bits reduced coverage width height '
               'epsilon_bits numerator_bits mxcsr').split()
MANAGER_FIELDS = ('complete ticket sequence thread count copied chain_complete prepared kept_count '
                  'keep_complete admission_valid admitted binding_unknown duplicate_candidates '
                  'limiter_active limiter_fail_open limiter_degraded identity_health call '
                  'renderer_epoch begin end observer_ticks').split()
QUEUE_FIELDS = ('complete ticket sequence thread original admitted requested_faces admitted_faces '
                'copied call begin end observer_ticks').split()
CANDIDATE_FIELDS = ('candidate descriptor spatial handle vehicle entity_id identity_valid '
                    'identity_matches handle_instances identity_age_ms published_manager '
                    'published_renderer valid handle_valid keep_known kept admitted binding_count '
                    'update cache_slot').split()
QUEUE_ROW_FIELDS = ('candidate descriptor spatial handle backing_before resource_before resource_after '
                    'valid handle_valid submitted faces width height resource_width resource_height '
                    'before_valid before_width before_height native_width native_height policy_sampled').split()


class CaptureError(ValueError):
    """Malformed, truncated, or unsupported capture; no analysis should be trusted."""


def require(condition, message):
    if not condition:
        raise CaptureError(message)


def unpack_map(fmt, fields, data, offset=0):
    return dict(zip(fields, fmt.unpack_from(data, offset), strict=True))


def parse_header(data):
    require(len(data) >= HEADER_BYTES + FOOTER_BYTES, 'truncated capture')
    require(data[:8] == b'SEPOP01\0', 'unsupported magic')
    fields = ('schema header_bytes endian record_header_bytes manager_prefix candidate_bytes '
              'queue_prefix queue_row_bytes max_candidates max_queue max_owners buckets period_ms '
              'window_ms pid serial session flags').split()
    h = unpack_map(struct.Struct('<18I'), fields, data, 8)
    expected = (1, 192, 0x01020304, 24, 576, 168, 72, 112, 512, 31, 8, 1200, 50, 60000)
    require(tuple(h[k] for k in fields[:14]) == expected, 'unsupported schema or layout')
    h.update(unpack_map(struct.Struct('<3qQ'), ('frequency', 'started', 'stopped', 'started_filetime'), data, 80))
    h.update(unpack_map(struct.Struct('<12I'), ('elapsed_ms stop_reason manager_count queue_count '
                       'manager_attempted queue_attempted manager_rate_skipped queue_rate_skipped '
                       'manager_contended queue_contended manager_missing queue_missing').split(), data, 112))
    h.update(unpack_map(struct.Struct('<q2I'), ('marked_qpc', 'marked_elapsed_ms', 'expected_bins'), data, 160))
    version = data[176:192]
    require(b'\0' in version, 'unterminated version')
    name, padding = version.split(b'\0', 1)
    require(name and all(v == 0 for v in padding), 'invalid version padding')
    try:
        h['version'] = name.decode('ascii')
    except UnicodeDecodeError as exc:
        raise CaptureError('non-ASCII version') from exc
    require(h['flags'] & ~7 == 0, 'unknown flags')
    require(h['frequency'] > 0 and h['started'] > 0, 'invalid QPC origin')
    if not h['flags'] & 4:
        require(h['stopped'] >= h['started'], 'invalid QPC stop')
    else:
        require(h['flags'] & 2, 'clock anomaly must qualify coverage as partial')
    require(h['elapsed_ms'] <= 60000 and h['stop_reason'] in (1, 2), 'invalid stop metadata')
    require(h['stop_reason'] != 1 or h['elapsed_ms'] == 60000, 'deadline lacks full elapsed window')
    require(h['expected_bins'] <= 1200, 'invalid expected bucket count')
    for stream in ('manager', 'queue'):
        count = h[f'{stream}_count']
        require(count <= h['expected_bins'], 'stream count exceeds expected buckets')
        require(h[f'{stream}_missing'] == h['expected_bins'] - count, 'incorrect missing bucket count')
        require(count <= h[f'{stream}_attempted'], 'more records than callback attempts')
    require(not (h['manager_missing'] or h['queue_missing']) or h['flags'] & 2,
            'missing buckets require partial coverage flag')
    if not h['flags'] & 1:
        require(h['marked_qpc'] == h['marked_elapsed_ms'] == 0, 'unmarked capture has annotation')
    elif not h['flags'] & 4:
        require(h['started'] <= h['marked_qpc'] <= h['stopped'], 'annotation outside capture')
        require(h['marked_elapsed_ms'] <= h['elapsed_ms'], 'annotation time exceeds window')
    return h


def cache_copy(data, offset):
    result = unpack_map(CACHE, 'valid count encoded capacity a8 dimension b0 b4 base'.split(), data, offset)
    result['owners'] = [unpack_map(OWNER, ('valid', 'identity', 'initialized', 'age', 'backing'),
                                 data, offset + 40 + i * 24) for i in range(8)]
    return result


def float_bits(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def round32(value):
    try:
        return struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError:
        return float('inf') if value >= 0 else -float('inf')


def normal_or_zero(bits):
    magnitude = bits & 0x7fffffff
    return magnitude == 0 or 0x00800000 <= magnitude < 0x7f800000


def gate_result(g):
    """Copied pre-budget reconstruction, independent of observed native admission."""
    valid = g['valid']
    if not valid & 1:
        return 0
    if not g['enabled']:
        return 1
    if not valid & 16 or not all(normal_or_zero(g[k]) for k in ('epsilon_bits', 'descriptor_factor_bits')):
        return 0
    epsilon = float_bits(g['epsilon_bits'])
    if epsilon > float_bits(g['descriptor_factor_bits']):
        return 2
    if not valid & 2 or not normal_or_zero(g['candidate_factor_bits']):
        return 0
    if epsilon > float_bits(g['candidate_factor_bits']):
        return 3
    if g['raw_type'] == 0:
        return 8
    if g['raw_type'] == 2:
        return 8 if g['descriptor_flags'] & 0x84000000 else 4
    if g['raw_type'] == 4:
        return 5
    if not valid & 4:
        return 0
    if g['raw_type'] == 3 and not g['type3_enabled']:
        return 6
    if not normal_or_zero(g['threshold_bits']):
        return 0
    threshold = float_bits(g['threshold_bits'])
    if threshold <= 0 or g['reduced']:
        return 9
    if valid & 40 != 40 or g['mxcsr'] & 0x6000 or not normal_or_zero(g['numerator_bits']):
        return 0
    numerator = float_bits(g['numerator_bits'])
    # Schema 1's proof fixes the numerator to 1; reject unproved arithmetic.
    if g['numerator_bits'] != 0x3f800000:
        return 0
    product = (g['width'] * g['height']) & 0xffffffff
    denominator = 1 if product & 0x80000000 or product <= 1 else product
    scale = round32(numerator / round32(denominator))
    scaled = round32(round32(g['coverage']) * scale)
    return 7 if scaled < threshold else 9


def decode_record(data, offset, record_type, bucket, size):
    fmt = MANAGER if record_type == 1 else QUEUE
    fields = MANAGER_FIELDS if record_type == 1 else QUEUE_FIELDS
    packet = unpack_map(fmt, fields, data, offset)
    packet['stream'] = 'manager' if record_type == 1 else 'queue'
    packet['bucket'] = bucket
    rows = []
    if record_type == 1:
        packet['before'] = cache_copy(data, offset + 112)
        packet['after'] = cache_copy(data, offset + 344)
        for index in range(packet['copied']):
            values = CANDIDATE.unpack_from(data, offset + 576 + index * 168)
            row = dict(zip(CANDIDATE_FIELDS, values[:20], strict=True))
            row['gate'] = dict(zip(GATE_FIELDS, values[20:35], strict=True))
            row['stored_gate_result'] = values[35]
            row['reconstructed_gate_result'] = gate_result(row['gate'])
            row['reconstructed_gate_name'] = GATE_NAMES[row['reconstructed_gate_result']]
            rows.append(row)
    else:
        for index in range(packet['copied']):
            rows.append(unpack_map(QUEUE_ROW, QUEUE_ROW_FIELDS, data, offset + 72 + index * 112))
    packet['rows'] = rows
    return packet


class Capture:
    """Bounded immutable file snapshot, validated completely before exposing records."""

    def __init__(self, data):
        require(len(data) <= MAX_BYTES, 'capture exceeds schema storage bound')
        self.data = data
        self.header = h = parse_header(data)
        self.locations = []
        offset, seen = HEADER_BYTES, {1: set(), 2: set()}
        while offset < len(data) - FOOTER_BYTES:
            require(offset + 24 <= len(data) - FOOTER_BYTES, 'truncated record header')
            kind, header_size, bucket, size, sequence, reserved = RECORD.unpack_from(data, offset)
            require(kind in (1, 2) and header_size == 24 and reserved == 0, 'invalid record header')
            require(bucket < h['expected_bins'] and bucket not in seen[kind], 'invalid or duplicate bucket')
            require(not seen[kind] or bucket > max(seen[kind]), 'stream bucket order regressed')
            require(sequence == len(seen[kind]) + 1, 'nonconsecutive stream sequence')
            prefix, row_size, bound = (576, 168, 512) if kind == 1 else (72, 112, 31)
            require(prefix <= size <= prefix + bound * row_size, 'invalid payload size')
            start = offset + 24
            require(start + size <= len(data) - FOOTER_BYTES, 'truncated record payload')
            require(data[start + 4:start + 8] == b'\0' * 4, 'nonzero packet padding')
            fmt, fields = (MANAGER, MANAGER_FIELDS) if kind == 1 else (QUEUE, QUEUE_FIELDS)
            packet = unpack_map(fmt, fields, data, start)
            require(packet['complete'] == 1 and packet['sequence'] == sequence, 'incomplete or mismatched packet')
            expected_ticket = (h['serial'] << 32) | (0x80000000 if kind == 2 else 0) | (bucket + 1)
            require(packet['ticket'] == expected_ticket, 'ticket does not identify capture and bucket')
            require(packet['copied'] <= bound and size == prefix + packet['copied'] * row_size, 'row count mismatch')
            if not h['flags'] & 4:
                require(packet['begin'] >= h['started'] and packet['end'] >= packet['begin'], 'invalid packet QPC')
                require(packet['observer_ticks'] >= 0, 'negative observer interval')
                # Reservation precedes clearing/copying. A boundary crossing or deschedule
                # may delay begin into a later bucket; the ticket retains the chosen bucket.
                actual_bucket = (packet['begin'] - h['started']) * 1000 // h['frequency'] // h['period_ms']
                require(actual_bucket >= bucket, 'packet time precedes reserved bucket')
            if kind == 1:
                require(packet['copied'] <= packet['count'], 'copied candidates exceed source count')
            seen[kind].add(bucket)
            self.locations.append((start, kind, bucket, size))
            offset = start + size
        if not h['flags'] & 4:
            elapsed_ticks = h['stopped'] - h['started']
            require(h['elapsed_ms'] == min(60000, elapsed_ticks * 1000 // h['frequency']),
                    'elapsed milliseconds disagree with QPC duration')
            expected_bins = min(1200, (elapsed_ticks * 20 + h['frequency'] - 1) // h['frequency'])
            retained_bins = max((bucket + 1 for buckets in seen.values() for bucket in buckets), default=0)
            require(h['expected_bins'] == max(expected_bins, retained_bins), 'expected buckets disagree with duration')
        require(offset == len(data) - 64, 'misaligned footer')
        require(data[offset:offset + 8] == b'SEPOEND\0', 'missing completion footer')
        f = unpack_map(struct.Struct('<6I2Q2IQ'),
                       ('schema footer_bytes records managers queues crc32 bytes_before_footer '
                        'total_bytes flags expected_bins reserved').split(), data, offset + 8)
        require(f['schema'] == 1 and f['footer_bytes'] == 64 and f['reserved'] == 0, 'invalid footer schema')
        require(f['records'] == len(self.locations) == f['managers'] + f['queues'], 'footer record count mismatch')
        require(f['managers'] == h['manager_count'] == len(seen[1]), 'manager count mismatch')
        require(f['queues'] == h['queue_count'] == len(seen[2]), 'queue count mismatch')
        require(f['bytes_before_footer'] == offset and f['total_bytes'] == len(data), 'footer byte count mismatch')
        require(f['flags'] == h['flags'] and f['expected_bins'] == h['expected_bins'], 'footer metadata mismatch')
        require(zlib.crc32(memoryview(data)[:offset]) == f['crc32'], 'capture CRC32 mismatch')
        self.footer = f

    @classmethod
    def read(cls, path):
        with Path(path).open('rb') as stream:
            require(stream.seek(0, 2) <= MAX_BYTES, 'capture exceeds schema storage bound')
            stream.seek(0)
            data = stream.read(MAX_BYTES + 1)
        return cls(data)

    def records(self):
        for location in self.locations:
            yield decode_record(self.data, *location)

    def summary(self):
        gates, totals, timeline = Counter(), Counter(), []
        for p in self.records():
            stream = p['stream']
            counts = Counter()
            if stream == 'manager':
                counts['copied_candidates'] = p['copied']
                counts['uncopied_candidates'] = p['count'] - p['copied']
                if p['admission_valid']:
                    counts['observed_native_bindings'] = p['admitted']
                else:
                    counts['unknown_native_binding_packets'] += 1
                for r in p['rows']:
                    gates[r['reconstructed_gate_name']] += 1
                    if r['reconstructed_gate_result'] == 9:
                        counts['ordinary_eligible'] += 1
                        if not r['keep_known']:
                            counts['ordinary_eligible_keep_unknown'] += 1
                        elif not r['kept']:
                            counts['ordinary_eligible_policy_filtered'] += 1
                        elif p['admission_valid'] and not r['admitted']:
                            counts['ordinary_eligible_kept_unbound'] += 1
                            if r['identity_valid']:
                                counts['identified_ordinary_eligible_kept_unbound'] += 1
                counts['incomplete_candidate_chains'] = int(not p['chain_complete'])
            else:
                counts['copied_queue_rows'] = p['copied']
                for r in p['rows']:
                    if r['submitted'] and r['policy_sampled'] and r['valid'] & 4:
                        counts['cpu_resource_dimension_mismatches'] += int(
                            (r['width'], r['height']) != (r['resource_width'], r['resource_height']))
            totals.update(counts)
            timeline.append({'stream': stream, 'bucket': p['bucket'], 'call': p['call'],
                             'elapsed_ms': None if self.header['flags'] & 4 else
                             (p['begin'] - self.header['started']) * 1000 / self.header['frequency'],
                             **counts})
        return {'header': self.header, 'footer': self.footer, 'totals': dict(totals),
                'reconstructed_gates': dict(gates), 'timeline': timeline,
                'limits': ['Samples are at most one callback per 50 ms bucket per stream; events between samples can be missed.',
                           'Totals count sampled row occurrences, not unique candidates or vehicles; stream buckets do not establish causal pairing.',
                           'Copied eligibility is pre-budget reconstruction, not a native branch trace.',
                           'CPU resources and native binding observations do not prove visible or GPU readiness.',
                           'Partial flags, missing buckets and uncopied candidates limit coverage; an annotation is optional.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--summary', type=Path, help='create a new JSON summary (default: stdout)')
    parser.add_argument('--records', type=Path, help='create a new JSONL file with all copied rows')
    args = parser.parse_args()
    try:
        capture = Capture.read(args.capture)
        summary = json.dumps(capture.summary(), indent=2) + '\n'
        if args.summary:
            with args.summary.open('x', encoding='utf-8') as output:
                output.write(summary)
        else:
            sys.stdout.write(summary)
        if args.records:
            with args.records.open('x', encoding='utf-8') as output:
                for record in capture.records():
                    output.write(json.dumps(record, separators=(',', ':')) + '\n')
    except (OSError, CaptureError) as exc:
        parser.exit(1, f'Capture rejected: {exc}\n')


if __name__ == '__main__':
    main()
