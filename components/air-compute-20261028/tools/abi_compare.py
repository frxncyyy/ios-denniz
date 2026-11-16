#!/usr/bin/env python3
"""ABI conformance of AIR-direct vs m2v results of the same record: generic plan bindings
(binding, kind, stage mask), framebuffer-read mode, MRT/RT mask, pair flags, vertex attribute mask."""
import json, struct, sys, collections
run = sys.argv[1]
rows = [json.loads(l) for l in open(f'runs/{run}/results.jsonl')]
def plan(h):
    b = bytes.fromhex(h)
    ver, count, rf, res = struct.unpack_from('<4I', b, 0)
    binds = set()
    for i in range(count):
        binding, kind, stage, declared, ia = struct.unpack_from('<5I', b, 16 + 32 * i)
        binds.add((binding, kind, stage, ia if kind == 6 else 0))
    return rf, res, binds
def pair(h):
    b = bytes.fromhex(h)
    w = struct.unpack_from('<10I', b, 0)
    pre = struct.unpack_from('<Q', b, 40)[0]
    w2 = struct.unpack_from('<5I', b, 48)
    return {'flags': w[1], 'attr_mask': w2[0], 'fs_ld_var': w2[3]}
c = collections.Counter(); diffs = collections.Counter(); ex = {}
for r in rows:
    if r['m2v']['rc'] or r['air']['rc']:
        continue
    mrf, mres, mb = plan(r['m2v']['plan']); arf, ares, ab = plan(r['air']['plan'])
    mp, ap = pair(r['m2v']['pair']), pair(r['air']['pair'])
    keys = []
    if mrf != arf: keys.append(f'reads_framebuffer m2v {mrf} air {arf}')
    if mres != ares: keys.append(f'plan.reserved m2v {mres:#x} air {ares:#x}')
    if ab - mb: keys.append('AIR binding not in m2v plan')
    if mb - ab: keys.append('m2v binding unused by AIR')
    for k in ('flags', 'attr_mask', 'fs_ld_var'):
        if mp[k] != ap[k]: keys.append(f'pair.{k} m2v {mp[k]:#x} air {ap[k]:#x}')
    c['identical' if not keys else 'differs'] += 1
    for k in keys:
        diffs[k] += 1; ex.setdefault(k, r['id'] + ' ' + r['name'])
print(dict(c))
for k, v in diffs.most_common(): print(v, k, '| e.g.', ex[k])
