#!/usr/bin/env python3
"""Reconcile the truncated USB stream with the same probe's persisted stdout.

Neither input is edited. Require an exact complete prefix match of all probe
records, including runtime memory measurements; keep bridge diagnostics from
USB, and append only the actually recovered suffix. No qualification waiver.
"""
import argparse
import hashlib
import json
from pathlib import Path
from analyze_metal_probe import analyze, BOOT

STARTS = (b'NORD2_METAL probe start batch-surface-v8', b'NORD2_METAL probe start batch-features-v9')
START = STARTS[0]
BRIDGE = (b'NORD2_METAL tm ', b'NORD2_METAL draw ', b'NORD2_METAL batch draws=')
# batch-features-v9 keeps these on stdout only (the kernel LOG quota is 16 KiB
# per client), so they are not part of the USB prefix to match.
REQUIRED_PHASES = (b'configured', b'compute-complete', b'batch-complete')


def kernel_logged(line):
    for kind in (b'NORD2_METAL status phase=', b'NORD2_METAL memory phase='):
        if line.startswith(kind):
            return line[len(kind):].split(b' ', 1)[0] in REQUIRED_PHASES
    return not line.startswith((b'NORD2_METAL pixel x=', b'NORD2_METAL feature-pixel '))


def records(raw, usb=False):
    if usb:
        raw = raw.split(BOOT.encode())[-1]
    rows = []
    for line in raw.splitlines():
        if usb:
            prefix = b'NORD2 GPU USER: '
            if prefix not in line:
                continue
            line = line.split(prefix, 1)[1]
        if line.startswith(b'NORD2_METAL ') and not line.startswith(BRIDGE):
            rows.append(line)
    starts = [i for i, row in enumerate(rows) if row.startswith(b'NORD2_METAL probe start ')]
    if not starts or not rows[starts[-1]].startswith(STARTS):
        raise ValueError('Latest probe version mismatch')
    rows = rows[starts[-1]:]
    if not usb and rows[0].startswith(STARTS[1]):
        rows = [r for r in rows if kernel_logged(r)]
    return rows


def reconcile(usb, stdout):
    prefix, full = records(usb, True), records(stdout)
    minimum = 50 if prefix[0].startswith(STARTS[1]) else 100
    if len(prefix) < minimum or full[:len(prefix)] != prefix:
        raise ValueError('Persisted log does not match the complete USB prefix')
    if len(full) <= len(prefix):
        raise ValueError('No additional probe evidence recovered')
    suffix = full[len(prefix):]
    combined = usb + b'\n' + b'\n'.join(suffix) + b'\n'
    result = analyze(combined)
    provenance = dict(usb_sha256=hashlib.sha256(usb).hexdigest(),
        stdout_sha256=hashlib.sha256(stdout).hexdigest(),
        matching_prefix_records=len(prefix), recovered_suffix_records=len(suffix),
        usb_probe_bytes=sum(len(x)+1 for x in prefix),
        combined_sha256=hashlib.sha256(combined).hexdigest())
    return combined, result, provenance


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('usb', type=Path)
    ap.add_argument('stdout', type=Path)
    ap.add_argument('--output', required=True, type=Path)
    a = ap.parse_args()
    combined, result, provenance = reconcile(a.usb.read_bytes(), a.stdout.read_bytes())
    a.output.mkdir(parents=True, exist_ok=False)
    (a.output/'combined.log').write_bytes(combined)
    (a.output/'batch-analysis.json').write_text(json.dumps(result, indent=2)+'\n')
    (a.output/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    print(json.dumps(dict(**provenance, batch_hardware_qualified=result['batch_hardware_qualified'], ui_qualified=False)))
    if not result['batch_hardware_qualified']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
