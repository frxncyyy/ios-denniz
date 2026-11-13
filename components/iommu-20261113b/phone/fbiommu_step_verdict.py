#!/usr/bin/env python3
"""Verdict of one display-IOMMU step transaction (payload-fbiommu-t1..t6, kernel 0401+0403).
  fbiommu_step_verdict.py STEP XNU_LIVE_LOG [--usb USB_LOG] [--json OUT]      STEP = t1..t6 | t1idle | t1m3 | poolonly | sub01..sub80
Prints every check (ok/MISSING/BAD), the last IOMMU line reached, then 'FBIOMMU STEP tN VERDICT: PASS|FAIL|INCOMPLETE'.
FAIL = a failure line (REFUSED, fault, AUTO-DISABLE, walk BAD, FALLBACK, underrun increase, hwInit refused because of MMU_EN) or the
console lost before 'N2FBHWCTL done' (hang: the last 'stage step:' line names the write).  INCOMPLETE = no failure line but a success
line missing (e.g. capture too short).  Exit 0 only on PASS.  T5 also needs the PHOTO (both colour strips R,G,B): not checkable here.
"""
import re, sys, json

STAGE_LINES = {      # 0403 sweet_fb_iommu_hw.h n2io_arm_locked / n2io_sysctl_stage
    'step PT_BASE': r'NORD2 FB IOMMU: stage step: write M4U0 PT_BASE',
    'step TFRP': r'NORD2 FB IOMMU: stage step: write M4U0 TFRP',
    'step TLB': r'NORD2 FB IOMMU: stage step: M4U0 TLB invalidate all',
    'STAGE 1 done fault_st1 0': r'NORD2 FB IOMMU: STAGE 1 done: PT_BASE read [0-9a-f]{8} fault_st1 00000000',
    'stage 1 alive': r'N2FBHWCTL iommu stage 1/\d+ value=1 alive',
}
FAIL_ANY = {
    'refused': r'NORD2 FB IOMMU: (REFUSED|STAGE \d REFUSED)|NORD2 FB IOMMU: MODE .* REFUSED',
    'fault': r'NORD2 FB IOMMU: fault \(|\(FAULT logged above\)',
    'auto-disable': r'NORD2 FB IOMMU: AUTO-DISABLE|N2FBHWCTL iommu phase_end .* AUTO-DISABLE',
    'walk BAD': r'NORD2 FB IOMMU: map id .* walk BAD',
    'fallback': r'N2FBHWCTL phase_end .* FALLBACK',
    'hwInit refused (MMU_EN)': r'display M4U enabled on an OVL port',
    'stage sysctl error': r'N2FBHWCTL iommu stage \d+/\d+ value=\d rc=[1-9]',
    'sub sysctl error': r'N2FBHWCTL iommu sub \d+/\d+ value=\d+ rc=[1-9]|NORD2 FB IOMMU: SUB 0x[0-9a-f]+ (FAILED|REFUSED)',
    'sub fault latched': r'NORD2 FB IOMMU: SUB 0x[0-9a-f]+ done: .* fault_st1 (?!00000000)[0-9a-f]{8}',
    'iommu sysctl error': r'N2FBHWCTL iommu phase \d+/\d+ iommu=\d rc=[1-9]',
    'job config error': r'N2FBHWCTL error',
    'test refused': r'NORD2 FB HW: test plan refused|NORD2 FB IOMMU: test slot: REFUSED',
    'panic': r'panic\(cpu',
}

