#!/usr/bin/env python3
"""Count completed framebuffer present() calls, not unique frames or panel FPS.

The kernel counter includes the whole present() call, including any early return.
Re-presented content can count again. Idle time is included in each window.
Use a single uninterrupted USB capture; reconnect replay cannot be deduplicated.
"""
import argparse
import json
import re
from pathlib import Path

PATTERN = re.compile(r'TEGU_PRESENT_FRAME window_ms=(\d+) copies=(\d+) mean_copy_us=(\d+) max_copy_us=(\d+) gate=([01])\b')


def summarize(text, replay=False):
    if 'STREAM-END' in text:
        raise ValueError('Select one uninterrupted USB segment before summarizing')
    if replay:
        begin, end = 'TEGU GESTURE: begin', 'TEGU GESTURE: completed'
        if text.count(begin) != 1 or text.count(end) != 1:
            raise ValueError('Expected exactly one complete gesture replay')
        start, stop = text.index(begin), text.index(end)
        if stop <= start:
            raise ValueError('Replay completion precedes start')
        text = text[start:stop]
    rows = [dict(zip(('window_ms', 'calls', 'mean_present_us', 'max_present_us', 'gate'),
                     map(int, m.groups()))) for m in PATTERN.finditer(text)]
    # First reported window crosses the replay start; its individual calls cannot
    # be assigned to the replay. The final unreported partial window is absent.
    dropped = bool(replay and rows)
    if dropped:
        rows = rows[1:]
    if not rows or any(r['window_ms'] <= 0 or r['calls'] <= 0 for r in rows):
        raise ValueError('No valid complete presentation windows')
    elapsed = sum(r['window_ms'] for r in rows)
    calls = sum(r['calls'] for r in rows)
    return dict(metric='completed present() calls; NOT unique-frame or panel FPS',
                windows=len(rows), calls=calls, covered_ms=elapsed,
                calls_per_second=1000 * calls / elapsed,
                mean_present_us=sum(r['mean_present_us'] * r['calls'] for r in rows) / calls,
                max_present_us=max(r['max_present_us'] for r in rows),
                replay_boundary_window_excluded=dropped,
                gate_values=sorted({r['gate'] for r in rows}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--replay', action='store_true')
    args = parser.parse_args()
    try:
        result = summarize(args.log.read_text(errors='replace'), args.replay)
    except ValueError as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
