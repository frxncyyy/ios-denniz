#!/usr/bin/env python3
"""Summarize paired kernel TGJ samples, not FPS or an unbiased workload average.

Kernel0214 samples every64th job plus startup and slow jobs. Use one complete
USB segment; reconnects replay earlier records. GPU time includes completion
waiting in the kernel, not a hardware cycle counter.
"""
import argparse
import json
import re
from pathlib import Path


def samples(text, replay=False):
    if replay:
        if text.count('TEGU GESTURE: begin') != 1 or text.count('TEGU GESTURE: completed') != 1:
            raise ValueError('need exactly one complete gesture replay in one USB segment')
        text = text.split('TEGU GESTURE: begin', 1)[1].split('TEGU GESTURE: completed', 1)[0]
    first, second, heaps = {}, {}, {}
    for line in text.splitlines():
        match = re.search(r'TEGU TGJ([12]) (TIME2?): job (\d+) (.*)', line)
        if match:
            abi, kind, job, rest = match.groups()
            row = {k: int(v, 16 if v.startswith('0x') else 10)
                   for k, v in re.findall(r'\b(\w+) (0x[0-9a-f]+|[0-9]+)\b', rest)}
            extent = re.search(r'target (\d+)x(\d+)', rest)
            if extent:
                row['width'], row['height'] = map(int, extent.groups())
            dest = first if kind == 'TIME' else second
            key = int(job)
            if key in dest:
                raise ValueError('duplicate job record: use one USB segment')
            dest[key] = row
        match = re.search(r'TEGU TGJ HEAP: job (\d+) known (\d+) head (\d+) tail (\d+) used (\d+)', line)
        if match:
            job, known, head, tail, used = map(int, match.groups())
            if known:
                if head < 64 or head - 64 + tail != used:
                    raise ValueError(f'inconsistent heap extents for job {job}')
                heaps[job] = dict(heap_head=head, heap_tail=tail, heap_used=used)
    rows = []
    for job in sorted(first.keys() & second.keys()):
        a, b = first[job], second[job]
        if not {'total', 'rc', 'width', 'height'} <= a.keys() or 'gpu' not in b:
            continue
        if a['rc'] or b['gpu'] > a['total']:
            continue
        rows.append(dict(a, **b, **heaps.get(job, {}), job=job,
                         non_gpu=a['total'] - b['gpu']))
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--replay', action='store_true')
    p.add_argument('--json', action='store_true')
    a = p.parse_args()
    try:
        rows = samples(a.log.read_text(errors='replace'), a.replay)
    except ValueError as e:
        p.error(str(e))
    if a.json:
        print(json.dumps(rows, indent=2))
        return
    print('Sampled kernel job wall time (microseconds); not FPS or hardware cycles')
    print('job size total gpu non_gpu heap_head heap_tail heap_used')
    for r in rows:
        print(r['job'], f"{r['width']}x{r['height']}",
              *(r.get(k, '-') for k in ('total', 'gpu', 'non_gpu', 'heap_head', 'heap_tail', 'heap_used')))
    print(f'{len(rows)} complete successful samples')


if __name__ == '__main__':
    main()
