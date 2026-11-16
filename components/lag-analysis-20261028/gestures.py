#!/usr/bin/env python3
"""Touch gestures (NORD2 INPUT finger lines) and the backboardd TEGU_PERF
windows printed after each gesture until the next one.

Panel coordinates are 1080x2400.  Kinds: up-from-bottom (start y>2250: home /
switcher), down-from-top (start y<120: notification centre / control centre),
h-swipe (|dx|>250, |dy|<300: page or switcher-card scroll), v-swipe, tap.
usage: gestures.py LOG PERF_JSON [--json OUT]"""
import argparse
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import lagparse as lp  # noqa: E402

TOUCH = re.compile(r'NORD2 INPUT: finger=(\d+) down=(\d) x=(\d+) y=(\d+)')


def kind(x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0
    if y0 > 2250 and dy < -300:
        return 'up-bottom'
    if y0 < 120 and dy > 300:
        return 'down-top'
    if abs(dx) > 250 and abs(dy) < 300:
        return 'h-swipe'
    if abs(dy) > 300:
        return 'v-swipe'
    return 'tap'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('perf')
    ap.add_argument('--json')
    a = ap.parse_args()
    evs = lp.clock(lp.events(a.log))
    downs = []
    cur = None
    for e in evs:
        m = TOUCH.search(e['text'])
        if not m:
            continue
        down, x, y = int(m.group(2)), int(m.group(3)), int(m.group(4))
        if down:
            cur = {'line': e['line'], 't': e.get('t'), 'x0': x, 'y0': y}
        elif cur:
            cur.update(x1=x, y1=y, up_line=e['line'], kind=kind(cur['x0'], cur['y0'], x, y))
            downs.append(cur)
            cur = None
    perf = json.loads(pathlib.Path(a.perf).read_text())
    for i, g in enumerate(downs):
        end = downs[i + 1]['line'] if i + 1 < len(downs) else 10 ** 9
        ws = [w for w in perf if g['line'] <= w['line'] < end]
        c = sum(w['commands'] for w in ws)
        g['n_win'] = len(ws)
        g['cmds'] = c
        g['secs'] = round(sum(w['window_ms'] for w in ws) / 1000, 1)
        g['cmd_per_s'] = round(c / g['secs'], 1) if g['secs'] else None
        g['present_per_s'] = round(sum(w.get('pass_present', 0) for w in ws) / g['secs'], 1) if g['secs'] else None
        g['kj_per_cmd'] = round(sum(w.get('frame_kernel_jobs', 0) for w in ws) / c, 1) if c else None
        g['exec_per_cmd'] = round(sum(w.get('exec_ms', 0) for w in ws) / c, 1) if c else None
        g['passes_per_cmd'] = round(sum(sum(w.get('pass_att') or [0]) for w in ws) / c, 1) if c else None
        g['fail'] = sum(w['fail'] for w in ws)
        g['pso_ms'] = sum(w['pso_ms'] for w in ws)
    print('line   t      kind       x0   y0   x1   y1  win secs cmd/s pres/s kj/cmd exec/cmd pass/cmd fail pso_ms')
    for g in downs:
        print(f"{g['line']:6d} {g['t'] if g['t'] is not None else -1:6.1f} {g['kind']:9s} {g['x0']:4d} {g['y0']:4d} {g['x1']:4d} {g['y1']:4d}"
              f" {g['n_win']:3d} {g['secs']:5.1f} {g['cmd_per_s'] or 0:5.1f} {g['present_per_s'] or 0:5.1f} {g['kj_per_cmd'] or 0:6.1f}"
              f" {g['exec_per_cmd'] or 0:7.1f} {g['passes_per_cmd'] or 0:7.1f} {g['fail']:4d} {g['pso_ms']:6d}")
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(downs, indent=1))


if __name__ == '__main__':
    main()
