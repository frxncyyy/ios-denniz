#!/usr/bin/env python3
"""Frame, job and cache metrics from a Nord2 USB console log (NORD2 FB SEEN / GPU JOBS lines).
Usage: python3 -I an_log.py LOG [LOG ...]"""
import re, sys
for path in sys.argv[1:]:
    L = open(path, 'rb').read().decode('latin1').splitlines()
    fb, jb = [], []
    for l in L:
        m = re.search(r'FB SEEN: presents=(\d+) different=(\d+) same=(\d+) gaps_ms<=20/34/67/100/more=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)', l)
        if m: fb.append(list(map(int, m.groups())))
        m = re.search(r'GPU JOBS: n=(\d+) fault=(\d+) full=(\d+) win_ms=(\d+) q_us=(\d+)/(\d+) run_us=(\d+)/(\d+) tot_us=(\d+)/(\d+) clean_us=(\d+)/(\d+) inv_us=(\d+)/(\d+) clean_KiB=(\d+)', l)
        if m: jb.append(list(map(int, m.groups())))
    fb = [r for r in fb if r[0] > 5]
    jb = [r for r in jb if r[0] > 300]
    n = max(1, len(fb)); nj = max(1, len(jb))
    tot = sum(r[3] + r[4] + r[5] + r[6] + r[7] for r in fb) or 1
    print('%s: fb windows %d, distinct/win %.0f, gap share <=20/34/67/100/>100 ms: %s | busy job windows %d, jobs/win %.0f, faults %d, q_us %.0f, run_us %.0f, clean_us %.0f, cleanGiB/win %.1f' % (
        '/'.join(path.split('/')[-3:-1]), len(fb), sum(r[1] for r in fb) / n,
        '/'.join('%.0f%%' % (100 * sum(r[i] for r in fb) / tot) for i in range(3, 8)),
        len(jb), sum(r[0] for r in jb) / nj, sum(r[1] for r in jb), sum(r[4] for r in jb) / nj, sum(r[6] for r in jb) / nj, sum(r[10] for r in jb) / nj,
        sum(r[14] for r in jb) / nj / 1048576))
