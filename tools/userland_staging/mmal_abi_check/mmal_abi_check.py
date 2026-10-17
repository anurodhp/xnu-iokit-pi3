#!/usr/bin/env python3
"""Compares two clang -fdump-record-layouts-simple dumps of wire_types.c
(32-bit ARM Linux, arm64 Darwin): every MMAL wire record must have the same
size and field offsets. Exit 1 and list the differences otherwise."""
import re
import sys

WIRE = ('mmal_worker', 'MMAL_VC_', 'MMAL_DRIVER', 'MMAL_ES_SPECIFIC', 'MMAL_PARAMETER_HEADER',
        'MMAL_BUFFER_HEADER_VIDEO', 'MMAL_CORE_STAT')


def parse(path):
    recs, cur = {}, None
    for line in open(path):
        m = re.match(r'\s*Type: (.*)', line)
        if m:
            cur = re.sub(r'\(anonymous at [^)]*:(\d+):\d+\)', r'(anon@\1)', m.group(1).strip())
            recs[cur] = {}
            continue
        m = re.match(r'\s*Size:(\d+)', line)
        if m and cur:
            recs[cur]['size'] = int(m.group(1)) // 8
            continue
        m = re.match(r'\s*FieldOffsets: \[(.*)\]', line)
        if m and cur:
            recs[cur]['off'] = [int(x) // 8 for x in m.group(1).split(',') if x.strip()]
    return recs


a, b = parse(sys.argv[1]), parse(sys.argv[2])
names = [k for k in sorted(a) if any(w in k for w in WIRE)]
bad = [k for k in names if a[k] != b.get(k)]
for k in bad:
    print('MMAL wire layout differs: %s\n  32-bit ARM: %s\n  arm64:      %s' % (k, a[k], b.get(k)))
print('mmal_abi_check: %d wire records, %d differ' % (len(names), len(bad)))
sys.exit(1 if bad or len(names) < 30 else 0)
