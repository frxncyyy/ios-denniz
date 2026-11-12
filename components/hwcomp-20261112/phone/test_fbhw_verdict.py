#!/usr/bin/env python3
"""host test of fbhw_verdict.py on synthetic logs (cycle with fallback/underflow, interleaving, steps mapping, HW-mode segmentation)."""
import json, os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbhw_verdict as V
fails = 0
def check(c, m):
    global fails
    print(('ok   ' if c else 'FAIL ') + m); fails += not c

def sync(p, tear, race=0):
    return ('NORD2 FB SYNC: pushes=%d safe=%d tear_none_te=%d tear_ambig=0 te_margin_min_us=900 te_margin_avg_us=1400 src_race=%d busy_after=0 '
            'copy_mismatch=0 checked=1 empty=0 black_rows=0 copy_gt4=0 copy_gt8=0 copy_max_us=2000 total_gt16=0 total_max_us=0 swait_level=1 '
            'not_ready=0 valve=0 dropped=0\n' % (p, p, tear, race))
def hw(mode, hwf, cpuf, race=0, und=0):
    return 'NORD2 FB HW: mode=%d hw_frames=%d cpu_frames=%d fallback=0 hw_race=%d ovl_und=%d smi_und=0 eof_abn=0 dsi_und=0 fd_timeout=0 readback_bad=0 ref_busy=3\n' % (mode, hwf, cpuf, race, und)
def present(n): return 'NORD2 FB PRESENT: n=%d avg_us=12000 max_us=14000 copy_us=%d/3000 xfer_us=9000/9100 legacy=0 timeouts=0 busy_skips=0 busy_forced=0 late_ticks=0 rate_hz=60\n' % (n, 2000 if n else 0)
def ovl(bits): return 'NORD2 FB OVL: frames=100 dsi_underrun=0 ovl0[%s] ovl1[none]\n' % bits

L = []
L += ['boot noise\n', present(50), ovl('b0=50 b1=50'), sync(50, 1), hw(0, 0, 50)]
L += ['N2FBHWCTL start modes=4 period=120 delay=90 pool=-1 rounds=1 final=0\n', 'N2FBHWCTL boot mode=0 t=90\n']
L += ['N2FBHWCTL phase 1/4 mode=0 set rc=0 readback=0 t=90\n', present(1000), ovl('b0=1000 b1=1000'), sync(1050, 11), hw(0, 0, 1050)]
L += ['N2FBHWCTL phase_end 1/4 mode=0 readback=0 t=210\n']
L += ['N2FBHWCTL phase 1/4 mode=1 set rc=0 readback=1 t=210\n'.replace('1/4 mode=1', '2/4 mode=1')]
# interleaved: a SYNC record cut by another record (keys after the cut are missing, the merge keeps them)
L += [present(1000), ovl('b0=1000'), 'NORD2 FB SYNC: pushes=2050 safe=2050 tear_none_te=19 tear_aNORD2 FB SWAPWAIT: level=1 calls=3\n', hw(1, 900, 1150)]
L += ['N2FBHWCTL phase_end 2/4 mode=1 readback=1 t=330\n', 'N2FBHWCTL phase 3/4 mode=2 set rc=0 readback=2 t=330\n']
L += [present(1000), ovl('b0=1000 b2=3 b10=1'), sync(3050, 30), hw(2, 1700, 1350, und=3)]
L += ['N2FBHWCTL phase_end 3/4 mode=2 readback=2 t=450\n', 'N2FBHWCTL phase 4/4 mode=3 set rc=0 readback=3 t=450\n']
L += [present(1000), sync(4050, 60, race=2), hw(3, 1900, 2150, race=4), 'NORD2 FB HW FALLBACK: reason=hw_race mode 3 -> 0\n']
L += ['N2FBHWCTL phase_end 4/4 mode=3 readback=0 t=570 FALLBACK\n', 'N2FBHWCTL done final=0 rc=0 readback=0 t=570\n', present(10), sync(4060, 60, race=2)]
text = ''.join(L)
segs = V.parse(text)
rows = V.judge([V.row(s) for s in segs], 1.5)
by = {(r['label'], r['mode']): r for r in rows}
print(V.table(rows))
check([r['mode'] for r in rows if r['label'] not in ('pre', 'final') and not r['label'].startswith('after')] == [0, 1, 2, 3], 'four phases in order 0,1,2,3')
p0, p1, p2, p3 = (by[('%d/4' % (i + 1), i)] for i in range(4))
check(by[('pre', 0)]['pushes'] == 50, 'pre segment: boot mode from the ctl boot line, pushes 50')
check(p0['pushes'] == 1000 and p0['tear_none_te'] == 10 and p0['verdict'] == 'PASS', 'mode 0: delta of the cumulative SYNC, PASS')
check(p1['pushes'] == 1000 and p1['tear_none_te'] == 8 and p1['src_race'] == 0, 'mode 1: truncated SYNC record merged (src_race kept from before)')
check(p1['hw']['hw_frames'] == 900 and p1['hw']['cpu_frames'] == 100 and p1['verdict'] == 'PASS', 'mode 1: HW deltas, PASS')
check(p2['fme_und'] == 3 and p2['smi_und'] == 1 and p2['verdict'] == 'FAIL', 'mode 2: OVL b2/b10 underflow -> FAIL')
check(any('ovl_und=3' in w for w in p2['why']), 'mode 2: HW ovl_und reported')
check(p3['verdict'] == 'FAIL' and len(p3['fallback']) == 2, 'mode 3: kernel FALLBACK line + ctl readback FALLBACK -> FAIL')
check(any('hw_race=4' in w for w in p3['why']) and any('src_race=2' in w for w in p3['why']), 'mode 3: hw_race and src_race listed')
check(any('tear' in w for w in p3['why']), 'mode 3: tear rate above mode 0 flagged')
check(by[('final', 0)]['pushes'] == 10, 'final segment after done')
# steps mapping
d = tempfile.mkdtemp(); lp = os.path.join(d, 'k.log'); sp = os.path.join(d, 'steps.jsonl')
open(lp, 'w').write(text)
o2 = text.index('N2FBHWCTL phase 2/4') + 5; o3 = text.index('N2FBHWCTL phase 3/4') + 5
with open(sp, 'w') as f:
    for off in (o2, o2 + 10, o3, 0):
        f.write(json.dumps(dict(kind='interaction', log_offset=off)) + '\n')
    f.write(json.dumps(dict(kind='logs', log_offset=o3)) + '\n')
