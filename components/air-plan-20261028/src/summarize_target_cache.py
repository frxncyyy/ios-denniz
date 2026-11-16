#!/usr/bin/env python3
"""Summarize retained-target cache counters, not GPU latency or frame rate.

Counters cover attempted eligible jobs after cache creation. Each printed row
covers 1024 attempts; startup, unreported tail and the 32-row logging cap limit
coverage. target_writes is cumulative, whereas hit/miss/evict are per window.
Use one uninterrupted USB segment.
"""
import argparse
import json
from pathlib import Path
import re

PATTERN = re.compile(r'TEGU GPU TCACHE: window (\d+) jobs (\d+) hit (\d+) miss (\d+) evict (\d+) target_writes (\d+)\b')


def summarize(text):
    if 'STREAM-END' in text:
        raise ValueError('Select one USB segment')
    rows = []
    seen = set()
    malformed = 0
    last = 0
    writes = 0
    gaps = []
    for line in text.splitlines():
        if 'TEGU GPU TCACHE:' not in line:
            continue
        match = PATTERN.search(line)
        if not match:
            malformed += 1
            continue
        row = dict(zip(('window', 'jobs', 'hit', 'miss', 'evict', 'target_writes'), map(int, match.groups())))
        number = row['window']
        if number in seen or number <= last:
            raise ValueError('Duplicate or reordered cache window')
        if not row['jobs'] or row['hit'] + row['miss'] != row['jobs']:
            raise ValueError('Inconsistent hit/miss accounting')
        if row['target_writes'] < writes:
            raise ValueError('Cumulative target writes decreased')
        if number != last + 1:
            gaps.append([last + 1, number - 1])
        rows.append(row)
        seen.add(number)
        last, writes = number, row['target_writes']
    if not rows:
        raise ValueError('No complete target-cache counter rows')
    totals = {k: sum(r[k] for r in rows) for k in ('jobs', 'hit', 'miss', 'evict')}
    return dict(metric='retained target mappings; logged eligible attempts only',
                windows=len(rows), missing_window_ranges=gaps, malformed=malformed,
                **totals, hit_fraction=totals['hit'] / totals['jobs'],
                cumulative_target_writes_at_last_report=writes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(summarize(args.log.read_text(errors='replace')), indent=2))
    except ValueError as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()
