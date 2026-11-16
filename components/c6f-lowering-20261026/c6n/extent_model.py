#!/usr/bin/env python3
"""Ledger #7 host estimate: frame_model pixel model per scenario, the same census
jobs with the fragment-job tile count replaced by the bounded count (n2mb_extent=1).
Everything else (pre-frame mode, covered tiles, draws, waits) is held equal."""
import json, sys, pathlib
G = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(G / 'tools'))
import frame_model as fm
F = G / 'build/c6f-lowering-20261026/fsjobs'
B = json.load(open(F / 'census-current-pf1.json')); A = json.load(open(F / 'census-extent-pf1.json'))
stats = dict(B.get('shader_stats') or {}, **(A.get('shader_stats') or {}))
before_all = [j for js in B['scenarios'].values() for j in js]
full_of = {}
orig = fm._is_full
fm._is_full = lambda j: j.get('full0', j['tiles'] >= fm.PIXEL_CAL['fs_tiles'])
prms = [fm.pixel_calibrate(l, stats, before_all, tile_fetch=B.get('tile_fetch', False))[0] for l in range(3)]
wait = fm.SCEN_DEFAULTS['wait_floor_ms']
print('%-11s %5s | %-26s | %-26s | %s' % ('scenario', 'jobs', 'GPU ms @350 full->bounded', 'GPU ms @654 full->bounded', 'tiles'))
for scn, jb in B['scenarios'].items():
    ja = A['scenarios'][scn]
    after = []
    for b, a in zip(jb, ja):
        j = dict(b); j['full0'] = b['tiles'] >= fm.PIXEL_CAL['fs_tiles']; j['tiles'] = min(b['tiles'], a['ftiles'])
        after.append(j)
    before = [dict(b, full0=b['tiles'] >= fm.PIXEL_CAL['fs_tiles']) for b in jb]
    cells = []
    for clk in (350.0, 654.0):
        r = [(fm.pixel_frame_ms(before, stats, p, clk, wait, True, True), fm.pixel_frame_ms(after, stats, p, clk, wait, True, True)) for p in prms]
        cells.append('%5.2f -> %5.2f [%4.2f..%4.2f]' % (r[1][0], r[1][1], min(x[0] - x[1] for x in r), max(x[0] - x[1] for x in r)))
    print('%-11s %5d | %-26s | %-26s | %d -> %d' % (scn, len(jb), cells[0], cells[1], sum(b['tiles'] for b in jb), sum(a['tiles'] for a in after)))
print('cells: central before -> after, [min..max saving over optimistic/central/pessimistic]; one async wait per frame.')