jp = os.path.join(d, 'v.json')
rc = V.main([lp, '--steps', sp, '--json', jp])
J = {(r['label'], r['mode']): r for r in json.load(open(jp))}
check(rc == 1, 'main exit 1 with a FAIL')
check(J[('2/4', 1)]['interactions'] == 2 and J[('3/4', 2)]['interactions'] == 1 and J[('pre', 0)]['interactions'] == 1, 'steps.jsonl interactions mapped to phases')
# no ctl: segmentation by the HW line's mode
t2 = hw(0, 0, 10) + present(10) + sync(10, 0) + hw(2, 8, 12) + present(10) + sync(20, 0) + ovl('b0=10')
r2 = V.judge([V.row(s) for s in V.parse(t2)], 1.5)
check([r['mode'] for r in r2] == [0, 2] and r2[1]['hw']['hw_frames'] == 8 and r2[1]['verdict'] == 'PASS', 'no ctl: segments by HW mode=')
r3 = V.judge([V.row(s) for s in V.parse(present(10) + sync(10, 0) + hw(1, 0, 10), 1)], 1.5)
t4 = hw(0, 0, 10) + present(10) + 'NORD2 FB HW MODE: old=0 new=1 why=sysctl\n' + present(10) + hw(1, 9, 11) + 'NORD2 FB HW MODE: old=1 new=0 why=fallback hw_race\n' + present(5)
r4 = V.judge([V.row(s) for s in V.parse(t4)], 1.5)
check([(r['mode'], r['presents']) for r in r4] == [(0, 10), (1, 10), (0, 5)] and r4[1]['verdict'] == 'PASS' and r4[2]['verdict'] == 'FAIL', 'HW MODE lines split segments; why=fallback -> FAIL')
check(r3[0]['verdict'] == 'FAIL' and 'hw_frames=0' in r3[0]['why'][0], 'mode 1 with hw_frames 0 -> FAIL')
open(lp, 'w').write('nothing\n'); check(V.main([lp]) == 2, 'no data -> exit 2')
print('ALL OK' if not fails else 'FAILED %d' % fails)
sys.exit(1 if fails else 0)
