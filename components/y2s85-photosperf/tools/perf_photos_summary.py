#!/usr/bin/env python3
"""Summary of the y2s85 'NORD2 PERF PHOTOS A/B/C' lines of a kernel console log, per process and opt (1 = photosperf on, 0 = no-photosperf).
   python3 -I perf_photos_summary.py LOG [OFFSET_FROM [OFFSET_TO]]   (byte offsets, e.g. from fbhw_scenario steps.jsonl log_offset)"""
import re, sys, collections
d = open(sys.argv[1], 'rb').read().decode('latin-1')
lo = int(sys.argv[2]) if len(sys.argv) > 2 else 0
hi = int(sys.argv[3]) if len(sys.argv) > 3 else len(d)
d = d[lo:hi]
A = re.compile(r'NORD2 PERF PHOTOS A (\S+) opt=(\d) fps=([\d.]+) fr=(\d+) cb=(\d+) lat=([\d.]+)/([\d.]+)ms commit=([\d.]+)ms drvcpu=([\d.]+)ms/s')
Bl = re.compile(r'NORD2 PERF PHOTOS B (\S+) conv=(\d+) odd=(\d+) cpu=(\d+) same=(\d+) frz=(\d+) c/fr=([\d.]+) rel=(\d+)')
C = re.compile(r'NORD2 PERF PHOTOS C (\S+) iostex=(\d+)/([\d.]+)ms\(max ([\d.]+)\) buf=(\d+)/([\d.]+)ms tex=(\d+)/([\d.]+)ms comp=(\d+)/([\d.]+)ms disp=([\d.]+)ms skip=(\d+)')
acc = collections.defaultdict(lambda: collections.defaultdict(float))
last_opt = {}
for m in A.finditer(d):
    p, opt = m[1], m[2]; last_opt[p] = opt; a = acc[(p, opt)]
    a['windows'] += 1; a['frames'] += int(m[4]); a['cbs'] += int(m[5]); a['lat_ms_sum'] += float(m[6]) * int(m[4])
    a['lat_max'] = max(a['lat_max'], float(m[7])); a['commit_ms_sum'] += float(m[8]) * int(m[5]); a['drvcpu_ms_s_sum'] += float(m[9])
for m in Bl.finditer(d):
    a = acc[(m[1], last_opt.get(m[1], '?'))]
    for k, i in (('conv', 2), ('odd', 3), ('cpu', 4), ('same', 5), ('frz', 6), ('rel', 8)): a[k] += int(m[i])
for m in C.finditer(d):
    a = acc[(m[1], last_opt.get(m[1], '?'))]
    a['iostex'] += int(m[2]); a['iostex_ms'] += float(m[3]); a['iostex_max'] = max(a['iostex_max'], float(m[4]))
    a['buf'] += int(m[5]); a['buf_ms'] += float(m[6]); a['tex'] += int(m[7]); a['tex_ms'] += float(m[8])
    a['comp'] += int(m[9]); a['comp_ms'] += float(m[10]); a['disp_ms'] += float(m[11]); a['skip'] += int(m[12])
if not acc:
    print('no NORD2 PERF PHOTOS lines in range'); sys.exit(1)
for (p, opt), a in sorted(acc.items()):
    w = max(a['windows'], 1); s = 5.0 * w; fr = max(a['frames'], 1)
    print('%-12s opt=%s windows=%d (%.0f s): fps=%.1f cb/s=%.1f lat=%.2fms(max %.1f) commit=%.2fms drvcpu=%.1fms/s | conv/fr=%.2f (gpu %d odd %d cpu %d) same=%d frz=%d rel=%d | iostex=%d avg %.2fms max %.1f | buf %.1f/s tex %.1f/s alloc %.1fms/s | comp %d avg %.2fms disp %.1fms/s | probes skipped %d'
          % (p, opt, a['windows'], s, a['frames'] / s, a['cbs'] / s, a['lat_ms_sum'] / fr, a['lat_max'], a['commit_ms_sum'] / max(a['cbs'], 1),
             a['drvcpu_ms_s_sum'] / w, (a['conv'] + a['odd'] + a['cpu']) / fr, a['conv'], a['odd'], a['cpu'], a['same'], a['frz'], a['rel'],
             a['iostex'], a['iostex_ms'] / max(a['iostex'], 1), a['iostex_max'], a['buf'] / s, a['tex'] / s, (a['buf_ms'] + a['tex_ms']) / s,
             a['comp'], a['comp_ms'] / max(a['comp'], 1), a['disp_ms'] / s, a['skip']))
