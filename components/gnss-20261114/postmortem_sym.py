#!/usr/bin/env python3
"""postmortem_sym.py POSTMORTEM_TXT TRACE_TXT KERNEL_DEVELOPMENT: map every kernel-looking hex value (0xfffffe00........) in the
rescue's last-PC/last-bus text to the nearest iOS kernel symbol. slide = runtime address of n2gt_init (trace 'kaddr' line, 0591) minus
its nm address. Prints 'value -> symbol+off' lines, plus the per-CPU 'c<N>' of the last trace lines."""
import re, subprocess, sys
txt, trace, kern = sys.argv[1:4]
t = open(trace, errors='replace').read() if trace else ''
m = re.search(r'kaddr (.*?) c\d+\s*$', t, re.M)
syms = []
for line in subprocess.run(['llvm-nm', '-n', kern], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in 'tTsSdDbB': syms.append((int(p[0], 16), p[2]))
nm = {n: a for a, n in syms}
slide = None
if m:
    toks = m.group(1).split()
    for n, v in zip(toks[0::2], toks[1::2]):
        if '_' + n in nm: slide = int(v, 16) - nm['_' + n]; print('slide %#x (from %s)' % (slide, n)); break
if slide is None: print('no usable kaddr line (kernel without 0591?): raw values only')
import bisect
addrs = [a for a, _ in syms]
def name(v):
    if slide is None: return '?'
    a = v - slide; i = bisect.bisect_right(addrs, a) - 1
    return '%s+%#x' % (syms[i][1], a - syms[i][0]) if i >= 0 else '?'
seen = set()
for line in open(txt, errors='replace'):
    for h in re.findall(r'0x(fffffe[0-9a-f]{10})|\b(fffffe[0-9a-f]{10})\b', line, re.I):
        v = int(h[0] or h[1], 16)
        if v in seen: continue
        seen.add(v); print('%#x -> %s   | %s' % (v, name(v), line.strip()[:120]))
last = [l for l in t.splitlines() if l.startswith('GT ')][-12:]
print('last trace lines (cpu at the end):'); [print('  ' + l) for l in last]
