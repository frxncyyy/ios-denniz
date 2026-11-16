#!/usr/bin/env python3
"""Delivery per FB PRESENT window (>=5 s) and per window class.

delivery = fresh presents / (60 * window seconds).  Every kernel present is a
fresh CA swap (the vsync handler presents only when a new swap is pending), so
n counts fresh pictures.  Window length = SpringBoard-clock delta, floored at
5 s (SWEET_FB_STAT_INTERVAL_US); where the clock is missing it is 5 s, which
makes fps an upper bound.  Idle windows (n < 10) are left out.
Class from the window's own backboardd records: flat (kernel jobs/cmd <= 3),
blurfail (render-pass refusal failures, or >= 35 kj/cmd at < 0.45 ms exec per kj),
heavy (the rest).
usage: delivery.py WINDOWS_JSON... [--json OUT]"""
import argparse
import json
import pathlib


def cls(w):
    kj = w.get('kj_per_cmd') or 0
    ex = w.get('exec_per_cmd') or 0
    if w.get('fail_n') or (kj >= 35 and ex / kj < 0.45):
        return 'blurfail'
    if kj <= 3:
        return 'flat'
    return 'heavy'


def pct(v, p):
    v = sorted(v)
    if not v:
        return None
    k = (len(v) - 1) * p
    lo = int(k)
    hi = min(lo + 1, len(v) - 1)
    return round(v[lo] + (v[hi] - v[lo]) * (k - lo), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('windows', nargs='+')
    ap.add_argument('--json')
    a = ap.parse_args()
    out = {}
    for p in a.windows:
        ws = [w for w in json.loads(pathlib.Path(p).read_text()) if w['n'] >= 10 and w.get('cmds')]
        res = {}
        for c in ('flat', 'heavy', 'blurfail', 'all'):
            sel = [w for w in ws if c == 'all' or cls(w) == c]
            if not sel:
                continue
            secs = sum(w['win_s'] for w in sel)
            n = sum(w['n'] for w in sel)
            res[c] = {
                'windows': len(sel), 'seconds': round(secs, 1), 'presents': n,
                'fps': round(n / secs, 1), 'delivery': round(n / secs / 60, 3),
                'fps_p10': pct([w['fps'] for w in sel], .1), 'fps_p50': pct([w['fps'] for w in sel], .5),
                'fps_p90': pct([w['fps'] for w in sel], .9),
                'dropped_per_s': round(sum(w.get('dropped') or 0 for w in sel) / secs, 2),
                'copy_us_avg': round(sum(w['copy_avg'] * w['n'] for w in sel) / n),
                'copy_us_max': max(w['copy_max'] or 0 for w in sel),
                'xfer_us_avg': round(sum(w['xfer_avg'] * w['n'] for w in sel) / n),
                'late_ticks': sum(w['late_ticks'] for w in sel),
                'busy_skips': sum(w['busy_skips'] for w in sel),
                'clock_windows': sum(1 for w in sel if w.get('dt')),
            }
        out[p] = res
        print('==', p)
        keys = list(res['all'].keys())
        print(f"{'':16s}" + ''.join(f"{c:>10s}" for c in res))
        for k in keys:
            print(f"{k:16s}" + ''.join(f"{str(res[c].get(k)):>10s}" for c in res))
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(out, indent=1))


if __name__ == '__main__':
    main()