SUBV = {'sub01': [1], 'sub02': [2], 'sub04': [4], 'sub08': [8], 'sub10': [16], 'sub80': [14, 128], 'subseq': [1, 2, 4, 8, 128], 'subx2': [1, 2, 4, 8]}
def need(step):
    n = {'job done': r'N2FBHWCTL done final=%d' % (3 if step == 't1m3' else 0)}
    if step == 'ctl0': return n
    if step in ('t1idle', 't1m3'): step = 't1'
    if step == 'poolonly':
        n['pool mapped'] = r'NORD2 FB POOL: pa 0xc0000000 .* -> ready'; n['pool set 1'] = r'N2FBHWCTL pool set=1 rc=0 readback=1'
    if step in SUBV:
        for i, v in enumerate(SUBV[step]):
            m = len(SUBV[step])
            n['sub %#x rc=0' % v] = r'N2FBHWCTL iommu sub %d/%d value=%d rc=0' % (i + 1, m, v)
            n['SUB %#x done' % v] = r'NORD2 FB IOMMU: SUB %#x done: ' % v
            n['sub %#x alive' % v] = r'N2FBHWCTL iommu sub %d/%d value=%d alive' % (i + 1, m, v)
        if step in ('sub80', 'subseq'): n['undone: PT_BASE 0 TFRP 0'] = r'NORD2 FB IOMMU: SUB 0x80 done: PT_BASE 00000000 TFRP 00000000'
        if step == 'sub01': n['prep, M4U0 untouched'] = r'NORD2 FB IOMMU: SUB prep: tables pa 0x[0-9a-f]+ .*M4U0 untouched'
        if step == 'sub10': n['dump PWR_STATUS'] = r'NORD2 FB IOMMU: SUB dump: PWR_STATUS'
    if step in ('t1', 't2', 't3'):
        n.update(STAGE_LINES)
        n['armed PT_BASE reads back'] = None      # special
    if step == 't2':
        n['port larb0 ON x3'] = (r'NORD2 FB IOMMU: stage port larb0 \(OVL1_2L, right half\) p6 MMU_EN ON: [0-9a-f]{8} -> [0-9a-f]*[13579bdf]\b', 3)
        n['watchdog 0x2 survived x3'] = (r'NORD2 FB IOMMU: stage watchdog after \d+ ms: ports 0x2 off, fault_st1 00000000; display survived', 3)
    if step == 't3':
        n['port larb1 ON x3'] = (r'NORD2 FB IOMMU: stage port larb1 \(OVL0_2L, left half\) p6 MMU_EN ON: [0-9a-f]{8} -> [0-9a-f]*[13579bdf]\b', 3)
        n['port larb0 ON x3'] = (r'NORD2 FB IOMMU: stage port larb0 \(OVL1_2L, right half\) p6 MMU_EN ON: [0-9a-f]{8} -> [0-9a-f]*[13579bdf]\b', 3)
        n['watchdog 0x3 survived x3'] = (r'NORD2 FB IOMMU: stage watchdog after \d+ ms: ports 0x3 off, fault_st1 00000000; display survived', 3)
    if step in ('t4', 't5', 't6'):
        k = {'t4': 1, 't5': 2, 't6': 3}[step]
        n['iommu set %d' % k] = r'N2FBHWCTL iommu phase 1/1 iommu=%d rc=0 readback=%d' % (k, k)
        n['MODE new=%d armed' % k] = r'NORD2 FB IOMMU: MODE old=\d new=%d why=sysctl state=1 \(armed\)' % k
        n['armed line'] = r'NORD2 FB IOMMU: armed: tables pa'
        n['map above 4G walk ok'] = r'NORD2 FB IOMMU: map id \d+ \d+ bytes -> iova 0x[0-9a-f]+ pages \d+ segs \d+ pa .* above_4g 1 walk ok'
        n['phase_end readback %d' % k] = r'N2FBHWCTL iommu phase_end 1/1 iommu=%d readback=%d t=\d+' % (k, k)
        n['iommu back to 0'] = r'N2FBHWCTL iommu done iommu=0 rc=0'
        n['stats faults=0'] = None      # special: last stats line during the phase
    if step in ('t5', 't6'):
        n['ports MMU_EN ON'] = r'NORD2 FB IOMMU: OVL ports MMU_EN ON'
        n['layers > 0'] = None
    if step == 't5':
        n['hwtest 6 set'] = r'N2FBHWCTL iommu hwtest 6 rc=0'
        n['test slot via IOMMU'] = r'NORD2 FB IOMMU: test slot: through the IOMMU'
        n['test 6 frame shown'] = r'NORD2 FB HW: test 6 step \d+ frame .* holding \d+ ms \(photo now\)'
    if step == 't4':
        n['dry > 0'] = None
    return n

