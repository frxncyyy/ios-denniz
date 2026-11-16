#!/usr/bin/env python3
"""crumbs.py dmesg-ramoops-NNN[.txt] : the BT breadcrumb zone (0720 bt8 layout) in order.

bt8 layout of the 4080-byte record: [0,900) first lines of the boot (linear), [900,940) NOW slot, [940,964) GUARD slot,
[964,4080) ring of 41 x 76-byte slots holding the LAST lines; every line starts "N2BT <seq> <ms> ...", so the ring is sorted by seq.
Older (linear) records are printed as they are."""
import re, sys

data = open(sys.argv[1], 'rb').read().decode('latin-1')
if len(data) < 964 or not data.startswith('====0.0-D'):
    print(data)
    sys.exit(0)
lin, now, guard, ring = data[:900], data[900:940], data[940:964], data[964:]
print('== first lines of that boot')
for l in lin.splitlines():
    if l.strip():
        print(l.rstrip())
print('== NOW (what the BT thread did last):', now.strip() or '(nothing)')
print('== GUARD:', guard.strip() or '(empty: the bring-up never got to INFLIGHT)')
lines = []
for i in range(0, len(ring) - 75, 76):
    t = ring[i:i + 76].rstrip()
    m = re.match(r'N2BT (\d+) ', t)
    if m:
        lines.append((int(m.group(1)), t))
print('== last %d lines (ring, oldest first)' % len(lines))
for _, t in sorted(lines):
    print(t)
