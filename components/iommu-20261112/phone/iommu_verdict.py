#!/usr/bin/env python3
"""0401 display-IOMMU verdict from a console capture (autocycle/runs/TAG/ui-usb/xnu-live.log).

  iommu_verdict.py LOG [--json OUT]        exit 0 = no FAIL;  iommu_verdict.py --selftest

Segments = the "N2FBHWCTL iommu phase I/N iommu=K" lines of the fbiommu job (else one segment).  Per segment:
  maps / walk BAD / map refused / layers handed to the planner / dry lookups (from the 10 s "NORD2 FB IOMMU: mode=" counters and the map lines),
  ports ON/off, M4U faults, AUTO-DISABLE, HW frames (delta of the "NORD2 FB HW: mode=.. hw_frames=" line), HW auto-disables.
Rules: FAIL = M4U fault, IOMMU AUTO-DISABLE, REFUSED, walk BAD, a HW AUTO-DISABLE in a phase with iommu >= 2, iommu 1 phase that lets a
layer through or turns the ports on, iommu >= 2 phase with no IOVA layer / no HW frame.  WARN = map refused, no maps at all in iommu 1.
The probe lines (registers) are summarised at the top: larb0/1 port 6 (OVL1_2L / OVL0_2L) must have bit0 = 0 before arming.
"""
import json, re, sys

R_PH = re.compile(r'N2FBHWCTL iommu phase (\d+)/(\d+) iommu=(\d+) rc=(-?\d+) readback=(-?\d+)')
R_CNT = re.compile(r'NORD2 FB IOMMU: mode=(-?\d+) state=(\d+).*?maps=(\d+) hits=(\d+) unmaps=(\d+) evicts=(\d+).*?map_fail=(\d+) verify_bad=(\d+) '
                   r'refused=(\d+) layers=(\d+) dry=(\d+) enables=(\d+) disables=(\d+) faults=(\d+)')
R_HW = re.compile(r'NORD2 FB HW: mode=(\d+) hw_frames=(\d+) cpu_frames=(\d+)')
R_MAP = re.compile(r'NORD2 FB IOMMU: map id (\d+) (\d+) bytes -> iova (0x[0-9a-f]+) pages (\d+) segs (\d+) pa (0x[0-9a-f]+)\.\.(0x[0-9a-f]+) above_4g (\d) walk (ok|BAD)')
R_LARB = re.compile(r'NORD2 FB IOMMU: probe: larb(\d) 0x380\.\. ((?:[0-9a-f]+ ?){15})')


