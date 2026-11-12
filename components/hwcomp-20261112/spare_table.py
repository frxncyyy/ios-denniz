#!/usr/bin/env python3
"""boot_spare budget table (boot_spare = 32 MiB - image bytes, from each candidate's ui-sched/image-receipt.json).
Per-patch cost = spare(build without it) - spare(build with it), from pairs of builds that differ by exactly that patch set and use
the same-size audio package (255216 B: audio-q10jp.pkg / audio-q10jae.pkg). The image is compressed and padded to 2048 B, so each
cost is +-2048. Prints the forecast for q10jfi / q10jfj and their real spare once built.
usage: spare_table.py [--min 8192]"""
import json, sys
from pathlib import Path
B = Path(__file__).resolve().parent.parent
MIN = int(sys.argv[sys.argv.index('--min') + 1]) if '--min' in sys.argv else 8192

def spare(name):
    r = B / f'device-mmu-{name}-candidate/ui-sched/image-receipt.json'
    return json.loads(r.read_text())['boot_spare'] if r.exists() else None

# (patches, without, with, note, trim class)
PAIRS = [
    ('0350+0351', 'q10jm', 'q10jr', 'GNSS gpsdl core + bind', 'keep'),
    ('0353', 'q10jr', 'q10jr3', 'GNSS safe steps', 'keep'),
    ('0354', 'q10jr3', 'q10jr4', 'GNSS one-access steps', 'probe ioctl'),
    ('0355', 'q10jr4', 'q10jr5', 'GNSS safe3', 'keep'),
    ('0356', 'q10jr5', 'q10jr6', 'GNSS power ladder', 'LADDER (bring-up only)'),
    ('0357', 'q10jr6', 'q10jr7', 'GNSS ladder L1', 'LADDER (bring-up only)'),
    ('0358', 'q10jr7', 'q10jr7e', 'GNSS L1 probe', 'PROBE ioctl (bring-up only)'),
    ('0359', 'q10jr7e', 'q10jr8', 'GNSS datalink ladder', 'LADDER (bring-up only)'),
    ('0362', 'q10jr8', 'q10jr9', 'GNSS datalink v9', 'keep'),
    ('0363', 'q10jr9', 'q10jr10', 'GNSS split ladders', 'LADDER (bring-up only)'),
    ('0364', 'q10jr10', 'q10jr11', 'GNSS DSP boot', 'keep'),
    ('0390', 'q10jp', 'q10jfb', 'fb layer blend', 'keep'),
    ('0391', 'q10jfb', 'q10jfc', 'fb inval diag', 'diag (trimmable)'),
    ('0392', 'q10jfc', 'q10jfe', 'fb swap-mask retain', 'keep'),
    ('0393', 'q10jfe', 'q10jfh', 'fb sync', 'keep'),
    ('0394', 'q10jfh', 'q10jfi-0394', 'display pool (track A)', 'keep'),
    ('0395', 'q10jfi-0394', 'q10jfi-0395', 'GPU pool import + fences (track B)', 'keep'),
    ('0396', 'q10jfi-0395', 'q10jfi', 'OVL HW composition (track C)', 'keep'),
]
rows, cost = [], {}
print(f"{'patch':<10} {'cost B':>7}  {'without':<12} {'with':<12} {'note':<34} trim class")
for p, a, b, note, cls in PAIRS:
    sa, sb = spare(a), spare(b)
    c = sa - sb if sa is not None and sb is not None else None
    cost[p] = c
    print(f"{p:<10} {c if c is not None else '-':>7}  {a:<12} {b:<12} {note:<34} {cls}")
gk = [k for k in cost if k.startswith(('035', '036'))]
gn = sum(cost[k] for k in gk) if all(cost[k] is not None for k in gk) else None
print(f"\nGNSS set total {gn} B; trimmable bring-up ladders/probes {sum(cost[k] or 0 for k in ('0354','0356','0357','0358','0359','0363'))} B")
H, G = spare('q10jfh'), spare('q10jfg')
print(f"q10jfh spare {H}; q10jfg spare {G}; q10jfj base forecast (q10jfg - 0393) {G - (cost['0393'] or 0) if G else '-'}")
for n, base in (('q10jfi', H), ('q10jfj', G - (cost['0393'] or 0) if G else None)):
    s = spare(n)
    if s is None:
        print(f"{n}: not built; spare = {base} - cost(0394..0396)  -> trim needed only if 0394..0396 cost >= {base - MIN if base else '-'} B (to stay >= {MIN})")
    else:
        print(f"{n}: spare {s} ({'OK' if s >= MIN else 'LOW: trim ' + ('GNSS ladders 0356/0359/0363 + probe 0358' if n == 'q10jfj' else '0391 diag')})"
              f" -> 0394..0396 cost {base - s if base else '-'} B")
