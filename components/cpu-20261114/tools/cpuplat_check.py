#!/usr/bin/env python3
"""0641 log check: CPU platform parity (CCI row, LL clock, CCI perf half) + CPU placement / cluster clocks.

    python3 cpuplat_check.py XNU_LIVE_LOG [--tables nord2_cpuhvfs_tables.h]

Reads 'NORD2 CPUPLAT:', 'NORD2 DVFS DECISION', 'NORD2 SYS:' (per-core busy), 'NORD2 SCHED watch' lines.
Works on pre-0641 captures too: then it prints which CCI row the old rule chose and which the 0641 rules would choose for the same
cluster rows (from the DECISION 'now L= B=' clocks, LL assumed at 1800 MHz / 2000 MHz with the boost).
Verdict line: CPUPLAT_ACTIVE / CPUPLAT_LL_BAD / CPUPLAT_OFF / CPUPLAT_NOT_SEEN.
"""
import re, sys, collections
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEF_TABLES = HERE.parent / 'piece-0641/patched-sources-0641/bsd/dev'


def load_tables(path):
    p = Path(path)
    if p.is_dir():
        p = p / 'nord2_cpuhvfs_tables.h'
    if not p.exists():
        p = Path('$NORD2_WORK/gpu/build/device-mmu-q10jperf1-candidate/k/kernel/stage/bsd/dev/nord2_cpuhvfs_tables.h')
    s = p.read_text()
    pvt = [[int(x) for x in r.split(',')] for r in re.findall(r'\{\s*(\d+,\s*\d+,\s*\d+,\s*\d+)\s*\}', s.split('n2dv_l1_cci')[0])]
    cci_part = s.split('n2dv_l1_cci')[1]
    cci = [[int(x) for x in r.split(',')] for r in re.findall(r'\{([0-9,\s]+)\}', cci_part)][:32]
    assert len(pvt) == 64 and len(cci) == 32 and all(len(r) == 16 for r in cci)
    return pvt, cci


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    tables = DEF_TABLES
    if '--tables' in sys.argv:
        tables = sys.argv[sys.argv.index('--tables') + 1]
        args = [a for a in args if a != tables]
    if not args:
        print(__doc__)
        return 2
    pvt, cci = load_tables(tables)
    mhz = lambda cl, r: pvt[16 * cl + r][0]
    row_of = lambda cl, m: min(range(16), key=lambda r: abs(mhz(cl, r) - m))
    b_row_for = lambda m: max([r for r in range(16) if mhz(2, r) >= m] or [0])

    def rule(ll, l, b, mode, perf):
        col = b
        if mode & 1:
            col = b_row_for(max(mhz(1, l), mhz(2, b)))
        return cci[(16 if (mode & 4) and perf else 0) + ll][col]

    plat, dec, sysl, sched = [], [], [], []
    boot = ll_ver = None
    lvts, lvts_sens, soc_lines, prime, primeb, jobs3 = [], [], [], [], [], []
    for line in open(args[0], errors='replace'):
        i = line.find('NORD2 GPU LVTS:')
        if i >= 0:
            (lvts_sens if ' sensor ' in line else lvts).append(line[i:].strip())
            continue
        i = line.find('NORD2 DVFS thermal ladder: level')
        if i >= 0 and 'SoC' in line:
            soc_lines.append(line[i:].strip())
        i = line.find('NORD2 SCHED prime')
        if i >= 0:
            m = re.search(r'busy (\d+)%, ca on prime (\d+)%, prime picks (\d+)/s', line[i:])
            if m:
                prime.append([int(x) for x in m.groups()])
            continue
        if 'prime-first placement' in line:
            primeb.append(line[line.find('NORD2'):].strip())
        i = line.find('NORD2 GPU JOBS3:')
        if i >= 0:
            m = re.search(r'bat_cC=(-?\d+).*gpu_cC=(-?\d+) soc_cC=(-?\d+)', line[i:])
            if m:
                jobs3.append([int(x) for x in m.groups()])
            continue
        i = line.find('NORD2 CPUPLAT:')
        if i >= 0:
            t = line[i:].strip()
            if ' on, ' in t:
                boot = t
            elif 'LL row 0' in t:
                ll_ver = t
            else:
                m = re.search(r'mode=(\d+) ll=(\d+)MHz cci=(-?\d+)MHz\(row (-?\d+)\) perf=(on|off) \| 2s: perf_ms=(\d+) ll_fast_ms=(\d+) cci_writes=(\d+) ll_moves=(\d+) \| ll_bad=(\d+)', t)
                if m:
                    plat.append([int(x) if x.lstrip('-').isdigit() else x for x in m.groups()])
            continue
        i = line.find('NORD2 DVFS DECISION')
        if i >= 0:
            m = re.search(r'now L=(\d+) B=(\d+) G=(\d+).*class=(\w+).*therm=(\d+) bat=([\d.]+)C', line[i:])
            if m:
                dec.append((int(m[1]), int(m[2]), m[4], int(m[5]), float(m[6])))
            continue
        i = line.find('NORD2 SYS:')
        if i >= 0:
            m = re.search(r'cpu_busy%=([\d/]+)', line[i:])
            if m:
                v = [int(x) for x in m[1].split('/')]
                if len(v) == 8:
                    sysl.append(v)
            continue
        i = line.find('NORD2 SCHED watch')
        if i >= 0:
            m = re.search(r'ca (\d+) ms/s on big (\d+)%.*elig-on-little-while-big-idle (\d+)/s; placed big-idle (\d+)/s big-preempt (\d+)/s spill (\d+)/s kept-little (\d+)/s pull (\d+)/s', line[i:])
            if m:
                sched.append([int(x) for x in m.groups()])

    print(f'log: {args[0]}')
    print(f'boot line: {boot or "-"}')
    print(f'LL check: {ll_ver or "-"}')
    if dec:
        cls = collections.Counter(d[2] for d in dec)
        print(f'DECISION rows: {len(dec)}; classes ' + ', '.join(f'{k} {v}' for k, v in cls.most_common()))
        print(f'  battery {dec[0][4]:.1f} -> {dec[-1][4]:.1f} C, max {max(d[4] for d in dec):.1f} C; thermal levels ' +
              ', '.join(f'{k}:{v}' for k, v in sorted(collections.Counter(d[3] for d in dec).items())))
        for name, idx, cl in (('L', 0, 1), ('B', 1, 2)):
            c = collections.Counter(d[idx] for d in dec)
            print(f'  cluster {name} clock share: ' + ', '.join(f'{k} {100 * v // len(dec)}%' for k, v in sorted(c.items(), reverse=True)))
        # what the old / 0641 rules pick for the logged cluster rows
        old, new, perf = collections.Counter(), collections.Counter(), collections.Counter()
        for L, Bm, c, th, bat in dec:
            l, b = row_of(1, L), row_of(2, Bm)
            old[mhz(3, rule(1, l, b, 0, False))] += 1
            cool = th < 2
            new[mhz(3, rule(0 if cool else 1, l, b, 1 | 2 | 4, cool and c in ('anim', 'touch', 'jank')))] += 1
            perf[c in ('anim', 'touch', 'jank') and cool] += 1
        fmt = lambda C: ', '.join(f'{k} MHz {100 * v // len(dec)}%' for k, v in sorted(C.items(), reverse=True))
        print(f'  CCI by the pre-0641 rule (these rows): {fmt(old)}')
        print(f'  CCI by the 0641 rules (all bits, same rows): {fmt(new)}  (perf half would be on in {100 * perf[True] // len(dec)}% of the rows)')
    if sysl:
        n = len(sysl)
        avg = [sum(v[k] for v in sysl) / n for k in range(8)]
        act = [v for v in sysl if sum(v) / 8 >= 15]
        aavg = [sum(v[k] for v in act) / len(act) for k in range(8)] if act else avg
        print(f'SYS windows {n} ({len(act)} with avg >= 15 %): per-core busy % cpu0-7 all: ' + '/'.join(f'{x:.0f}' for x in avg) +
              ' | loaded windows: ' + '/'.join(f'{x:.0f}' for x in aavg))
        print(f'  loaded windows: A55 cpu0-3 {sum(aavg[:4]) / 4:.0f} %, A78 cpu4-6 {sum(aavg[4:7]) / 3:.0f} %, prime cpu7 {aavg[7]:.0f} %')
    if sched:
        n = len(sched)
        s = [sum(x[k] for x in sched) / n for k in range(8)]
        print(f'SCHED watch {n}: CA render {s[0]:.0f} ms/s, {s[1]:.0f} % on big; eligible-on-little-while-big-idle {s[2]:.0f}/s; placed big-idle {s[3]:.0f}/s '
              f'preempt {s[4]:.0f}/s spill {s[5]:.0f}/s kept-little {s[6]:.0f}/s pull {s[7]:.0f}/s')
    if plat:
        n = len(plat)
        modes = collections.Counter(p[0] for p in plat)
        llc = collections.Counter(p[1] for p in plat)
        ccic = collections.Counter(p[2] for p in plat)
        perf_ms = sum(p[5] for p in plat); llf = sum(p[6] for p in plat); w = sum(p[7] for p in plat); mv = sum(p[8] for p in plat)
        print(f'CPUPLAT lines {n}: modes ' + ', '.join(f'{k}:{v}' for k, v in modes.items()) + '; LL ' + ', '.join(f'{k} MHz {100 * v // n}%' for k, v in llc.most_common()) +
              '; CCI at the line ' + ', '.join(f'{k} MHz {100 * v // n}%' for k, v in sorted(ccic.items(), reverse=True)))
        print(f'  perf half {perf_ms / 1000:.1f} s of ~{2 * n} s, LL at 2000 MHz {llf / 1000:.1f} s, CCI stores by refresh {w}, LL moves {mv}, ll_bad {plat[-1][9]}')
    print(f'prime boot: {primeb[0] if primeb else "-"}')
    if prime:
        n = len(prime)
        print(f'SCHED prime {n} lines: cpu7 busy {sum(p[0] for p in prime) / n:.0f} %, CA render on prime {sum(p[1] for p in prime) / n:.0f} %, prime picks {sum(p[2] for p in prime) / n:.0f}/s')
    for l in lvts[:3]:
        print(l)
    for l in lvts_sens[:20]:
        print('  ' + l)
    socs = [j[2] for j in jobs3 if j[2] != -9999]
    if jobs3:
        print(f'JOBS3 {len(jobs3)}: SoC readings {len(socs)}' + (f', SoC {min(socs) / 100:.1f}..{max(socs) / 100:.1f} C, battery {min(j[0] for j in jobs3) / 100:.1f}..{max(j[0] for j in jobs3) / 100:.1f} C' if socs else ''))
    for l in soc_lines[:6]:
        print(l)
    lv = 'LVTS_NOT_SEEN'
    if lvts:
        lv = 'LVTS_READING' if socs else ('LVTS_FAILED' if any('state=3' in l for l in lvts) else 'LVTS_NO_VALID_SENSOR')
    print(lv)
    verdict = 'CPUPLAT_NOT_SEEN'
    if boot or plat:
        if ll_ver and 'NOT reached' in ll_ver:
            verdict = 'CPUPLAT_LL_BAD'
        elif plat and all(p[0] == 0 for p in plat):
            verdict = 'CPUPLAT_OFF'
        else:
            verdict = 'CPUPLAT_ACTIVE'
    print(verdict)
    return 0


if __name__ == '__main__':
    sys.exit(main())
