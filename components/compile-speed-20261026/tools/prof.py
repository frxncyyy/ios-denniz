#!/usr/bin/env python3
"""Symbolize speedshim/malibench prof.txt and print per-tag self/inclusive tables.

    prof.py prof.txt [--top N] [--tag TAG] [--incl REGEX]...
"""
import argparse, collections, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument('prof')
ap.add_argument('--top', type=int, default=30)
ap.add_argument('--tag', action='append', default=[])
ap.add_argument('--group', action='store_true', help='merge all tags')
ap.add_argument('--incl', action='append', default=[], help='regex buckets (inclusive) to report per tag')
ap.add_argument('--dso', action='store_true', help='also report leaf DSO split')
a = ap.parse_args()

maps, samples = [], []
for l in open(a.prof):
    if l.startswith('MAP '):
        p = l[4:].split()
        if len(p) >= 6 and 'x' in p[1]:
            lo, hi = (int(x, 16) for x in p[0].split('-'))
            maps.append((lo, hi, int(p[2], 16), p[5]))
    elif l.startswith('S '):
        p = l.split()
        samples.append((p[1], [int(x, 16) for x in p[2:]]))


def loc(pc):
    for lo, hi, off, path in maps:
        if lo <= pc < hi:
            return path, pc - lo + off
    return None, pc


segcache = {}


def segs(path):
    if path not in segcache:
        out = subprocess.run(['readelf', '-lW', path], capture_output=True, text=True).stdout
        r = []
        for l in out.splitlines():
            p = l.split()
            if p and p[0] == 'LOAD':
                r.append((int(p[1], 16), int(p[2], 16), int(p[4], 16)))
        segcache[path] = r
    return segcache[path]


def tova(path, o):
    for off, va, fs in segs(path):
        if off <= o < off + fs:
            return o - off + va
    return o


req = collections.defaultdict(set)
# skip frames: [0]=handler, [1]=signal trampoline; leaf is the interrupted pc (no -1)
for _, st in samples:
    for i, pc in enumerate(st[2:]):
        path, o = loc(pc if i == 0 else pc - 1)
        if path:
            req[path].add(o)
names = {}
for path, offs in req.items():
    offs = sorted(offs)
    out = subprocess.run(['llvm-symbolizer', '--obj', path, '--functions=short', '--demangle'],
                         input='\n'.join(hex(tova(path, o)) for o in offs), capture_output=True, text=True).stdout.split('\n\n')
    for o, blk in zip(offs, out):
        lines = blk.strip().split('\n')
        # with inlines: function lines are at even indices, innermost first
        fns = [lines[k] for k in range(0, len(lines), 2)] if lines and lines[0] else ['?']
        names[(path, o)] = [re.sub(r'::h[0-9a-f]{16}$', '', f)[:110] for f in fns]


def frames(st):
    r, dsos = [], []
    for i, pc in enumerate(st[2:]):
        path, o = loc(pc if i == 0 else pc - 1)
        fns = names.get((path, o), ['?']) if path else ['?']
        r.extend(fns)
        dsos.extend([path.rsplit('/', 1)[-1] if path else '?'] * len(fns))
    return r, dsos


bytag = collections.defaultdict(list)
for tag, st in samples:
    bytag['ALL' if a.group else tag].append(frames(st))
total = sum(len(v) for v in bytag.values())
print(f'samples={total} (1 ms CPU each)')
for tag in sorted(bytag, key=lambda t: -len(bytag[t])):
    if a.tag and tag not in a.tag:
        continue
    fl = bytag[tag]
    n = len(fl)
    print(f'\n=== {tag}: {n} samples ({100*n/total:.1f}%)')
    self_c = collections.Counter(f[0][0] if f[0] else '?' for f in fl)
    incl = collections.Counter()
    for f, _ in fl:
        for x in set(f):
            incl[x] += 1
    if a.dso:
        d = collections.Counter(ds[0] if ds else '?' for _, ds in fl)
        print('  leaf DSO: ' + ', '.join(f'{k} {100*v/n:.1f}%' for k, v in d.most_common(6)))
    for rx in a.incl:
        r = re.compile(rx)
        c = sum(1 for f, _ in fl if any(r.search(x) for x in f))
        print(f'  incl /{rx}/: {c} ({100*c/n:.1f}%)')
    print('  -- self')
    for k, v in self_c.most_common(a.top):
        print(f'  {100*v/n:5.1f}% {v:6d}  {k}')
    print('  -- inclusive')
    for k, v in incl.most_common(a.top):
        print(f'  {100*v/n:5.1f}% {v:6d}  {k}')

import os
for fn in os.environ.get('CALLERS', '').split(','):
    if not fn:
        continue
    chains = collections.Counter()
    for tag in bytag:
        if a.tag and tag not in a.tag:
            continue
        for f, _ in bytag[tag]:
            hit = [j for j, x in enumerate(f) if x == fn or x.startswith(fn + '<')]
            if hit:
                i = hit[0]
                chains[' < '.join(x[:60] for x in f[i:i + int(os.environ.get('CDEPTH', '6'))] if not x.startswith(('catch_unwind', 'do_call', 'call_once', 'guarded', 'with_request')))] += 1
    print(f'\n--- callers of {fn}')
    for k, v in chains.most_common(12):
        print(f'  {v:6d}  {k}')
