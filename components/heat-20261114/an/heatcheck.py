#!/usr/bin/env python3
"""heat-20261114/an/heatcheck.py KERNEL_USB_LOG: the numbers that say whether the standby heat is gone (host only, reads a log file).
PASS needs: NORD2 CLOCK floor line with 'set to', no 'caught burning CPU', coreduetd AKS sel 17 < 1/s over the idle part,
no core above 30 % averaged over the display-off windows, L cluster back at its idle clock, battery not above its start in idle."""
import re, sys, collections
L = open(sys.argv[1], errors='replace').read().split('\n')
on = None; rows = []; aks = collections.Counter(); ex = collections.Counter(); bat = []; clock = [l for l in L if 'NORD2 CLOCK:' in l]
scp = [l for l in L if 'NORD2 SCP: step 5' in l or 'NORD2 SCP: motion' in l]; burn = [l for l in L if 'caught burning CPU' in l]
cur = {'aks': 0}
for l in L:
    if 'SWEET FB: display on' in l: on = True
    elif 'SWEET FB: display off' in l: on = False
    m = re.search(r'SWEET AKS: (\S+) sel 17', l)
    if m: aks[m.group(1)] += 1
    m = re.match(r'exec (\S+)', l)
    if m and m.group(1) != 'xpcproxy': ex[m.group(1)] += 1
    m = re.search(r'NORD2 SYS: win_ms=(\d+) cpu_busy%=([\d/]+)', l)
    if m: rows.append((on, int(m.group(1)), [int(x) for x in m.group(2).split('/')]))
    m = re.search(r'DVFS DECISION cpuL=(\d+)MHz cpuB=(\d+)MHz.*class=(\w+).*therm=(\d+) bat=([\d.]+)', l)
    if m: bat.append((on, int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4)), float(m.group(5))))
secs = sum(r[1] for r in rows) / 1000 or 1
print('log: %d SYS windows = %.0f s' % (len(rows), secs))
print('clock floor:', clock[0][:230] if clock else 'NO "NORD2 CLOCK:" LINE (kernel without 0530, or the log starts later)')
for l in scp[:3]: print('scp:', l[:160])
print('burning CPU lines:', len(burn), burn[:2])
print('AKS sel 17 per second by process:', ', '.join('%s %.2f' % (k, v / secs) for k, v in aks.most_common(6)))
print('execs per 10 min:', ', '.join('%s %.0f' % (k, v * 600 / secs) for k, v in ex.most_common(8)))
for st, name in ((False, 'display off'), (True, 'display on'), (None, 'before the first display event')):
    x = [r for r in rows if r[0] is st]
    if not x: continue
    t = sum(r[1] for r in x); per = [sum(r[1] * r[2][c] for r in x) / t for c in range(8)]
    b = [v for v in bat if v[0] is st]
    idle = [v for v in b if v[3] == 'idle']
    print('%s: %.0f s, per-core busy %% %s = %.2f cores; L clock in idle class: %s; battery %.1f -> %.1f C (max %.1f), therm level max %d' % (
        name, t / 1000, ' '.join('%.0f' % p for p in per), sum(per) / 100,
        collections.Counter(v[1] for v in idle).most_common(3), b[0][5] if b else 0, b[-1][5] if b else 0, max([v[5] for v in b] or [0]), max([v[4] for v in b] or [0])))
