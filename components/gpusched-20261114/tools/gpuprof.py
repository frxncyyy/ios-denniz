#!/usr/bin/env python3
"""gpuprof.py (gpusched-20261114 copy: + the 0660 NORD2 GPUPROF CTX/HIST/MAINT/DRV lines, section "T1 kernel telemetry") -- GPU bottleneck profile per phase from a Nord2 xnu-live.log (PRFCNT A..E + JOBS/LAT/GDVFS/FB lines).

  python3 gpuprof.py XNU_LIVE_LOG [--phases 'name:t0-t1,name:t0-t1,...'] [--csv OUT.csv] [--rows]

Log clock: t = cumulative 'NORD2 SYS: win_ms' (as n2perf.py).  A PRFCNT group (A..E) belongs to the SYS window it follows.
Counters (Mesa G77.xml offsets; the kernel sums the 4 L2 slices and the 9 shader cores):
  A: gpu=GPU_ACTIVE js0=JS0_ACTIVE js1=JS1_ACTIVE til=TILER_ACTIVE      B: L2 lookups, EXT_READ_BEATS, EXT_WRITE_BEATS, AR/W stalls
  C: external read latency histogram (beats per 64-cycle bucket)          D: FRAG_ACTIVE EXEC_CORE_ACTIVE FPK_ACTIVE QUADS_RAST WARPS PTILES FMA
  E: TEX flits, descriptor-fetch stalls, TFCH stalls, STARVED_PENDING_DATA_FETCH, BEATS_RD_TEX, BEATS_WR_TIB, BEATS_RD_FTC
Derived per phase (means over the windows that carry counters):
  hw%      GPU_ACTIVE / (window x clock)                 -- hardware busy (any slot)
  drv%     driver 'busy_pm' (a job in flight)            -- in flight minus hw = turnaround/idle gaps between jobs
  js0/js1  JS0_ACTIVE, JS1_ACTIVE share of GPU_ACTIVE    -- >100 % in sum = the two slots overlapped
  frag%    FRAG_ACTIVE per core / GPU_ACTIVE              -- how much of the busy time the cores shade fragments
  exec%    EXEC_CORE_ACTIVE / FRAG_ACTIVE                 -- warps actually executing while a fragment workload is present
  starve%  TEX starved (waiting for texel data) / FRAG_ACTIVE (summed over cores) -- memory-latency-bound texturing
  fma/ex   FMA warp-instructions per exec-active core cycle -- arithmetic density
  ovd      quads x4 / (frames x 1125 x 2436)              -- pixels shaded per displayed pixel per frame (overdraw incl. offscreen passes)
  tiles/f  FRAG_PTILES per presented frame / 10863        -- full-screen-equivalents of 16x16 tiles per frame
  wTIB/f   BEATS_WR_TIB x16 B per frame / 10.96 MB        -- full-screen RGBA8 surfaces written back per frame
  rdGB/s wrGB/s  external (DRAM) read / write bandwidth (beats x 16 B)
  lat_ns   mean external read latency from the histogram (bucket centres, 383+ ignored)
"""
import re, sys, argparse