def timing(logp, stepsp, lost):
    """Host time of the sub/stage lines (via the scenario's log_offset records) and of the console loss."""
    import os
    if not os.path.exists(stepsp): print('timing: no %s' % stepsp); return
    recs = [json.loads(l) for l in open(stepsp) if l.strip()]
    if not recs: print('timing: no scenario records'); return
    raw = open(logp, 'rb').read(); marks, off = [], 0
    for l in raw.split(b'\n'):
        off += len(l) + 1; t = l.decode('utf-8', 'replace')
        m = re.search(r'N2FBHWCTL iommu (sub|stage) (\d+)/(\d+) value=(\d+) rc=(-?\d+) t=(\d+)', t)
        if m: g = m.groups(); marks.append((off, '%s %s/%s value=%#x rc=%s job_t=%s' % (g[0], g[1], g[2], int(g[3]), g[4], g[5])))
        elif 'N2FBHWCTL done' in t: marks.append((off, 'job done'))
    def host_t(o):
        for r in recs:
            if r['log_offset'] >= o: return r['start']
        return None
    ok = [r for r in recs if r['rc'] == 0]; t_last_ok = (ok[-1]['start'] + ok[-1]['seconds']) if ok else None
    bad = [r for r in recs if r['rc'] != 0 and (not ok or r['start'] > ok[-1]['start'])]
    t_loss = bad[0]['start'] if bad else None
    t0 = recs[0]['start']
    for o, what in marks:
        h = host_t(o); print('  t=%6s s  %s' % ('%.0f' % (h - t0) if h else '<scen', what))
    if t_loss and lost:
        before = [(host_t(o), w) for o, w in marks if (host_t(o) or 0) <= t_loss and w != 'job done']
        last = before[-1] if before else (None, 'none (before the first sub/stage line)')
        print('LOSS at t=%.0f s (last phone answer t=%.0f s); last step logged before it: %s%s' % (t_loss - t0, (t_last_ok or t0) - t0, last[1],
              ', %.0f s earlier' % (t_loss - last[0]) if last[0] else ''))
    elif not lost: print('NO LOSS: phone answered until the end (t=%.0f s): ALL OK' % ((t_last_ok or t0) - t0))

