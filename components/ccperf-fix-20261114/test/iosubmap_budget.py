#!/usr/bin/env python3
"""iosubmap_budget.py LOG...   (ccperf-fix-20261114, host only, reads logs)
XNU's io_submap (ml_io_map / ml_io_map_wcomb) is 8 MiB = 512 pages of 16 KiB, permanent, KMA_NOFAIL: the map that does not fit panics inside the call and the
pulled USB console never shows it (0410).  The kernel prints the range at boot ("io_submap : A - B (8M)") and nord2_scp.inc prints every SCP map before and after,
allocations are bottom-up and contiguous, so a console log tells how full it was:
  before_scp = pages taken when the SCP thread starts mapping (boot + 90 s)    left = pages free after its last finished map
  verdict FULL = a map was announced ("IOMAP x pa ...: mapping") and never answered while fewer pages were left than it needs.
--expect NAME=before,left,verdict ... turns it into a check (used by run.sh on the three evidence extracts).   --min-left N: exit 1 when a log has less."""
import re, sys
PG = 0x4000
def pages(pa, size): return ((pa & (PG - 1)) + size + PG - 1) // PG
def analyse(path):
    txt = open(path, 'rb').read().decode('latin1')
    m = re.search(r'io_submap\s*: 0x([0-9a-f]+) - 0x([0-9a-f]+)', txt)
    if not m: return None
    base, end = int(m.group(1), 16), int(m.group(2), 16)
    ev = re.findall(r'NORD2 SCP: IOMAP (\S+) (?:pa 0x([0-9a-f]+) \+0x([0-9a-f]+): mapping|-> 0x([0-9a-f]+))', txt)
    r = {'pages': (end - base) // PG, 'before_scp': None, 'left': None, 'verdict': 'no SCP maps in the log', 'pending': None, 'probe': 'NORD2 DRAM RATE: shu_lv' in txt,
         'outside': 'NORD2 DRAM IOMAP:' in txt}
    top, pend = None, None
    for what, pa, sz, va in ev:
        if pa: pend = (what, int(pa, 16), int(sz, 16)); continue
        va = int(va, 16) & ~(PG - 1); need = pages(pend[1], pend[2]) if pend and pend[0] == what else 1
        if r['before_scp'] is None: r['before_scp'] = (va - base) // PG
        top = va + need * PG; pend = None
    if top is not None:
        r['left'] = (end - top) // PG; r['verdict'] = 'ok'
        if pend:
            need = pages(pend[1], pend[2]); r['pending'] = '%s needs %u' % (pend[0], need)
            r['verdict'] = 'FULL' if need > r['left'] else 'log ends inside a map that fits'
    return r
def main():
    a = sys.argv[1:]; exp = {}; minleft = None; logs = []; rc = 0
    while a:
        x = a.pop(0)
        if x == '--expect':
            while a and '=' in a[0]: k, v = a.pop(0).split('=', 1); exp[k] = v
        elif x == '--min-left': minleft = int(a.pop(0))
        else: logs.append(x)
    for p in logs:
        r = analyse(p); name = p.rsplit('/', 1)[-1].rsplit('.', 1)[0]
        if r is None: print('%s: no io_submap range line' % p); rc = 1; continue
        print('%s: io_submap %u pages, before the SCP maps %s taken, after them %s left%s -> %s%s%s' % (name, r['pages'], r['before_scp'], r['left'],
              ', unanswered map: ' + r['pending'] if r['pending'] else '', r['verdict'], ' | DRAM probe ran' if r['probe'] else '',
              ' with maps outside io_submap (0494)' if r['outside'] else ''))
        if name in exp:
            got = '%s,%s,%s' % (r['before_scp'], r['left'], r['verdict'].split()[0])
            if got != exp[name]: print('  EXPECTED %s, got %s' % (exp[name], got)); rc = 1
        if minleft is not None and (r['left'] is None or r['left'] < minleft or r['verdict'] == 'FULL'): print('  BELOW --min-left %u' % minleft); rc = 1
    sys.exit(rc)
main()