SCREEN_PX = 1125 * 2436
TILES_FS = ((1125 + 15) // 16) * ((2436 + 15) // 16)
FS_BYTES = SCREEN_PX * 4

KV = re.compile(r'(\w+)=([-\d.]+)')


def kv(s):
    return {k: float(v) for k, v in KV.findall(s)}


def parse(path):
    wins, cur, t = [], None, 0.0
    last_fresh = None
    with open(path, 'rb') as f:
        for raw in f:
            s = raw.decode('latin-1', 'replace').rstrip('\n')
            i = s.find('NORD2 ')
            if i < 0:
                continue
            s = s[i:]
            if s.startswith('NORD2 SYS: win_ms='):
                d = kv(s)
                t += d.get('win_ms', 2000) / 1000.0
                cur = {'t': t}
                if last_fresh is not None:
                    cur['fresh'] = last_fresh
                wins.append(cur)
                continue
            if cur is None:
                continue
            if s.startswith('NORD2 GPUPROF CTX:'):          # 0660: several per window
                m = re.search(r'name=(\S+) pid=(-?\d+) prio=(\d+)', s)
                d = kv(s)
                if m:
                    d['name'] = m.group(1)
                cur.setdefault('CTX', []).append(d)
                continue
            for tag, key in (('NORD2 GPUPROF HIST:', 'HIST'), ('NORD2 GPUPROF MAINT:', 'MAINT'), ('NORD2 GPUPROF DRV:', 'DRV')):
                if s.startswith(tag):
                    cur[key] = kv(s)
                    break
            for tag, key in (('NORD2 PRFCNT A', 'A'), ('NORD2 PRFCNT B', 'B'), ('NORD2 PRFCNT C', 'C'), ('NORD2 PRFCNT D', 'D'),
                             ('NORD2 PRFCNT E', 'E'), ('NORD2 PRFCNT F', 'F'), ('NORD2 PRFCNT G', 'G'), ('NORD2 PRFCNT H', 'H'), ('NORD2 PRFCNT I', 'I'),
                             ('NORD2 GPU JOBS:', 'JOBS'), ('NORD2 GPU LAT:', 'LAT'), ('NORD2 GDVFS:', 'GDVFS'),
                             ('NORD2 GPUCLK:', 'CLK'), ('NORD2 FB PACE:', 'PACE'), ('NORD2 FB CLIENT:', 'CLIENT')):
                if s.startswith(tag):
                    if key == 'CLIENT':
                        m = re.search(r'win_ms=\d+ (\S+?)\[\d+\] shown=(\d+) fresh=(\d+)', s)
                        if m:
                            cur['client'] = m.group(1); cur['fresh'] = int(m.group(3)); last_fresh = cur['fresh']
                    elif key == 'JOBS' or key == 'LAT':
                        d = {}
                        for m in re.finditer(r'(\w+)=(\d+)/(\d+)', s):
                            d[m.group(1)] = float(m.group(2)); d[m.group(1) + '_max'] = float(m.group(3))
                        d.update({k: v for k, v in kv(s).items() if k not in d})
                        m = re.search(r'kinds=c(\d+)/r(\d+)/f(\d+)', s)
                        if m:
                            d['kc'], d['kr'], d['kf'] = map(float, m.groups())
                        cur[key] = d
                    else:
                        cur[key] = kv(s)
                    break
    return wins


def derive(w):
    A, B, C, D, E = (w.get(k) for k in 'ABCDE')
    if not (A and B and D and E):
        return None
    clk = (w.get('CLK') or {}).get('core_khz', 654000.0) * 1000.0
    win = A.get('win_us', 2e6) / 1e6
    cyc = clk * win
    cores = A.get('cores', 9) or 9
    gpu = A['gpu']
    r = {}
    r['hw'] = 100.0 * gpu / cyc
    lat = w.get('LAT') or {}
    r['drv'] = lat.get('busy_pm', 0) / 10.0
    r['js0'] = 100.0 * A['js0'] / max(gpu, 1)
    r['js1'] = 100.0 * A['js1'] / max(gpu, 1)
    r['til'] = 100.0 * A['til'] / max(gpu, 1)
    r['frag'] = 100.0 * D['frag'] / cores / max(gpu, 1)
    r['exec'] = 100.0 * D['exec'] / max(D['frag'], 1)
    r['starve'] = 100.0 * E['texstarve'] / max(D['frag'], 1)
    r['texstall'] = 100.0 * E['texstall'] / max(D['frag'], 1)
    r['fma_ex'] = D['fma'] / max(D['exec'], 1)
    r['cyc_q'] = D['frag'] / max(D['quads'], 1)          # core-cycles of fragment activity per rasterised quad
    r['q_warp'] = D['quads'] / max(D['warps'], 1)        # quads per fragment warp (4 = full 16-thread warp)
    pace = w.get('PACE') or {}
    frames = w.get('fresh') or pace.get('pushes') or 0
    # FB lines cover ~5 s; PRFCNT 2 s: scale frames to the counter window
    fwin = 5.065
    fps = frames / fwin if frames else 0.0
    r['fps'] = fps
    nfr = fps * win
    r['mpix'] = D['quads'] * 4 / win / 1e6
    r['gms_f'] = (gpu / clk * 1000.0) / nfr if nfr >= 1 else float('nan')
    r['ovd'] = D['quads'] * 4 / (nfr * SCREEN_PX) if nfr >= 1 else float('nan')
    r['tiles_f'] = D['ptiles'] / nfr / TILES_FS if nfr >= 1 else float('nan')
    r['wtib_f'] = E['wrtib'] * 16 / nfr / FS_BYTES if nfr >= 1 else float('nan')
    r['rd'] = B['rbeat'] * 16 / win / 1e9
    r['wr'] = B['wbeat'] * 16 / win / 1e9
    r['wstall'] = 100.0 * B['wstall'] / 4 / cyc
    r['tib_share'] = 100.0 * E['wrtib'] / max(B['wbeat'], 1)
    r['tex_share'] = 100.0 * E['rdtex'] / max(B['rbeat'], 1)   # texture beats from L2 (hits+misses) vs DRAM read beats
    if C:
        cent = [(C.get('lat0_127', 0), 64), (C.get('lat128_191', 0), 160), (C.get('lat192_255', 0), 224),
                (C.get('lat256_319', 0), 288), (C.get('lat320_383', 0), 352)]
        n = sum(c for c, _ in cent)
        r['lat_ns'] = (sum(c * m for c, m in cent) / n) / clk * 1e9 if n else float('nan')
    # T1 patch lines (absent in pre-T1 captures): see GPU_PERF_ANALYSIS.md 4.5 for the field names
    F, G, H, I = (w.get(k) for k in 'FGHI')
    if F:
        a0 = max(A['js0'], 1)
        r['js0_wait_read'] = 100.0 * F.get('js0wr', 0) / a0
        r['js0_wait_issue'] = 100.0 * F.get('js0wi', 0) / a0
        r['js0_wait_dep'] = 100.0 * F.get('js0wd', 0) / a0
        r['js0_wait_fin'] = 100.0 * F.get('js0wf', 0) / a0
        r['js2_active'] = 100.0 * F.get('js2', 0) / max(gpu, 1)
    if G:
        r['cull_pct'] = 100.0 * G.get('cull', 0) / max(G.get('tri', 0), 1)
        r['mmu_per_quad'] = G.get('mmu', 0) / max(D['quads'], 1)
    if H:
        r['ezs_kill_pct'] = 100.0 * H.get('ezk', 0) / max(H.get('ezt', 0), 1)
        r['te_tiles_pct'] = 100.0 * H.get('te', 0) / max(D['ptiles'], 1)
        r['blendsh_per_kquad'] = 1000.0 * H.get('blsh', 0) / max(D['quads'], 1)
        r['reg64_warps_pct'] = 100.0 * H.get('r64', 0) / max(D['warps'], 1)
    if I:
        r['tex_l2_miss_pct'] = 100.0 * I.get('rdtexx', 0) / max(E['rdtex'], 1)
        r['ls_reads_per_quad'] = (I.get('lsrf', 0) + I.get('lsrs', 0)) / max(D['quads'], 1)
    jobs = w.get('JOBS') or {}
    r['jobs_s'] = jobs.get('n', 0) / max(jobs.get('win_ms', 2000) / 1000.0, 1e-3)
    r['run_us'] = jobs.get('run_us', 0)
    r['q_us'] = jobs.get('q_us', 0)
    r['hand_us'] = lat.get('hand_us', 0)
    r['irq_us'] = lat.get('irq_us', 0)
    r['depth'] = lat.get('depth', 0)
    r['passes_s'] = lat.get('kf', 0) / max(lat.get('win_ms', 2000) / 1000.0, 1e-3)
    r['gclk'] = clk / 1e6
    return r


COLS = ['gms_f', 'hw', 'drv', 'js0', 'js1', 'til', 'frag', 'exec', 'starve', 'texstall', 'fma_ex', 'cyc_q', 'q_warp', 'fps', 'mpix', 'ovd', 'tiles_f', 'wtib_f',
        'rd', 'wr', 'tib_share', 'tex_share', 'wstall', 'lat_ns', 'jobs_s', 'passes_s', 'run_us', 'hand_us', 'irq_us', 'q_us', 'depth', 'gclk']


T1_COLS = ['js0_wait_read', 'js0_wait_issue', 'js0_wait_dep', 'js0_wait_fin', 'js2_active', 'cull_pct', 'mmu_per_quad',
           'ezs_kill_pct', 'te_tiles_pct', 'blendsh_per_kquad', 'reg64_warps_pct', 'tex_l2_miss_pct', 'ls_reads_per_quad']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--phases', default='idle:0-36,home+applib:36-226,Maps:226-306,Magnifier:308-350,Safari:352-432,'
                                         'Photos/switcher:432-468,idle-after:468-580,idle-hot:580-745')
    ap.add_argument('--csv')
    ap.add_argument('--rows', action='store_true')
    a = ap.parse_args()
    wins = parse(a.log)
    rows = []
    for w in wins:
        r = derive(w)
        if r:
            r['t'] = w['t']
            rows.append(r)
    phases = []
    for p in a.phases.split(','):
        name, rng = p.rsplit(':', 1)
        t0, t1 = map(float, rng.split('-'))
        phases.append((name, t0, t1))
    cols = COLS + [c for c in T1_COLS if any(c in r for r in rows)]
    if a.csv:
        with open(a.csv, 'w') as f:
            f.write('t,' + ','.join(cols) + '\n')
            for r in rows:
                f.write('%.1f,' % r['t'] + ','.join('%.3f' % r.get(c, float('nan')) for c in cols) + '\n')
    print('windows with counters: %d of %d' % (len(rows), len(wins)))
    hdr = '%-16s %3s ' % ('phase', 'n') + ' '.join('%7s' % c for c in cols)
    print(hdr)
    for name, t0, t1 in phases:
        sel = [r for r in rows if t0 <= r['t'] < t1]
        if not sel:
            continue
        def mean(c):
            v = [r[c] for r in sel if r.get(c) == r.get(c)]
            return sum(v) / len(v) if v else float('nan')
        print('%-16s %3d ' % (name[:16], len(sel)) + ' '.join('%7.2f' % mean(c) for c in cols))
    # 0660: T1 kernel telemetry per phase (all windows of the phase, counters or not)
    if any('MAINT' in w or 'CTX' in w for w in wins):
        print('\nT1 kernel telemetry (0660) per phase: GPU ms per s by process (top), job run-time histogram (both slots), CPU clean MiB/s by reason, driver averages')
        for name, t0, t1 in phases:
            ws = [w for w in wins if t0 <= w['t'] < t1]
            if not ws:
                continue
            dur = max(1e-3, sum(w.get('LAT', {}).get('win_ms', 2000) for w in ws) / 1000.0)
            ctx = {}
            for w in ws:
                for d in w.get('CTX', []):
                    e = ctx.setdefault(d.get('name', '?'), [0.0, 0.0, 0.0, 0])
                    e[0] += d.get('run_us', 0); e[1] += d.get('q_us', 0); e[2] = max(e[2], d.get('qmax_us', 0)); e[3] += d.get('jobs', 0)
            top = sorted(ctx.items(), key=lambda kv_: -kv_[1][0])[:4]
            tops = ' | '.join('%s %.1fms/s q%.2fms max%.1fms' % (n, e[0] / 1000 / dur, (e[1] / e[3] / 1000) if e[3] else 0, e[2] / 1000) for n, e in top)
            hist = [0] * 7
            for w in ws:
                h = w.get('HIST', {})
                for i, k in enumerate(('100', '250', '500', '1k', '4k', '16k', 'big')):
                    hist[i] += h.get('s0_' + k, 0) + h.get('s1_' + k, 0)
            cl = {}
            for w in ws:
                for k, v in w.get('MAINT', {}).items():
                    cl[k] = cl.get(k, 0) + v
            mib = lambda k: cl.get(k, 0) / 1024.0 / dur
            drv = [w['DRV'] for w in ws if 'DRV' in w]
            avg = lambda k: (sum(d.get(k, 0) for d in drv) / len(drv)) if drv else float('nan')
            print('%-16s %s' % (name[:16], tops or '-'))
            print('%-16s hist(us) <100:%d <250:%d <500:%d <1k:%d <4k:%d <16k:%d >=16k:%d' % ('', *hist))
            print('%-16s clean MiB/s fullhard %.1f legacy %.1f implicit %.1f dirty %.1f full %.1f presubmit %.1f, max %.1f MiB, invalidate %.1f MiB/s; full jobs %d trunc %d precise %d of %d' % (
                  '', mib('cl_fullhard'), mib('cl_legacy'), mib('cl_implicit'), mib('cl_dirty'), mib('cl_full'), mib('cl_presub'),
                  max([w.get('MAINT', {}).get('cl_max_kib', 0) for w in ws] or [0]) / 1024.0, mib('inv_kib'),
                  cl.get('full', 0), cl.get('trunc', 0), cl.get('precise', 0), cl.get('jobs', 0)))
            print('%-16s lock wait %.0f us hold %.0f us, bottom-half wake %.0f us poll %.0f us, irq cpu %s bh cpu %s, prio take-backs %d aged %d reserve %d' % (
                  '', avg('lock_wait_avg_us'), avg('lock_hold_avg_us'), avg('bh_wake_avg_us'), avg('bh_poll_avg_us'),
                  drv[-1].get('irq_cpu', '?') if drv else '?', drv[-1].get('bh_cpu', '?') if drv else '?',
                  sum(d.get('evict', 0) for d in drv), sum(d.get('aged', 0) for d in drv), sum(d.get('reserve', 0) for d in drv)))
    if a.rows:
        for r in rows:
            print('%7.1f ' % r['t'] + ' '.join('%7.2f' % r.get(c, float('nan')) for c in cols))
    return 0


if __name__ == '__main__':
    sys.exit(main())
