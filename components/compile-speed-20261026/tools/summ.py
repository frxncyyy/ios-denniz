#!/usr/bin/env python3
"""summ.py run... : per-stage CPU totals; cache-entry and Mali-output equivalence vs the first run."""
import sys, collections, hashlib, pathlib, re
W = pathlib.Path(__file__).resolve().parents[1] / 'runs'
def stages(r):
    c = collections.defaultdict(lambda: [0, 0.0]); outs = []
    for l in open(W / r / 'stages.tsv'):
        f = l.rstrip('\n').split('\t'); c[f[0]][0] += 1; c[f[0]][1] += float(f[1])
        if f[0] == 'mali_options': outs.append(re.search(r'out=(\w+)', f[4]).group(1))
    return c, outs
def entries(r):
    d = {}
    for p in sorted((W / r / 'cache').rglob('*.tpc')):
        d[str(p.relative_to(W / r / 'cache'))] = hashlib.sha256(p.read_bytes()).hexdigest()
    return d
base = sys.argv[1]; bc, bo = stages(base); be = entries(base)
for r in sys.argv[1:]:
    c, o = stages(r); e = entries(r)
    tot = sum(v[1] for v in c.values())
    print(f'== {r}: total {tot:.0f} ms; ' + '; '.join(f'{k} {v[1]/v[0]:.2f}x{v[0]}' for k, v in sorted(c.items(), key=lambda x: -x[1][1]) if v[1] > 1))
    if r != base:
        same = sum(1 for k in be if e.get(k) == be[k]); 
        print(f'   cache entries identical {same}/{len(be)} (files {len(e)}); mali outputs identical {sum(1 for a,b in zip(bo,o) if a==b)}/{len(bo)}')