def judge(text):
    segs, cur = [], None
    probe = {'lines': 0}

    def new(k, idx):
        return {'phase': idx, 'iommu': k, 'maps': 0, 'walk_bad': 0, 'refused_maps': 0, 'above4g_maps': 0, 'scattered_maps': 0, 'ports_on': 0,
                'ports_off': 0, 'faults': 0, 'auto_disable': 0, 'refused_boot': 0, 'hw_auto_disable': 0, 'cnt0': None, 'cnt1': None,
                'hw0': None, 'hw1': None, 'test_iova': 0, 'armed': 0}
    cur = new(None, 0)
    last_cnt = last_hw = None
    for line in text.splitlines():
        m = R_PH.search(line)
        if m:
            if cur['phase'] or cur['maps'] or cur['faults']:
                segs.append(cur)
            cur = new(int(m.group(3)), int(m.group(1)))
            cur['cnt0'] = last_cnt; cur['hw0'] = last_hw
            continue
        if 'NORD2 FB IOMMU: probe:' in line:
            probe['lines'] += 1
            m = R_LARB.search(line)
            if m:
                v = [int(x, 16) for x in m.group(2).split()]
                probe['larb%s_p6' % m.group(1)] = v[6]
                probe['larb%s_mmu_en_ports' % m.group(1)] = [i for i, x in enumerate(v) if x & 1]
            m = re.search(r'gated of smi_common/gals/infra/iommu: ([0-9a-f]+)\) GICD_ISENABLER10 [0-9a-f]+ \(spi300 m4u0 (\w+)\)', line)
            if m:
                probe['cg_gated'] = m.group(1); probe['gic_spi300'] = m.group(2)
            m = re.search(r'pt_base ([0-9a-f]+) .*ctrl ([0-9a-f]+) \(4gb bit17 (\d)\) tfrp ([0-9a-f]+) int0 ([0-9a-f]+) int_main ([0-9a-f]+)', line)
            if m and 'pt_base_first' not in probe:
                probe.update(pt_base_first=m.group(1), ctrl=m.group(2), bit17=int(m.group(3)), tfrp_first=m.group(4), int_main_first=m.group(6))
            if 'M4U0 clock gated' in line:
                probe['m4u_clock_gated'] = True
        m = R_CNT.search(line)
        if m:
            last_cnt = [int(x) for x in m.groups()]; cur['cnt1'] = last_cnt
        m = R_HW.search(line)
        if m:
            last_hw = (int(m.group(2)), int(m.group(3))); cur['hw1'] = last_hw
        m = R_MAP.search(line)
        if m:
            cur['maps'] += 1
            if m.group(9) == 'BAD': cur['walk_bad'] += 1
            if m.group(8) == '1': cur['above4g_maps'] += 1
            if int(m.group(5)) > 1: cur['scattered_maps'] += 1
        if 'NORD2 FB IOMMU: map id' in line and 'refused at page' in line: cur['refused_maps'] += 1
        if 'NORD2 FB IOMMU: OVL ports MMU_EN ON' in line: cur['ports_on'] += 1
        if 'NORD2 FB IOMMU: OVL ports MMU_EN off' in line: cur['ports_off'] += 1
        if 'NORD2 FB IOMMU: fault' in line: cur['faults'] += 1
        if 'NORD2 FB IOMMU: AUTO-DISABLE' in line: cur['auto_disable'] += 1
        if 'NORD2 FB IOMMU: REFUSED' in line: cur['refused_boot'] += 1
        if 'NORD2 FB IOMMU: armed:' in line: cur['armed'] += 1
        if 'NORD2 FB HW: AUTO-DISABLE' in line: cur['hw_auto_disable'] += 1
        if 'NORD2 FB IOMMU: test slot: through the IOMMU' in line: cur['test_iova'] += 1
    segs.append(cur)
    rows, worst = [], 'PASS'
    for s in segs:
        d = lambda i: (s['cnt1'][i] - (s['cnt0'][i] if s['cnt0'] else 0)) if s['cnt1'] else 0
        layers, dry, verify_bad = d(9), d(10), d(7)
        hwf = (s['hw1'][0] - (s['hw0'][0] if s['hw0'] else 0)) if s['hw1'] else 0
        cpuf = (s['hw1'][1] - (s['hw0'][1] if s['hw0'] else 0)) if s['hw1'] else 0
        fail, warn = [], []
        if s['faults']: fail.append('m4u fault x%d' % s['faults'])
        if s['auto_disable']: fail.append('iommu auto-disable')
        if s['refused_boot']: fail.append('iommu refused for the boot')
        if s['walk_bad'] or verify_bad: fail.append('page-table walk BAD')
        k = s['iommu']
        if k is not None and k >= 2 and s['hw_auto_disable']: fail.append('HW auto-disable')
        if k == 1 and (layers or s['ports_on']): fail.append('dry run let a layer through / ports on')
        if k == 1 and not s['maps'] and not dry: warn.append('dry run mapped nothing')
        if k is not None and k >= 2 and not layers: fail.append('no IOVA layer handed out')
        if k is not None and k >= 2 and not hwf: fail.append('no HW frame')
        if s['refused_maps']: warn.append('map refused x%d' % s['refused_maps'])
        v = 'FAIL' if fail else ('WARN' if warn else 'PASS')
        if v == 'FAIL' or (v == 'WARN' and worst == 'PASS'): worst = v
        rows.append(dict(phase=s['phase'], iommu=k, maps=s['maps'], above_4g=s['above4g_maps'], scattered=s['scattered_maps'], layers=layers, dry=dry,
                         hw_frames=hwf, cpu_frames=cpuf, hw_pct=round(100.0 * hwf / (hwf + cpuf), 1) if hwf + cpuf else None, ports_on=s['ports_on'],
                         ports_off=s['ports_off'], test_iova=s['test_iova'], armed=s['armed'], verdict=v, why=fail + warn))
    return {'probe': probe, 'phases': rows, 'verdict': worst}


def show(r):
    p = r['probe']
    print('probe: lines=%d %s' % (p['lines'], ' '.join('%s=%s' % (k, v) for k, v in sorted(p.items()) if k != 'lines')))
    for k in ('larb0_mmu_en_ports', 'larb1_mmu_en_ports'):
        if p.get(k): print('PROBE WARNING: %s has MMU_EN ports %s (0401 refuses to arm)' % (k, p[k]))
    print('%-5s %-5s %5s %6s %6s %7s %6s %9s %9s %6s %5s %5s %4s  %s' % ('phase', 'iommu', 'maps', '>4G', 'scatt', 'layers', 'dry', 'hw_frames', 'cpu_frames',
                                                                     'hw%', 'on', 'off', 'test', 'verdict'))
    for x in r['phases']:
        print('%-5s %-5s %5d %6d %6d %7d %6d %9d %9d %6s %5d %5d %4d  %s %s' % (x['phase'], x['iommu'], x['maps'], x['above_4g'], x['scattered'], x['layers'],
              x['dry'], x['hw_frames'], x['cpu_frames'], x['hw_pct'], x['ports_on'], x['ports_off'], x['test_iova'], x['verdict'], '; '.join(x['why'])))
    print('IOMMU VERDICT: %s' % r['verdict'])


