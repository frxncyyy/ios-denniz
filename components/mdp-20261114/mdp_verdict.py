#!/usr/bin/env python3
"""MDP log reader for 0670 (probe), 0671 (stages 1-3), 0672 (AppleM2ScalerCSCDriver) and 0673 (bring-up stage P).
usage: python3 mdp_verdict.py <xnu-live.log> [--quiet]

Probe verdict (stage 0): MDP_READY / MDP_CLOCKS_CLOSED / MDP_DOMAIN_OFF / MDP_NOT_SEEN / MDP_RESTORE_FAILED
Stage verdicts: S1_PASS / S1_FAIL(code) / S1_NOT_RUN, S2 pass/fail masks with test names, S3_PASS / S3_FAIL(code) / S3_NOT_RUN,
SERVICE_REGISTERED / SERVICE_NOT_REGISTERED, transforms done / refused / failed, aborts and guard hits.
Codes (kern.n2mdp_s1 / s3): 1 pass; S1: 3 compile, 4 timeout (frame-done GCE event never came), 5 guard band overwritten,
6 compare mismatch, 9 gates/pool; S3: 10-14 M4U1 arm (12/13 = another master translates), 20-22 allocation/map, 24 timeout,
25 mismatch or IOVA fault.
Next step for the camera: python3 gpu/build/camprev-20261114/camprev_check.py LOG  -> the VTMTS (VTMTSComputeFunction) dispatch
count should drop when VideoToolbox uses the service ('NORD2 M2: transform ... done' lines present).
"""
import re, sys

TESTS = ['copy256', 'rot90', 'nv12v', 'nv12f', 'nv12rot90', 'scaledown', 'scaleup', 'rot180', 'rot270', 'flip', 'tilecopy',
         'tilerot90', 'scalerot', 'rgbscale1', 'nv12scale3t', 'nv12scale3o', 'rgbscale3t', 'rgbscale1s', 'rgbscale3s', 'nv2nv', 'nv2nvrot90', 'nv2nvrot180', 'nv2nvrot270', 'nv2nvcam90']   # 0686; 0682 (last 4 = S2 DIAG); 0681 order (scaletile removed; 0678-0680 logs had it last) (q10jmdp2..7 logs used the old order)

def names(mask):
    return ','.join(t for i, t in enumerate(TESTS) if mask & (1 << i)) or '-'

def main(path, quiet=False):
    lines = [l.rstrip('\n') for l in open(path, errors='replace') if 'NORD2 MDP' in l or 'NORD2 M2' in l]
    if not quiet:
        for l in lines:
            print(l[l.find('NORD2'):])
    verdicts = [l for l in lines if 'NORD2 MDP VERDICT' in l]
    if not verdicts:
        print('MDP_NOT_SEEN'); return 1
    kv = [dict(re.findall(r'(\w+)=(\S+)', v[v.find('VERDICT'):])) for v in verdicts]
    if any('DIFFERENT from found' in l for l in lines):
        print('MDP_RESTORE_FAILED'); return 2
    best = None
    for d in kv:
        st = int(d.get('state', '0'), 16)
        if not (st & 2) or (st & 4) or (st & 8):
            continue
        if d.get('apb') == 'open' or (st & 0x80 and st & 0x40):
            best = 'MDP_READY'
        elif best is None:
            best = 'MDP_CLOCKS_CLOSED'
    print('PROBE:', best or 'MDP_DOMAIN_OFF')
    for d in kv:
        print('  lvl=%s power=%s prot=%s mdp_sel=%s apb=%s smi=%s cg0=%s cg1=%s engines_read=%s larb2_mmu=%s m4u1_pt=%s gced_thr=%s gcem_thr=%s'
              % tuple(d.get(k, '?') for k in ('lvl', 'power', 'prot', 'mdp_sel', 'apb', 'smi', 'cg0', 'cg1', 'engines_read',
                                               'larb2_mmu', 'm4u1_pt', 'gced_thr', 'gcem_thr')))
    pw = [l for l in lines if 'NORD2 MDP PWRON: result' in l]
    if pw:
        print('PWRON (0673 bring-up):', pw[-1][pw[-1].find('result'):])
        for l in lines:
            if 'NORD2 MDP PWRON:' in l and any(k in l for k in ('STUCK', 'released', 'shared with', 'gates', 're-probe', 'after release')):
                print('  ' + l[l.find('PWRON:') + 7:])
    else:
        print('PWRON: none logged (image without 0673, or kern.n2mdp_stages bit 0x100 off / kill file)')
    st = [l for l in lines if 'NORD2 MDP STAGES:' in l]
    if not st:
        print('STAGES: none logged (image without 0671?)')
    for l in st:
        m = re.search(r'stage (\d+) caps (0x[0-9a-f]+|0) s1 (\d+) s2 pass (0x[0-9a-f]+|0) fail (0x[0-9a-f]+|0) s3 (\d+) jobs (\d+) fail (\d+) timeouts (\d+) max (\d+)', l)
        if not m:
            print('STAGES:', l[l.find('STAGES:') + 8:]); continue
        stage, caps, s1, p2, f2, s3 = int(m.group(1)), int(m.group(2), 16), int(m.group(3)), int(m.group(4), 16), int(m.group(5), 16), int(m.group(6))
        print('STAGES: highest passed %d, caps %#x' % (stage, caps))
        print('  S1:', 'S1_PASS' if s1 == 1 else ('S1_NOT_RUN' if s1 == 0 else 'S1_FAIL(%d)' % s1))
        print('  S2: pass [%s] fail [%s]' % (names(p2), names(f2)))
        print('  S3:', 'S3_PASS' if s3 == 1 else ('S3_NOT_RUN' if s3 == 0 else 'S3_FAIL(%d)' % s3))
        print('  jobs %s, failed %s, timeouts %s, max hw %s us' % (m.group(7), m.group(8), m.group(9), m.group(10)))
    for l in lines:   # 0674: larb2 bring-up, pipe dumps around the first start and at a timeout, destination write count
        if any(k in l for k in ('LARB2 configured', 'LARB2: ', 'DIAG GO+50us', 'DIAG timeout', 'failed (timeout)', 'failed (poll)', 'tile done after', ' retry ', 'main-buffer lines', 'S2 DIAG', 'NORD2 MDP S3', 'enabled (', 'stopped at the first failing', 'mismatch detail', 'BELOW the destination')):
            print('  ' + l[l.find('NORD2'):][:400])
    for key in ('MISMATCH', 'ABORT', 'guard bytes overwritten', 'R/B order calibrated', 'IOVA fault'):
        n = sum(1 for l in lines if key in l)
        if n:
            print('  %-24s %d line(s)' % (key, n))
    reg = [l for l in lines if 'AppleM2ScalerCSCDriver registered' in l]
    notreg = [l for l in lines if 'NORD2 M2: not registered' in l or 'no service hook' in l]
    print('SERVICE:', 'SERVICE_REGISTERED' if reg else ('SERVICE_NOT_REGISTERED' + (' (%s)' % notreg[-1][notreg[-1].find('NORD2'):] if notreg else '')))
    done = [l for l in lines if re.search(r'NORD2 M2: transform .* done in', l)]
    refused = [l for l in lines if 'NORD2 M2:' in l and 'refused' in l]
    notdone = [l for l in lines if 'NORD2 M2: transform' in l and 'not done' in l]
    print('  transforms logged: done %d (first 4 + every 1024th are logged), refused %d, not done %d' % (len(done), len(refused), len(notdone)))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], '--quiet' in sys.argv))
