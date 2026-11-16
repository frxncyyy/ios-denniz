#!/usr/bin/env python3
"""Instruction/cycle table from an air_vs_m2v run: per shader (FS, VS position) m2v vs AIR."""
import json, sys, statistics, collections
run = sys.argv[1]
rows = [json.loads(l) for l in open(f'runs/{run}/results.jsonl')]
def st(r, mode, kind):
    for s in r.get(mode, {}).get('stats', []):
        if s['_line'].startswith('MESA_SHADER_' + kind):
            return s
    return None
def bound(s):
    return max(('fma', 'cvt', 'sfu', 'v', 't', 'ls'), key=lambda k: s.get(k, 0))
groups = collections.defaultdict(list)
for r in rows:
    if r.get('m2v', {}).get('rc') or r.get('air', {}).get('rc'):
        continue
    vs, fs = r['name'].split(' + ')
    for kind, fn in (('FRAGMENT', fs), ('POSITION', vs)):
        a, m = st(r, 'air', kind), st(r, 'm2v', kind)
        if a and m:
            groups[(kind, fn)].append((m, a))
print('| stage | function | records | m2v instrs | AIR instrs | m2v cycles (bound) | AIR cycles (bound) | cycles ratio | m2v fma/cvt/sfu | AIR fma/cvt/sfu |')
print('|---|---|---|---|---|---|---|---|---|---|')
tot = collections.Counter()
for (kind, fn), v in sorted(groups.items(), key=lambda x: (x[0][0] != 'FRAGMENT', -len(x[1]), x[0][1])):
    n = len(v)
    mi = sum(m['instrs'] for m, a in v) / n; ai = sum(a['instrs'] for m, a in v) / n
    mc = sum(m['cycles'] for m, a in v) / n; ac = sum(a['cycles'] for m, a in v) / n
    mb = collections.Counter(bound(m) for m, a in v).most_common(1)[0][0]; ab = collections.Counter(bound(a) for m, a in v).most_common(1)[0][0]
    alu = lambda s: sum(s.get(k, 0) for k in ('fma', 'cvt', 'sfu'))
    mf = '/'.join('%.2f' % (sum(m[k] for m, a in v) / n) for k in ('fma', 'cvt', 'sfu'))
    af = '/'.join('%.2f' % (sum(a[k] for m, a in v) / n) for k in ('fma', 'cvt', 'sfu'))
    print(f"| {'FS' if kind=='FRAGMENT' else 'VS'} | {fn} | {n} | {mi:.0f} | {ai:.0f} | {mc:.2f} ({mb}) | {ac:.2f} ({ab}) | {mc/ac if ac else 0:.2f}x | {mf} | {af} |")
    tot[kind + 'm'] += mc * n; tot[kind + 'a'] += ac * n; tot[kind + 'mi'] += mi * n; tot[kind + 'ai'] += ai * n
for k in ('FRAGMENT', 'POSITION'):
    print(f"total {k}: instrs m2v {tot[k+'mi']:.0f} AIR {tot[k+'ai']:.0f} ({tot[k+'mi']/max(1,tot[k+'ai']):.2f}x); cycles m2v {tot[k+'m']:.1f} AIR {tot[k+'a']:.1f} ({tot[k+'m']/max(1,tot[k+'a']):.2f}x)")
# fixed_frag distribution
v = groups.get(('FRAGMENT', 'fixed_frag_lph_cph'), [])
if v:
    ratios = sorted((m['cycles'] / a['cycles'] if a['cycles'] else 1) for m, a in v)
    ir = sorted(m['instrs'] / a['instrs'] for m, a in v)
    print(f"fixed_frag cycles ratio min/median/max {ratios[0]:.2f}/{statistics.median(ratios):.2f}/{ratios[-1]:.2f}; instr ratio {ir[0]:.2f}/{statistics.median(ir):.2f}/{ir[-1]:.2f}; AIR worse cycles in {sum(1 for x in ratios if x<1)}")
cpu = [(r['m2v']['cpu_s'], r['air']['cpu_s']) for r in rows if 'cpu_s' in r.get('air', {}) and 'cpu_s' in r.get('m2v', {})]
print(f"compile CPU (Mali entry only, translation excluded): m2v {sum(a for a,b in cpu):.1f}s AIR {sum(b for a,b in cpu):.1f}s over {len(cpu)} records")