SELF = """NORD2 FB IOMMU: probe: MMSYS_CG_CON1 00000000 (gated of smi_common/gals/infra/iommu: 00000000) GICD_ISENABLER10 00000000 (spi300 m4u0 masked)
NORD2 FB IOMMU: probe: larb0 0x380.. 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
NORD2 FB IOMMU: probe: larb1 0x380.. 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
NORD2 FB IOMMU: probe: m4u0 pt_base 00000000 inv 0 0 0 0 misc 0 dcm 0 wrlen 0 ctrl 00000002 (4gb bit17 0) tfrp 00000000 int0 00000000 int_main 00000000 cpe 0
N2FBHWCTL iommu phase 1/3 iommu=0 rc=0 readback=0 t=90
NORD2 FB HW: mode=3 hw_frames=100 cpu_frames=50 hw_race=0
N2FBHWCTL iommu phase 2/3 iommu=1 rc=0 readback=1 t=210
NORD2 FB IOMMU: armed: tables pa 0xc1230000 ...
NORD2 FB IOMMU: map id 7 11069184 bytes -> iova 0x90000000 pages 2703 segs 811 pa 0x142100000..0x22c0fffff above_4g 1 walk ok (live 1, window 10880 KiB used)
NORD2 FB IOMMU: mode=1 state=1 ports=0 maps=1 hits=40 unmaps=0 evicts=0 live=1 pages=2703 map_fail=0 verify_bad=0 refused=0 layers=0 dry=41 enables=0 disables=0 faults=0 tlb_to=0
NORD2 FB HW: mode=3 hw_frames=200 cpu_frames=90 hw_race=0
N2FBHWCTL iommu phase 3/3 iommu=2 rc=0 readback=2 t=330
NORD2 FB IOMMU: test slot: through the IOMMU (pattern copy 0x1 -> scattered buffer, iova 0x90b00000)
NORD2 FB IOMMU: OVL ports MMU_EN ON: larb1 p6 00000001 larb0 p6 00000001
NORD2 FB IOMMU: mode=2 state=1 ports=1 maps=3 hits=900 unmaps=0 evicts=0 live=3 pages=8000 map_fail=0 verify_bad=0 refused=0 layers=800 dry=41 enables=1 disables=0 faults=0 tlb_to=0
NORD2 FB HW: mode=3 hw_frames=1200 cpu_frames=95 hw_race=0
"""


def selftest():
    r = judge(SELF)
    ph = r['phases']
    assert r['verdict'] == 'PASS', r
    assert [x['iommu'] for x in ph] == [0, 1, 2] and ph[1]['dry'] == 41 and ph[2]['layers'] == 800 and ph[2]['hw_frames'] == 1000, ph
    assert ph[1]['above_4g'] == 1 and ph[1]['scattered'] == 1 and ph[2]['test_iova'] == 1 and r['probe']['gic_spi300'] == 'masked'
    bad = SELF + "NORD2 FB IOMMU: fault (frame done): fault_st1 00000001 l2 0 va 90001010 (iova 0x90001000 translation fault) pa 0\n" \
                 "NORD2 FB IOMMU: AUTO-DISABLE (M4U fault): kern.n2fbiommu 2 -> 0\n"
    r2 = judge(bad); assert r2['verdict'] == 'FAIL' and 'm4u fault x1' in r2['phases'][2]['why'], r2
    leak = SELF.replace('layers=0 dry=41 enables=0', 'layers=5 dry=41 enables=0')
    r3 = judge(leak); assert r3['verdict'] == 'FAIL' and r3['phases'][1]['verdict'] == 'FAIL', r3
    r4 = judge(SELF.replace('NORD2 FB HW: mode=3 hw_frames=1200', 'NORD2 FB HW: mode=3 hw_frames=200')); assert r4['phases'][2]['verdict'] == 'FAIL'
    print('iommu_verdict selftest OK')


if __name__ == '__main__':
    if sys.argv[1:2] == ['--selftest']:
        selftest(); sys.exit(0)
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    text = open(sys.argv[1], errors='replace').read()
    r = judge(text)
    show(r)
    st = [l for l in text.splitlines() if 'N2FBHWCTL iommu stage' in l or 'NORD2 FB IOMMU: stage' in l or 'NORD2 FB IOMMU: STAGE' in l]
    if st:      # 0403 staged arming: the last line is the last step reached (a hang stops the log right after it)
        print('0403 stages (%d lines), last 6:' % len(st))
        for l in st[-6:]: print('   ' + l.strip()[:200])
        r['stages_last'] = st[-1].strip()
        if any('watchdog' in l and 'FAULT' in l for l in st): r['verdict'] = 'FAIL'; print('STAGE FAULT')
    if '--json' in sys.argv:
        json.dump(r, open(sys.argv[sys.argv.index('--json') + 1], 'w'), indent=1)
    sys.exit(1 if r['verdict'] == 'FAIL' else 0)
