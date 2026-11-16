#!/usr/bin/env python3
"""Summarise patch 0099 lines in an xnu live log (host-only).

- NORD2 CPUCLK: per-CPU MHz samples -> per-cluster min/median/max, and a verdict
  against MT6893 nominal maxima (A55 2000, A78 2600, cpu7 A78 3000 MHz).
- NORD2 SCHED (+ AB phase markers): placement/accounting per phase.
- TEGU_PERF windows (backboardd) split by the kernel A/B phase markers in log
  order: commands/s, exec ms per command, our encode ms per draw.

    analyze_nord2_sched.py LOG [--json OUT]
"""
import argparse, json, re, statistics, sys
from pathlib import Path

NOMINAL = {**{c: 2000 for c in range(4)}, 4: 2600, 5: 2600, 6: 2600, 7: 3000}
CLK = re.compile(r'NORD2 CPUCLK((?: \d+:\d+)+) MHz big 0x([0-9a-f]+)(.*)')
AB = re.compile(r'NORD2 SCHED AB phase=(\d+) mode=0x([0-9a-f]+) t_ms=(\d+)')
S1 = re.compile(r'NORD2 SCHED mode 0x([0-9a-f]+) pri>=(\d+) ab=(\d+): big busy (\d+)% elig (\d+)%, little busy (\d+)% elig (\d+)%')
S2 = re.compile(r'NORD2 SCHED watch (\S+) (\d+) ms/s on big (\d+)% (\d+) runs/s; elig-on-little-while-big-idle (\d+)/s; '
                r'placed big-idle (\d+)/s big-preempt (\d+)/s spill (\d+)/s kept-little (\d+)/s pull (\d+)/s \(ipi (\d+)/s\)')
INIT = re.compile(r'NORD2 SCHED init: (.*)')
KV = re.compile(r'(\w+)=(-?\d+)')


def analyze(text):
    phase = 0
    clk = []
    init = None
    phases = {}

    def ph(p):
        return phases.setdefault(p, {'sched': [], 'watch': [], 'perf': {'window_ms': 0, 'commands': 0, 'exec_ms': 0},
                                     'encode': {'draws': 0, 'draw_ms': 0, 'plan_hit': 0, 'plan_miss': 0}})
    for line in text.splitlines():
        if (m := INIT.search(line)):
            init = m.group(1)
        if (m := AB.search(line)):
            phase = int(m.group(1))
            continue
        if (m := CLK.search(line)):
            clk.append({'mhz': {int(c): int(v) for c, v in (x.split(':') for x in m.group(1).split())},
                        'big': int(m.group(2), 16), 'extra': m.group(3).strip()})
            continue
        if (m := S1.search(line)):
            ph(phase)['sched'].append([int(x, 16) if i == 0 else int(x) for i, x in enumerate(m.groups())])
            continue
        if (m := S2.search(line)):
            ph(phase)['watch'].append([m.group(1)] + [int(x) for x in m.groups()[1:]])
            continue
        if '|backboardd|' in line and ('TEGU_PERF ' in line or 'TEGU_PERF_ENCODE ' in line):
            for seg in line.split('|backboardd|'):
                kv = dict((k, int(v)) for k, v in KV.findall(seg.split('|')[0]))
                if 'TEGU_PERF_ENCODE ' in seg:
                    for k in ph(phase)['encode']:
                        ph(phase)['encode'][k] += kv.get(k, 0)
                elif 'TEGU_PERF ' in seg:
                    for k in ph(phase)['perf']:
                        ph(phase)['perf'][k] += kv.get(k, 0)
    out = {'init': init, 'cpuclk_samples': len(clk), 'phases': {}}
    if clk:
        per = {}
        for s in clk:
            for c, v in s['mhz'].items():
                per.setdefault(c, []).append(v)
        out['cpuclk'] = {c: {'min': min(v), 'median': statistics.median(v), 'max': max(v), 'nominal': NOMINAL.get(c),
                             'below_nominal': NOMINAL.get(c) is not None and statistics.median(v) < 0.9 * NOMINAL[c]}
                         for c, v in sorted(per.items())}
        out['cpuclk_extra_last'] = clk[-1]['extra']
    for p, d in sorted(phases.items()):
        perf, enc = d['perf'], d['encode']
        r = {}
        if perf['window_ms']:
            r['cmd_per_s'] = round(perf['commands'] * 1000 / perf['window_ms'], 2)
            r['exec_ms_per_cmd'] = round(perf['exec_ms'] / perf['commands'], 2) if perf['commands'] else None
        if enc['draws']:
            r['encode_ms_per_draw'] = round(enc['draw_ms'] / enc['draws'], 3)
            r['plan_miss_pct'] = round(100 * enc['plan_miss'] / max(1, enc['plan_hit'] + enc['plan_miss']), 1)
        if d['sched']:
            cols = list(zip(*d['sched']))
            r['big_busy_pct'] = round(statistics.mean(cols[3]), 1)
            r['little_busy_pct'] = round(statistics.mean(cols[5]), 1)
            r['big_elig_pct'] = round(statistics.mean(cols[4]), 1)
            r['little_elig_pct'] = round(statistics.mean(cols[6]), 1)
        if d['watch']:
            cols = list(zip(*d['watch']))
            r['ca_ms_per_s'] = round(statistics.mean(cols[1]), 1)
            r['ca_on_big_pct'] = round(statistics.mean(cols[2]), 1)
            r['elig_on_little_while_big_idle_per_s'] = round(statistics.mean(cols[4]), 1)
        r['windows'] = len(d['sched'])
        out['phases'][p] = r
    return out


def main():
    a = argparse.ArgumentParser()
    a.add_argument('log', type=Path)
    a.add_argument('--json', type=Path)
    args = a.parse_args()
    res = analyze(args.log.read_text(errors='replace'))
    if args.json:
        args.json.write_text(json.dumps(res, indent=1) + '\n')
    print('init:', res['init'] or '(no NORD2 SCHED init line: kernel without 0099)')
    for c, v in res.get('cpuclk', {}).items():
        flag = '  BELOW NOMINAL' if v['below_nominal'] else ''
        print(f"cpu{c}: {v['min']}/{v['median']}/{v['max']} MHz (min/median/max), nominal {v['nominal']}{flag}")
    if res.get('cpuclk_extra_last'):
        print('csram:', res['cpuclk_extra_last'])
    for p, r in res['phases'].items():
        print(f'phase {p}:', ', '.join(f'{k}={v}' for k, v in r.items()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
