#!/usr/bin/env python3
"""ram8-20261114: read an XNU live log (ui-usb/xnu-live.log or console-ramoops) of q10jram8a/b and say what the 0650/0651 heap did.
    ram8_check.py LOG
Verdicts: RAM8_ALL_READY (every wanted region ready, no foreign write) / RAM8_PARTIAL (some region off: reason printed) /
RAM8_HANG_AT_<region> (a 'setup begins' line without its result line = the log ends inside that region's first touch) / RAM8_NOT_SEEN.
"""
import re, sys

t = open(sys.argv[1], 'rb').read().decode('latin-1')
lines = [l for l in re.split(r'[\r\n]+', t) if 'NORD2 HEAP' in l or 'NORD2 RAM8' in l or 'NORD2 FB POOL: 0650' in l]
for l in lines:
    if 'RAM8:' in l or '0650' in l or 'OFF' in l or 'NOT used' in l or 'FOREIGN' in l or ('region ' in l and 'canary' in l):
        print(l[l.index('NORD2'):][:400])
stats = [l for l in lines if 'NORD2 HEAP: on ' in l]
if stats:
    print('last stats:', stats[-1][stats[-1].index('NORD2'):][:500])
begun = re.findall(r'0650 region (\w+) pa .* setup begins', t)
done = set(re.findall(r'NORD2 HEAP: region (\w+) pa 0x[0-9a-f]+\.\.0x[0-9a-f]+ \(\d+ KiB', t)) | set(re.findall(r'region (\w+) NOT used', t))
hang = [r for r in begun if r not in done]
off = re.findall(r'region (\w+) (?:OFF|NOT used)[^\n]*', t)
foreign = re.findall(r'FOREIGN write[^\n]*', t)
if not lines:
    print('RAM8_NOT_SEEN')
elif hang:
    print('RAM8_HANG_AT_%s (setup began, no result line: if the phone hung here, flash q10jfinal1)' % hang[-1])
elif off or foreign:
    print('RAM8_PARTIAL off=%s foreign=%d' % (sorted(set(off)), len(foreign)))
else:
    print('RAM8_ALL_READY' if 'all wanted regions set up' in t else 'RAM8_SETUP_IN_PROGRESS (log ends before the plan line)')
