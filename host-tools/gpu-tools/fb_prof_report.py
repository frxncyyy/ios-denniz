#!/usr/bin/env python3
"""Resolve a frame_bench FB_PROF sample file: self and inclusive share per
function (addr2line on each mapped image). Host-only; usage:
    fb_prof_report.py samples.txt [--top N] [--focus SUBSTR]"""
import argparse, bisect, collections, subprocess, sys
ap = argparse.ArgumentParser()
ap.add_argument('samples'); ap.add_argument('--top', type=int, default=40); ap.add_argument('--focus', default='')
a = ap.parse_args()
maps, stacks = [], []
for line in open(a.samples):
    if line.startswith('map '):
        f = line.split()
        if len(f) >= 7 and 'x' in f[2]:
            lo, hi = (int(x, 16) for x in f[1].split('-'))
            maps.append((lo, hi, int(f[3], 16), f[6]))
    elif line.strip():
        stacks.append([int(x, 16) for x in line.split()])
maps.sort()
starts = [m[0] for m in maps]
def image(addr):
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and maps[i][0] <= addr < maps[i][1]:
        return maps[i][3], addr - maps[i][0] + maps[i][2]
    return None, None
by_img = collections.defaultdict(set)
for st in stacks:
    for k, pc in enumerate(st):
        img, off = image(pc - (1 if k else 0))
        if img: by_img[img].add(off)
names = {}
for img, offs in by_img.items():
    offs = sorted(offs)
    try:
        out = subprocess.run(['addr2line', '-f', '-C', '-e', img] + ['0x%x' % o for o in offs],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout.splitlines()
    except Exception:
        out = []
    for i, o in enumerate(offs):
        fn = out[2 * i] if 2 * i < len(out) else '??'
        names[(img, o)] = fn if fn != '??' else '%s+0x%x' % (img.rsplit('/', 1)[-1], o)
skip = ('fb_prof_handler', '__restore_rt', 'backtrace', '_Unwind', '??')
selfc, incl = collections.Counter(), collections.Counter()
n = 0
for st in stacks:
    fns = []
    for k, pc in enumerate(st):
        img, off = image(pc - (1 if k else 0))
        fns.append(names.get((img, off), '?') if img else '?')
    while fns and (fns[0].startswith(skip) or fns[0] in ('?',) or 'sigaction' in fns[0]):
        fns.pop(0)
    if not fns: continue
    if a.focus and not any(a.focus in f for f in fns): continue
    n += 1
    selfc[fns[0]] += 1
    for f in set(fns): incl[f] += 1
print('samples %d (of %d)' % (n, len(stacks)))
print('--- self ---')
for f, c in selfc.most_common(a.top): print('%6.2f%% %s' % (100.0 * c / max(n, 1), f))
print('--- inclusive ---')
for f, c in incl.most_common(a.top): print('%6.2f%% %s' % (100.0 * c / max(n, 1), f))
