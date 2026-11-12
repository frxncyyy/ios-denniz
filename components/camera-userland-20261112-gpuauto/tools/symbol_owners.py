#!/usr/bin/env python3
"""
Writes symbol_owners.tsv: for every symbol the bundle's objects import, the install name of the image of the iOS 16.7 dyld shared cache
that exports it (from the cache's patch-info export tables) and whether it was found. Needs the private cache and the helper modules
of the session scratchpad (dsc2.py / exports.py: the cache's LINKEDIT subcaches are APFS-compressed, LZBITMAP; see REPORT.md section 3);
the result, tools/symbol_owners.tsv, is names only and is what make_tbds.py (no cache needed) reads.
usage: symbol_owners.py <scratchpad dsc dir> <need.txt> <out.tsv>
"""
import sys
sys.path.insert(0, sys.argv[1])
import exports, dsc2
need = [l.strip() for l in open(sys.argv[2]) if l.strip()]
ex = exports.build_cache()
idx = {}
for p, (a, L) in ex.items():
    for addr, n in L:
        nm = dsc2.rd(exports.NAMES + (n & 0x0fffffff), 120).split(b'\0')[0].decode('latin1')
        idx.setdefault(nm, []).append(p)
with open(sys.argv[3], 'w') as f:
    for n in need:
        p = idx.get(n)
        f.write('%s\t%s\t%s\n' % (n, p[0] if p else '-', 'cache-export' if p else 'not-in-patch-table'))
