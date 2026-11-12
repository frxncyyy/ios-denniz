#!/usr/bin/env python3
"""CPU-free composition verdict from a console log (0402, hwmulti-20261113).

  cpufree_verdict.py LOG [LOG...] [--from OFFSET] [--to OFFSET] [--json OUT] [--min-pass N]

Reads the kernel's 60-second window lines
  NORD2 FB CPUFREE: FB HW: CPU-FREE <PASS|FAIL|IDLE|OFF> window_s=.. hw=.. cpu=.. cpu_per_min=.. cpu_fault=.. hw_mode=.. cap=..
                    why[count/cover/format/memory/pitch/range/missing/state]=a/b/c/d/e/f/g/h fence_wait=.. fence_forced=..
and the cumulative 'NORD2 FB HW: mode=...' lines (for 4L faults, fence_forced, scaled, faults), plus the fault lines
('AUTO-DISABLE', '4L OFF', 'BACKOFF').  Console records may be cut mid-line by interleaving: unparsable windows are skipped.

Verdict (exit code): PASS (0) = at least --min-pass (default 1) PASS windows and no FAIL window;
FAIL (1) = any FAIL window (CPU frames outside a fault backoff), printed with the reasons;
NO DATA (2) = no PASS/FAIL window (HW off or idle all the time).  Hardware faults (cpu_fault, AUTO-DISABLE, 4L OFF) are
reported separately (WARN lines) and do not fail the CPU-free requirement by themselves.
"""
import json, re, sys

WIN = re.compile(r'NORD2 FB CPUFREE: FB HW: CPU-FREE (PASS|FAIL|IDLE|OFF) (window_s=\d+[^\n]*)')
KV = re.compile(r'([a-z_0-9]+)=(\d+)')
WHY = re.compile(r'why\[count/cover/format/memory/pitch/range/missing/state\]=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)')
REASONS = ('count', 'cover', 'format', 'memory', 'pitch', 'range', 'missing', 'state')
HWLINE = re.compile(r'NORD2 FB HW: (mode=\d+[^\n]*)')
FAULTS = re.compile(r'NORD2 FB HW: (AUTO-DISABLE[^\n]{0,120}|4L OFF[^\n]{0,120}|BACKOFF[^\n]{0,80})')


def parse(text):
    wins = []
    for m in WIN.finditer(text):
        body = m.group(2)
        d = {k: int(v) for k, v in KV.findall(body)}
        if 'cpu' not in d or 'hw' not in d:
            continue
        w = WHY.search(body)
        d['why'] = dict(zip(REASONS, map(int, w.groups()))) if w else {}
        d['verdict'] = m.group(1)
        d['offset'] = m.start()
        wins.append(d)
    hw = [{k: int(v) for k, v in KV.findall(m.group(1))} for m in HWLINE.finditer(text)]
    faults = [(m.start(), m.group(1)) for m in FAULTS.finditer(text)]
    return wins, hw, faults


def judge(wins, hw, faults, min_pass=1):
    n = {v: sum(1 for w in wins if w['verdict'] == v) for v in ('PASS', 'FAIL', 'IDLE', 'OFF')}
    why = {r: sum(w['why'].get(r, 0) for w in wins) for r in REASONS}
    cpu = sum(w['cpu'] for w in wins); hwf = sum(w['hw'] for w in wins); cf = sum(w.get('cpu_fault', 0) for w in wins)
    lines = ['windows: %d PASS, %d FAIL, %d IDLE, %d OFF; HW frames %d, CPU frames %d (%.2f %% of composed frames), cpu frames during fault backoff %d'
             % (n['PASS'], n['FAIL'], n['IDLE'], n['OFF'], hwf, cpu, 100.0 * cpu / max(1, cpu + hwf), cf)]
    if cpu:
        lines.append('CPU frames by reason: ' + ', '.join('%s=%d' % (r, why[r]) for r in REASONS if why[r]))
        worst = max((w for w in wins if w['verdict'] == 'FAIL'), key=lambda w: w['cpu'], default=None)
        if worst:
            lines.append('worst window at offset %d: cpu=%d cpu_per_min=%d hw=%d cap=%d' % (worst['offset'], worst['cpu'], worst.get('cpu_per_min', 0),
                                                                                         worst['hw'], worst.get('cap', 0)))
    if hw:
        a, b = hw[0], hw[-1]
        dd = lambda k: b.get(k, 0) - a.get(k, 0)
        lines.append('cumulative over the log: l4_frames +%d l4_faults +%d fence_wait +%d fence_forced +%d fence_resv +%d scaled +%d faults +%d race +%d'
                     % tuple(dd(k) for k in ('l4_frames', 'l4_faults', 'fence_wait', 'fence_forced', 'fence_resv', 'scaled', 'faults', 'race')))
        if dd('scaled'):
            lines.append('WARN scaled/transformed layers were presented %d times (shown 1:1; IOMFBScalingLimits not honoured?)' % dd('scaled'))
    for off, s in faults[:12]:
        lines.append('WARN fault at %d: %s' % (off, s))
    if n['FAIL']:
        v = 'FAIL'
    elif n['PASS'] >= min_pass:
        v = 'PASS'
    else:
        v = 'NO DATA'
    lines.append('FB HW: CPU-FREE %s' % v)
    return v, lines, dict(windows=n, cpu=cpu, hw=hwf, cpu_fault=cf, why=why)


def main(argv):
    args = argv[1:]
    opt = {}
    for k in ('--from', '--to', '--json', '--min-pass'):
        if k in args:
            i = args.index(k); opt[k] = args[i + 1]; del args[i:i + 2]
    if not args:
        print(__doc__); return 2
    text = ''.join(open(p, errors='replace').read() for p in args)
    text = text[int(opt.get('--from', 0)):int(opt['--to']) if '--to' in opt else None]
    wins, hw, faults = parse(text)
    v, lines, summary = judge(wins, hw, faults, int(opt.get('--min-pass', 1)))
    print('\n'.join(lines))
    if '--json' in opt:
        summary['verdict'] = v
        json.dump(summary, open(opt['--json'], 'w'), indent=1)
    return {'PASS': 0, 'FAIL': 1}.get(v, 2)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