def main(a):
    if len(a) < 2 or a[0] not in ('t1', 't2', 't3', 't4', 't5', 't6', 't1idle', 't1m3', 'poolonly', 'ctl0') + tuple(SUBV): print(__doc__); return 2
    step, logp = a[0], a[1]
    usb = a[a.index('--usb') + 1] if '--usb' in a else None
    lines = open(logp, 'rb').read().decode('utf-8', 'replace').splitlines()
    res, fails, missing = {}, [], []
    start = next((i for i, l in enumerate(lines) if 'N2FBHWCTL start' in l), None)
    if start is None: print('no N2FBHWCTL start line: payload not installed or job not run'); print('FBIOMMU STEP %s VERDICT: INCOMPLETE' % step); return 1
    body = lines[start:]
    for name, rx in FAIL_ANY.items():
        hit = [l for l in body if re.search(rx, l)]
        if hit: fails.append('%s: %s' % (name, hit[0].strip()[:200]))
    stats = [l[l.index('NORD2 FB IOMMU: mode='):] for l in body if 'NORD2 FB IOMMU: mode=' in l and ' maps=' in l]
    def stat(l, k):
        m = re.search(r'\b%s=(\d+)' % k, l); return int(m.group(1)) if m else -1
    for name, rx in need(step).items():
        if rx is None:
            if name == 'armed PT_BASE reads back':
                m = [re.search(r'PT_BASE [0-9a-f]{8} -> ([0-9a-f]{8}) \(read ([0-9a-f]{8})\)', l) for l in body if 'NORD2 FB IOMMU: armed:' in l]
                ok = bool(m) and m[0] is not None and m[0].group(1) == m[0].group(2) and m[0].group(1) != '00000000'
            elif name == 'stats faults=0':
                live = [l for l in stats if stat(l, 'state') == 1]
                ok = bool(live) and all(stat(l, 'faults') == 0 and stat(l, 'verify_bad') == 0 for l in live)
                if live and not ok: fails.append('stats: ' + [l for l in live if stat(l, 'faults') or stat(l, 'verify_bad')][0][:200])
            elif name == 'layers > 0': ok = any(stat(l, 'layers') > 0 for l in stats)
            elif name == 'dry > 0': ok = any(stat(l, 'dry') > 0 for l in stats)
            res[name] = ok
        else:
            rx, cnt = rx if isinstance(rx, tuple) else (rx, 1)
            res[name] = sum(1 for l in body if re.search(rx, l)) >= cnt
        if not res[name]: missing.append(name)
    # display alive: underrun never increases after the start, and OVL stats keep coming after the job's last action
    ovl = [(i, int(m.group(1))) for i, l in enumerate(lines) for m in [re.search(r'NORD2 FB OVL: frames=\d+ dsi_underrun=(\d+)', l)] if m]
    base = max([v for i, v in ovl if i < start] or [0])
    up = [v for i, v in ovl if i >= start and v > base]
    if up: fails.append('dsi_underrun rose %d -> %d after the job start' % (base, max(up)))
    done = next((i for i, l in enumerate(lines) if 'N2FBHWCTL done' in l), None)
    after = sum(1 for i, v in ovl if done is not None and i > done)
    res['display alive (>=3 OVL stats after done)'] = after >= 3
    if after < 3: missing.append('display alive (>=3 OVL stats after done)')
    lost = bool(usb) and 'Capture ended early' in open(usb, errors='replace').read()
    import os
    rj = os.path.join(os.path.dirname(os.path.abspath(logp)), 'result.json')
    stop = json.load(open(rj)).get('stop_reason') if os.path.exists(rj) else None
    terr = json.load(open(rj)).get('transport_error') if os.path.exists(rj) else None
    if stop is not None: print('capture stop_reason %s%s' % (stop, (' (%s)' % terr) if terr else ''))
    lost = lost or stop == 'usb_error'
    if lost: fails.append('console lost (%s) %s N2FBHWCTL done: hang/reset during the capture -- a normal end is stop_reason deadline'
                          % (terr or 'Capture ended early', 'before' if done is None else 'after'))
    for k, v in res.items(): print('  %-42s %s' % (k, 'ok' if v else 'MISSING'))
    for f in fails: print('  BAD ' + f)
    io = [l.strip() for l in body if 'NORD2 FB IOMMU' in l or 'N2FBHWCTL iommu' in l]
    print('last IOMMU line: %s' % (io[-1][:220] if io else '-'))
    stg = [l.strip() for l in body if 'stage step:' in l or 'SUB ' in l and 'NORD2 FB IOMMU' in l]
    if lost and stg: print('last stage step logged (the loss may be much later): %s' % stg[-1][:200])
    if '--steps' in a: timing(logp, a[a.index('--steps') + 1], lost)
    if step == 'subx2':
        done = {v: any(re.search(r'NORD2 FB IOMMU: SUB %#x done: ' % v, l) for l in body) for v in (1, 2, 4, 8)}
        print('subx2: survived to the end of the capture: %s | prep %s PT_BASE %s TFRP %s invalidate %s' % ('NO (console lost)' if lost else 'YES',
              *['done' if done[v] else 'not reached' for v in (1, 2, 4, 8)]))
    v = 'FAIL' if fails else ('INCOMPLETE' if missing else 'PASS')
    if step == 't5' and v == 'PASS': print('  NOTE: PASS needs the PHOTO too (both strips R,G,B during the test 6 hold)')
    print('FBIOMMU STEP %s VERDICT: %s' % (step, v))
    if '--json' in a: json.dump({'step': step, 'verdict': v, 'checks': res, 'fails': fails, 'last': io[-1] if io else None}, open(a[a.index('--json') + 1], 'w'), indent=1)
    return 0 if v == 'PASS' else 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
