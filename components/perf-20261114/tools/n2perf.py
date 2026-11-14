#!/usr/bin/env python3
"""n2perf.py - turn a Nord2 USB console capture (xnu-live.log) into a performance timeline + stutter report.

usage: n2perf.py LOG [--csv OUT.csv] [--from T] [--to T] [--all] [--events-only] [--json OUT.json]
  T = seconds on the log clock (see below).  --all prints every 2 s row (default: rows with activity or an event).

Clock: console lines carry no timestamps.  The kernel prints 'NORD2 SYS: win_ms=..' every ~2 s (Nord2GPU.cpp), so the log clock
t = sum of the SYS win_ms seen so far; every other line belongs to the SYS window it follows (resolution 2 s; 5 s FB windows are
attributed to the 2 s row where they end).  If the capture has 'NORD2 JANK:' / 'NORD2 PERF T:' lines (0621) those carry the
kernel uptime in ms and are used for exact event times.

Sources parsed (all existing kernel/userland lines; cumulative counters are turned into per-window deltas):
  SYS (CPU busy per core, pageins/faults/zerofill/cow/decomp)  DVFS DECISION (CPU/GPU clocks, utils, class, therm, battery)
  GDVFS (GPU cur/want/cap kHz, busy avg/max)  GPU JOBS / JOBS2 / LAT / LAT2 (queue / run / total latency, handler, bottom half)
  GPU AS (dynamic address spaces: binds/evict/waits, cumulative)  GPU FENCE  PRFCNT A (HW busy, js0/js1)  MEM / MEM SURF / MEM GPU
  MEMGOV (purge/kill)  FB PRESENT / SEEN / PACE / CLIENT / ARRIVAL / WAITS / SWAPWAIT / HW / IOMMU / LOCK  INPUT RATE (touch)
  GPU USER (userland Nord2Metal: SLOW / W warnings / PERF)  exec <proc> (process launches; respawn loops)  NORD2 HANG / panics
  0620/0621 lines: NORD2 JANK (one line per present gap > threshold while active), NORD2 PERF T (1 s present/input timeline),
  NORD2 INPUT PACE (touch sampler).
"""
import re, sys, json, argparse, collections

SPLIT = re.compile(r'(?=NORD2 |SWEET |N2MNLD |^exec |panic\()')
KV = re.compile(r'([A-Za-z_][A-Za-z0-9_%\[\]/.<=+-]*?)=(-?[0-9][0-9./:-]*)')

def kv(s):
    d = {}
    for k, v in KV.findall(s):
        d.setdefault(k, v)
    return d

def num(v, default=0.0):
    try:
        return float(v.split('/')[0].split('(')[0])
    except Exception:
        return default

def pair(v):
    """'avg/max' -> (avg, max)"""
    try:
        a = v.split('/')
        return float(a[0]), float(a[1].split('(')[0]) if len(a) > 1 else float(a[0])
    except Exception:
        return 0.0, 0.0

def lst(v):
    try:
        return [float(x) for x in v.split('/')]
    except Exception:
        return []

CUMUL = {   # line -> keys that are cumulative counters (delta per window)
    'GPU AS': ['binds', 'evict', 'waits', 'bindfail', 'refused', 'mmu_faults', 'resets', 'flushfail'],
    'FB IOMMU': ['maps', 'unmaps', 'evicts', 'map_fail', 'faults', 'tlb_to', 'refused', 'verify_bad', 'layers'],
    'FB SWAPWAIT': ['calls', 'not_ready', 'blocked', 'timeouts', 'swaps_dropped', 'late_held', 'late_forced'],
    'FB HW': ['hw_frames', 'cpu_frames', 'race', 'fence_skip', 'late_write'],
    'GPU FENCE': ['miss'],
    'SURF USE': ['table-full'],
}

RESPAWN_PROCS = {'xpcproxy', 'lskdd', 'bluetoothd', 'fairplayd.H2', 'wifip2pd'}


class Win:
    def __init__(self, idx, t0):
        self.idx = idx; self.t0 = t0; self.t1 = t0; self.d = {}; self.ev = []; self.execs = collections.Counter()

    def put(self, key, val):
        self.d[key] = val


def parse(path):
    wins = [Win(0, 0.0)]
    t = 0.0
    last_cum = {}
    jank = []
    perf_t = []
    userslow = collections.Counter()
    lines = 0
    with open(path, 'r', errors='replace') as f:
        for raw in f:
            lines += 1
            raw = raw.rstrip('\n')
            for s in SPLIT.split(raw):
                if not s:
                    continue
                handle(s, wins, last_cum, jank, perf_t, userslow)
    return wins, jank, perf_t, userslow, lines


def cum(line, d, last_cum, w):
    keys = CUMUL.get(line)
    if not keys:
        return
    for k in keys:
        if k in d:
            v = num(d[k]); p = last_cum.get((line, k))
            last_cum[(line, k)] = v
            if p is not None:
                dv = v - p
                if dv < 0:
                    dv = v    # counter reset
                w.put(line + '.' + k, w.d.get(line + '.' + k, 0) + dv)


def handle(s, wins, last_cum, jank, perf_t, userslow):
    w = wins[-1]
    if s.startswith('NORD2 SYS:'):
        d = kv(s)
        win_ms = num(d.get('win_ms', '2000'))
        w.t1 = w.t0 + win_ms / 1000.0
        m = re.search(r'cpu_busy%=([0-9/]+) \(avg (\d+), big (\d+)\)', s)
        if m:
            cores = lst(m.group(1)); w.put('cpu_cores', cores); w.put('cpu_avg', float(m.group(2))); w.put('cpu_big', float(m.group(3)))
            w.put('cpu_max', max(cores) if cores else 0)
        for k in ('pageins', 'pageouts', 'faults', 'zerofill', 'cow', 'decomp'):
            if k in d:
                w.put('vm_' + k, num(d[k]))
        wins.append(Win(len(wins), w.t1))
        return
    if s.startswith('exec '):
        p = s[5:].split()[0] if len(s) > 5 else '?'
        w.execs[p] += 1
        return
    if s.startswith('NORD2 DVFS DECISION'):
        d = kv(s)
        m = re.search(r'util L=(-?\d+) B=(-?\d+) G=(\d+)', s)
        if m:
            w.put('utilL', float(m.group(1))); w.put('utilB', float(m.group(2))); w.put('utilG', float(m.group(3)))
        m = re.search(r'now L=(\d+) B=(\d+) G=(\d+)', s)
        if m:
            w.put('fL', float(m.group(1))); w.put('fB', float(m.group(2))); w.put('fG', float(m.group(3)))
        m = re.search(r'class=(\S+)', s)
        if m: w.put('class', m.group(1))
        m = re.search(r'bat=([0-9.]+)C', s)
        if m: w.put('bat', float(m.group(1)))
        m = re.search(r'therm=(\d+)', s)
        if m: w.put('therm', float(m.group(1)))
        m = re.search(r'why L=(\S+) B=(\S+) G=(\S+)', s)
        if m: w.put('why', '%s/%s/%s' % m.groups())
        return
    if s.startswith('NORD2 GDVFS:') and 'busy_pm' in s:
        d = kv(s)
        w.put('g_cur', num(d.get('cur', '0')) / 1000); w.put('g_want', num(d.get('want', '0')) / 1000)
        w.put('g_cap', num(d.get('cap', '0')) / 1000)
        m = re.search(r'busy_pm avg=(\d+) max=(\d+)', s)
        if m: w.put('g_busy', float(m.group(1))); w.put('g_busymax', float(m.group(2)))
        m = re.search(r'why=(\S+)', s)
        if m: w.put('g_why', m.group(1))
        return
    if s.startswith('NORD2 GPU JOBS:'):
        d = kv(s)
        w.put('jobs', num(d.get('n', '0'))); w.put('jfault', num(d.get('fault', '0')))
        for k in ('q_us', 'run_us', 'tot_us', 'clean_us', 'inv_us'):
            if k in d:
                a, b = pair(d[k]); w.put(k, a); w.put(k + '_max', b)
        return
    if s.startswith('NORD2 GPU JOBS2:'):
        d = kv(s)
        for k in ('gap_us', 'wait_us', 'ret_us'):
            if k in d:
                a, b = pair(d[k]); w.put('j2_' + k, a); w.put('j2_' + k + '_max', b)
        w.put('j2_waits', num(d.get('waits', '0')))
        return
    if s.startswith('NORD2 GPU LAT:'):
        d = kv(s)
        for k in ('irq_us', 'hand_us', 'depth'):
            if k in d:
                a, b = pair(d[k]); w.put('lat_' + k, a); w.put('lat_' + k + '_max', b)
        w.put('lat_busy', num(d.get('busy_pm', '0')))
        return
    if s.startswith('NORD2 GPU LAT2:'):
        d = kv(s)
        for k in ('wake_max_us', 'poll_max_us', 'wait_max_us', 'hold_max_us', 'drain_max_us'):
            if k in d:
                w.put('l2_' + k, num(d[k]))
        return
    if s.startswith('NORD2 GPU AS:') and 'binds=' in s:
        d = kv(s); cum('GPU AS', d, last_cum, w)
        w.put('as_open', num(d.get('open', '0'))); w.put('as_peak', num(d.get('peak', '0')))
        return
    if s.startswith('NORD2 GPU FENCE:'):
        d = kv(s); cum('GPU FENCE', d, last_cum, w); return
    if s.startswith('NORD2 PRFCNT A'):
        d = kv(s)
        win = num(d.get('win_us', '0')); gpu = num(d.get('gpu', '0'))
        # gpu = active cycles in the window; clock from GPUCLK / GDVFS
        w.put('pc_gpu_cycles', gpu); w.put('pc_win_us', win)
        w.put('pc_js0', num(d.get('js0', '0'))); w.put('pc_js1', num(d.get('js1', '0')))
        return
    if s.startswith('NORD2 PRFCNT D'):
        d = kv(s); w.put('pc_quads', num(d.get('quads', '0'))); w.put('pc_frag', num(d.get('frag', '0'))); return
    if s.startswith('NORD2 PRFCNT E'):
        d = kv(s); w.put('pc_texstarve', num(d.get('texstarve', '0'))); w.put('pc_texstall', num(d.get('texstall', '0'))); return
    if s.startswith('NORD2 PRFCNT B'):
        d = kv(s); w.put('pc_rbeat', num(d.get('rbeat', '0'))); w.put('pc_wbeat', num(d.get('wbeat', '0'))); return
    if s.startswith('NORD2 GPUCLK:'):
        d = kv(s); w.put('gpuclk_khz', num(d.get('core_khz', '0'))); return
    if s.startswith('NORD2 MEM:'):
        d = kv(s)
        for k in ('free', 'avail', 'compressed', 'throttled', 'wired'):
            if k in d:
                w.put('mem_' + k, num(d[k]) * 16 / 1024)      # MiB (16K pages)
        return
    if s.startswith('NORD2 MEM SURF:'):
        d = kv(s)
        for k in ('gpu_private', 'iommu', 'wired'):
            if k in d: w.put('surf_' + k, num(d[k]))
        if 'combined' in d: w.put('surf_combined', num(d['combined']))
        return
    if s.startswith('NORD2 MEM GPU:'):
        d = kv(s); w.put('bos_live', num(d.get('bos_live', '0'))); return
    if s.startswith('NORD2 MEMGOV'):
        if 'killed' in s or 'would kill' in s or 'victim' in s:
            w.ev.append(('memgov', s[:200]))
        m = re.search(r'A=(\d+) pages', s)
        if m: w.put('memgov_A_MiB', float(m.group(1)) * 16 / 1024)
        return
    if s.startswith('NORD2 FB PRESENT:'):
        d = kv(s)
        w.put('fb_n', w.d.get('fb_n', 0) + num(d.get('n', '0')))
        w.put('fb_avg_us', num(d.get('avg_us', '0'))); w.put('fb_max_us', max(w.d.get('fb_max_us', 0), num(d.get('max_us', '0'))))
        w.put('fb_late', w.d.get('fb_late', 0) + num(d.get('late_ticks', '0')))
        w.put('fb_timeouts', w.d.get('fb_timeouts', 0) + num(d.get('timeouts', '0')))
        w.put('fb_busy_skips', w.d.get('fb_busy_skips', 0) + num(d.get('busy_skips', '0')))
        w.put('fb_rate_hz', num(d.get('rate_hz', '0')))
        c = d.get('copy_us')
        if c: a, b = pair(c); w.put('fb_copy_max', max(w.d.get('fb_copy_max', 0), b))
        return
    if s.startswith('NORD2 FB SEEN:'):
        d = kv(s)
        g = lst(d.get('gaps_ms<=20/34/67/100/more', ''))
        if len(g) == 5:
            w.put('seen_gaps', g)
        w.put('seen_maxgap', num(d.get('max_gap_ms', '0')))
        return
    if s.startswith('NORD2 FB PACE: panel_hz'):
        d = kv(s)
        w.put('panel_hz', num(d.get('panel_hz', '0'))); w.put('active_fps', num(d.get('active_fps', '0')))
        w.put('pace_active', num(d.get('active', '0'))); w.put('pace_pushes', num(d.get('pushes', '0')))
        k = lst(d.get('refresh_per_present_1/2/3/4/5+', ''))
        if len(k) == 5: w.put('pace_k', k)
        m = re.search(r'dropped=(\d+) \(ready (\d+) nosw (\d+)\)', s)
        if m: w.put('drop', float(m.group(1))); w.put('drop_ready', float(m.group(2))); w.put('drop_nosw', float(m.group(3)))
        iv = lst(d.get('ivl_ms<=12.5/18/24/35/50/100', ''))
        if len(iv) == 6: w.put('pace_ivl', iv)
        w.put('best_run', num(d.get('best_run', '0')))
        return
    if s.startswith('NORD2 FB CLIENT:'):
        cl = re.findall(r'(\S+?)\[(\d+)\] shown=(\d+) fresh=(\d+) \(([0-9.]+)/s\)', s)
        w.put('clients', [(c[0], float(c[4])) for c in cl])
        fg = [c for c in cl if c[0] != 'backboardd' and float(c[4]) > 0.5]
        w.put('fg', max(fg, key=lambda c: float(c[4]))[0] if fg else ('home/UI' if any(float(c[4]) > 0.5 for c in cl) else 'idle'))
        for c in cl:
            w.put('cl_' + c[0], float(c[4]))
        return
    if s.startswith('NORD2 FB WAITS:'):
        d = kv(s); w.put('te_miss', num(d.get('te_miss', '0'))); w.put('gpu_waits', num(d.get('gpu_waits', '0')))
        w.put('fb_gpuwait_avg', num(d.get('avg_us', '0')))
        return
    for line in ('FB SWAPWAIT', 'FB HW', 'FB IOMMU', 'SURF USE'):
        if s.startswith('NORD2 ' + line + ':'):
            d = kv(s)
            if line == 'FB IOMMU' and 'maps=' not in s:
                return
            if line == 'FB HW' and 'hw_frames=' not in s:
                return
            cum(line, d, last_cum, w)
            return
    if s.startswith('NORD2 INPUT RATE:'):
        d = kv(s)
        m = re.search(r'reports=(\d+) \(([0-9.]+)/s\)', s)
        if m: w.put('in_reports', float(m.group(1)))
        w.put('in_gap_avg', num(d.get('gap_avg_us', '0')) / 1000); w.put('in_gap_max', num(d.get('gap_max_us', '0')) / 1000)
        w.put('in_read_avg', num(d.get('read_avg_us', '0')) / 1000); w.put('in_read_max', num(d.get('read_max_us', '0')) / 1000)
        g = lst(d.get('gap_ms<=10/12/16/20/25/33/50/more', ''))
        if g: w.put('in_gaps', g)
        if num(d.get('gap_avg_us', '0')) > 0:
            w.put('in_hz', 1e6 / num(d['gap_avg_us']))      # sampling rate while the finger is down
        return
    if s.startswith('NORD2 INPUT PACE:'):
        d = kv(s)
        for k in ('hz', 'gap_avg_us', 'gap_max_us', 'read_avg_us', 'read_max_us', 'idle_reads', 'irq_skips'):
            if k in d: w.put('ip_' + k, num(d[k]))
        return
    if s.startswith('NORD2 JANK:'):          # 0621: n=.. lost=.. ev=<uptime ms>:<refreshes><r|n>,...
        m = re.search(r'ev=(\S+)', s)
        for e in (m.group(1).split(',') if m else []):
            mm = re.match(r'(\d+):(\d+)([rn])', e)
            if mm:
                jank.append((w.t0, {'t_ms': int(mm.group(1)), 'k': int(mm.group(2)), 'why': 'swap-ready(presenter/TE/fence)' if mm.group(3) == 'r' else 'app/CA-late'}, e))
        mm = re.search(r'n=(\d+) lost=(\d+)', s)
        if mm:
            w.put('jank_n', w.d.get('jank_n', 0) + int(mm.group(1)) + int(mm.group(2)))
        return
    if s.startswith('NORD2 PERF T:'):        # 0621: t_ms=<uptime of the first second> pres=a/b/c (presents in each second)
        m = re.search(r't_ms=(\d+) pres=([0-9/]+)', s)
        if m:
            for i, c in enumerate(m.group(2).split('/')):
                perf_t.append((w.t0, {'t_ms': int(m.group(1)) + 1000 * i, 'pres': int(c)}, ''))
        return
    if s.startswith('NORD2 GPU USER:'):
        m = re.search(r'NORD2_RW (\w) (\w+)', s)
        if m:
            tag = m.group(1) + ' ' + m.group(2)
            userslow[tag] += 1
            if m.group(1) in ('W', 'E') or m.group(2) == 'SLOW':
                w.put('user_slow', w.d.get('user_slow', 0) + 1)
                mm = re.search(r'took ([0-9.]+) ms', s)
                if mm and float(mm.group(1)) >= 16:
                    w.ev.append(('slow', s.strip()[16:200]))
        return
    if s.startswith('NORD2 HANG') or s.startswith('panic(') or 'GPU JOBS fault' in s or 'NORD2 GPU IRQ' in s and 'fault' in s:
        w.ev.append(('fault', s.strip()[:200]))
        return
    if s.startswith('NORD2 GPU') and ('timeout' in s or 'soft-stop' in s or 'hard-stop' in s or 'reset' in s.lower() and 'resets=' not in s):
        w.ev.append(('gpu', s.strip()[:200]))
        return


def fmt(v, w=5, p=0):
    if v is None:
        return '-'.rjust(w)
    return ('%' + str(w) + '.' + str(p) + 'f') % v


def analyse(wins, args):
    rows = []
    for w in wins[:-1] if len(wins) > 1 else wins:
        d = w.d
        if args.t_from is not None and w.t0 < args.t_from:
            continue
        if args.t_to is not None and w.t0 > args.t_to:
            continue
        ev = []
        # stutter / problem detection per window
        k = d.get('pace_k'); iv = d.get('pace_ivl')
        active = d.get('pace_active', 0)
        if k and active >= 10:
            gaps = k[1] + k[2] + k[3]          # k5+ (> ~55 ms) = a pause in the content, counted apart
            k1 = k[0] / max(1, active)
            # a steady low-rate animation (every interval k>=3, e.g. 11.3/s) is content, not a stutter
            if k1 >= 0.5 and gaps > 0 and (k1 < 0.9 or k[2] + k[3] > 0):
                ev.append('STUTTER present gaps k2=%d k3=%d k4=%d k5+=%d of %d active (fps %.1f, drop ready %d nosw %d)' % (
                    k[1], k[2], k[3], k[4], active, d.get('active_fps', 0), d.get('drop_ready', 0), d.get('drop_nosw', 0)))
        if d.get('g_busy', 0) >= 600 and d.get('q_us', 0) >= 10000:
            ev.append('GPU-BOUND: in flight %d permille, mean queue wait %.1f ms per job: every client (also backboardd) waits behind the running jobs' % (d['g_busy'], d['q_us'] / 1000))
        sg = d.get('seen_gaps')
        if sg and (sg[1] + sg[2]) > 0 and d.get('fb_n', 0) > 40:
            ev.append('FB gaps 20-34ms=%d 34-67ms=%d max %d ms' % (sg[1], sg[2], d.get('seen_maxgap', 0)))
        if d.get('fb_late', 0) > 0:
            ev.append('late_ticks %d' % d['fb_late'])
        if d.get('fb_max_us', 0) > 20000:
            ev.append('present max %.1f ms' % (d['fb_max_us'] / 1000))
        if d.get('q_us_max', 0) > 8000:
            ev.append('GPU queue spike q_max %.1f ms (avg %.2f)' % (d['q_us_max'] / 1000, d.get('q_us', 0) / 1000))
        if d.get('tot_us_max', 0) > 16000:
            ev.append('GPU job total max %.1f ms' % (d['tot_us_max'] / 1000))
        if d.get('g_busymax', 0) >= 900 and d.get('g_cur', 0) < d.get('g_cap', 0) - 1:
            ev.append('GPU saturated busy max %d permille at %d MHz (cap %d, decisions-only)' % (d['g_busymax'], d['g_cur'], d['g_cap']))
        if d.get('l2_hold_max_us', 0) > 2000 or d.get('l2_wake_max_us', 0) > 2000:
            ev.append('GPU BH wake %d us hold %d us' % (d.get('l2_wake_max_us', 0), d.get('l2_hold_max_us', 0)))
        if d.get('GPU AS.evict', 0) > 20:
            ev.append('AS evictions %d (binds %d, waits %d)' % (d['GPU AS.evict'], d.get('GPU AS.binds', 0), d.get('GPU AS.waits', 0)))
        if d.get('GPU AS.waits', 0) > 0 or d.get('GPU AS.bindfail', 0) > 0:
            ev.append('AS waits %d bindfail %d' % (d.get('GPU AS.waits', 0), d.get('GPU AS.bindfail', 0)))
        if d.get('vm_pageins', 0) > 500 or d.get('vm_faults', 0) > 60000:
            ev.append('VM storm pageins %d faults %d zerofill %d decomp %d' % (d.get('vm_pageins', 0), d.get('vm_faults', 0), d.get('vm_zerofill', 0), d.get('vm_decomp', 0)))
        if d.get('FB IOMMU.maps', 0) + d.get('FB IOMMU.unmaps', 0) > 50:
            ev.append('IOMMU maps %d unmaps %d evicts %d' % (d.get('FB IOMMU.maps', 0), d.get('FB IOMMU.unmaps', 0), d.get('FB IOMMU.evicts', 0)))
        if d.get('FB IOMMU.map_fail', 0) or d.get('FB IOMMU.faults', 0) or d.get('FB IOMMU.tlb_to', 0):
            ev.append('IOMMU fail %d faults %d tlb_to %d' % (d.get('FB IOMMU.map_fail', 0), d.get('FB IOMMU.faults', 0), d.get('FB IOMMU.tlb_to', 0)))
        if d.get('FB HW.cpu_frames', 0) > 0:
            ev.append('CPU-composed frames %d (overlay fallback)' % d['FB HW.cpu_frames'])
        if d.get('FB SWAPWAIT.timeouts', 0) or d.get('FB SWAPWAIT.blocked', 0) > 5:
            ev.append('swapwait timeouts %d blocked %d' % (d.get('FB SWAPWAIT.timeouts', 0), d.get('FB SWAPWAIT.blocked', 0)))
        if d.get('in_gap_max', 0) > 33:
            ev.append('touch gap max %.1f ms (avg %.1f, %.0f Hz, read %.1f ms)' % (d['in_gap_max'], d.get('in_gap_avg', 0), d.get('in_hz', 0), d.get('in_read_avg', 0)))
        if d.get('cpu_max', 0) >= 90:
            ev.append('CPU core saturated %s' % '/'.join('%d' % c for c in d.get('cpu_cores', [])))
        if d.get('therm', 0) > 0:
            ev.append('thermal level %d (bat %.1f C)' % (d['therm'], d.get('bat', 0)))
        if d.get('mem_avail', 1e9) < 700:
            ev.append('low memory avail %.0f MiB' % d['mem_avail'])
        if d.get('te_miss', 0) > 0:
            ev.append('TE miss %d' % d['te_miss'])
        if d.get('pc_win_us') and d.get('g_cur'):
            cyc = d['pc_win_us'] * d['g_cur']      # us * MHz = cycles
            d['hw_active_pm'] = 1000.0 * d.get('pc_gpu_cycles', 0) / max(1.0, cyc)
            d['mpix_s'] = d.get('pc_quads', 0) * 4 / d['pc_win_us']        # Mpixel/s shaded (quads x4)
            d['bus_gbs'] = (d.get('pc_rbeat', 0) + d.get('pc_wbeat', 0)) * 16 / d['pc_win_us'] / 1000
            if d.get('g_busy', 0) >= 600 and d['hw_active_pm'] < 0.75 * d['g_busy']:
                ev.append('GPU in flight %d permille but HW active only %d permille (stalls: MMU/dependencies/serialisation)' % (d['g_busy'], d['hw_active_pm']))
        rs = {p: c for p, c in w.execs.items()}
        nexec = sum(rs.values())
        for e in w.ev:
            ev.append('%s: %s' % e)
        rows.append((w, ev, nexec))
    return rows


def summarize(wins, rows, jank, perf_t, userslow, lines, args):
    out = []
    W = [w for w, _, _ in rows]
    dur = (W[-1].t1 - W[0].t0) if W else 0
    out.append('== n2perf: %s  (%d lines, %d windows, %.0f s on the log clock)' % (args.log, lines, len(W), dur))
    import os
    t0f = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(args.log))), 'T0.txt')
    if os.path.exists(t0f):
        out.append('capture T0 %s (log clock 0 ~ start of the capture incl. the ring history; wall ~ T0 + t, +-10 s)' % open(t0f).read().strip())
    # header
    hdr = ('   t(s)  cpu% max  L/B MHz    G MHz gbusy/max  jobs q_ms/max  tot_max  pres fps  k>=2 drop(r/n)  late  in_Hz gap_max  memMiB  exec  events')
    out.append(hdr)
    for w, ev, nexec in rows:
        d = w.d
        interesting = args.all or ev or d.get('pace_active', 0) >= 10 or d.get('jobs', 0) > 200 or d.get('in_reports', 0) > 0
        if args.events_only and not ev:
            continue
        if not interesting:
            continue
        k = d.get('pace_k') or [0, 0, 0, 0, 0]
        line = '%7.0f  %3s %3s  %4s/%4s  %4s/%3s %4s/%4s %5s %5s/%5s %6s  %4s %5s  %4s %4s/%-4s %4s  %5s %6s  %6s  %4s  ' % (
            w.t0, fmt(d.get('cpu_avg'), 3), fmt(d.get('cpu_max'), 3), fmt(d.get('fL'), 4), fmt(d.get('fB'), 4),
            fmt(d.get('g_cur'), 4), fmt(d.get('g_cap'), 3), fmt(d.get('g_busy'), 4), fmt(d.get('g_busymax'), 4), fmt(d.get('jobs'), 5),
            fmt(d.get('q_us', 0) / 1000 if 'q_us' in d else None, 5, 2), fmt(d.get('q_us_max', 0) / 1000 if 'q_us_max' in d else None, 5, 1),
            fmt(d.get('tot_us_max', 0) / 1000 if 'tot_us_max' in d else None, 6, 1),
            fmt(d.get('fb_n'), 4), fmt(d.get('active_fps'), 5, 1), fmt(sum(k[1:]) if 'pace_k' in d else None, 4),
            fmt(d.get('drop_ready'), 4), fmt(d.get('drop_nosw'), 4), fmt(d.get('fb_late'), 4),
            fmt(d.get('in_hz'), 5, 1), fmt(d.get('in_gap_max'), 6, 1), fmt(d.get('mem_avail'), 6), nexec or '')
        out.append(line + '%-10s ' % str(d.get('fg', ''))[:10] + ('; '.join(ev) if ev else ''))
    # carry the foreground client forward over the 2 s rows between 5 s FB CLIENT lines
    fg = 'idle'
    for w in W:
        fg = w.d.get('fg', fg); w.d['fg_c'] = fg
    out.append('')
    out.append('== phases (consecutive rows with the same foreground FB client)')
    out.append('   from    to  client            fps(med) ontime%  q_ms(avg) q_max  gbusy hwact  G MHz  cpu%  touchHz  drops r/n  stutterWins')
    ph = []
    for w in W:
        if ph and ph[-1][0] == w.d['fg_c']:
            ph[-1][1].append(w)
        else:
            ph.append((w.d['fg_c'], [w]))
    for name, ws in ph:
        fps = sorted(x.d['active_fps'] for x in ws if x.d.get('pace_active', 0) >= 10)
        kk = [0] * 5
        for x in ws:
            for i, v in enumerate(x.d.get('pace_k', [0] * 5)): kk[i] += v
        avg = lambda key: (sum(x.d[key] for x in ws if key in x.d) / max(1, sum(1 for x in ws if key in x.d))) if any(key in x.d for x in ws) else None
        mx = lambda key: max((x.d.get(key, 0) for x in ws), default=0)
        st = sum(1 for x in rows if x[0] in ws and any(e.startswith('STUTTER') for e in x[1]))
        out.append('%6.0f %6.0f  %-16s  %7s  %6s  %8s  %5s  %5s %5s  %5s  %4s  %6s  %5s/%-5s  %d' % (
            ws[0].t0, ws[-1].t1, name[:16], fmt(fps[len(fps) // 2], 5, 1) if fps else '-', fmt(100.0 * kk[0] / sum(kk), 4, 0) if sum(kk) else '-',
            fmt(avg('q_us') / 1000 if avg('q_us') is not None else None, 6, 2), fmt(mx('q_us_max') / 1000, 5, 1), fmt(avg('g_busy'), 5), fmt(avg('hw_active_pm'), 5),
            fmt(avg('g_cur'), 5), fmt(avg('cpu_avg'), 4), fmt(avg('in_hz'), 5, 1), fmt(sum(x.d.get('drop_ready', 0) for x in ws), 4),
            fmt(sum(x.d.get('drop_nosw', 0) for x in ws), 4), st))
    # worst peaks: score = dropped refreshes + queue latency + present gaps
    def score(x):
        d = x.d; k = d.get('pace_k', [0] * 5)
        act = max(1, d.get('pace_active', 1))
        sh = (k[1] + 2 * k[2] + 3 * k[3]) if k[0] / act >= 0.5 else 0      # short gaps inside an animation (k5+ = pauses)
        return sh + d.get('q_us_max', 0) / 2000 + d.get('q_us', 0) / 500 + 20 * (d.get('fb_late', 0) > 0)
    out.append('')
    out.append('== worst %d peaks' % args.peaks)
    for w, ev, nexec in sorted(rows, key=lambda r: -score(r[0]))[:args.peaks]:
        d = w.d
        out.append('[t=%.0f s, fg %s] fps %s, drops ready/nosw %s/%s, q avg/max %.1f/%.1f ms, GPU busy %s hwact %s at %s MHz, CPU %s%% (max core %s), mem avail %s MiB, execs %s' % (
            w.t0, d.get('fg_c'), d.get('active_fps', '-'), d.get('drop_ready', '-'), d.get('drop_nosw', '-'), d.get('q_us', 0) / 1000, d.get('q_us_max', 0) / 1000,
            fmt(d.get('g_busy'), 1), fmt(d.get('hw_active_pm'), 1), fmt(d.get('g_cur'), 1), fmt(d.get('cpu_avg'), 1), fmt(d.get('cpu_max'), 1), fmt(d.get('mem_avail'), 1),
            dict(w.execs)))
        for e in ev[:8]:
            out.append('     - ' + e[:220])
    # aggregate stats
    out.append('')
    out.append('== summary')
    act = [w for w in W if w.d.get('pace_active', 0) >= 10]
    if act:
        fps = sorted(w.d.get('active_fps', 0) for w in act)
        tot_k = [0] * 5
        for w in act:
            for i, v in enumerate(w.d.get('pace_k', [0] * 5)):
                tot_k[i] += v
        n = sum(tot_k)
        out.append('active FB windows %d: active_fps median %.1f min %.1f max %.1f; refreshes per present 1/2/3/4/5+ = %s (%.1f %% on time)' % (
            len(act), fps[len(fps) // 2], fps[0], fps[-1], '/'.join('%d' % x for x in tot_k), 100.0 * tot_k[0] / max(1, n)))
        dr = sum(w.d.get('drop_ready', 0) for w in act); dn = sum(w.d.get('drop_nosw', 0) for w in act)
        out.append('dropped refreshes in active windows: ready %d (swap was there: presenter/TE/GPU-fence lost it) nosw %d (app/CA late)' % (dr, dn))
    inw = [w for w in W if w.d.get('in_reports', 0) > 0]
    if inw:
        hz = sorted(w.d.get('in_hz', 0) for w in inw); rd = [w.d.get('in_read_avg', 0) for w in inw]
        out.append('touch sampling (finger down): %d windows, median %.1f Hz (display %.0f Hz), I2C read avg %.2f ms, worst gap %.1f ms' % (
            len(inw), hz[len(hz) // 2], max((w.d.get('panel_hz', 0) for w in W), default=0), sum(rd) / len(rd), max(w.d.get('in_gap_max', 0) for w in inw)))
    gw = [w for w in W if 'g_busymax' in w.d]
    if gw:
        sat = [w for w in gw if w.d['g_busymax'] >= 900]
        out.append('GPU: clock %s MHz (cap %s); windows with busy max >= 90 %%: %d of %d; q_us avg median %.2f ms, worst q %.1f ms; job total worst %.1f ms' % (
            '/'.join(sorted({'%d' % w.d.get('g_cur', 0) for w in gw})), '/'.join(sorted({'%d' % w.d.get('g_cap', 0) for w in gw})), len(sat), len(gw),
            sorted(w.d.get('q_us', 0) for w in gw)[len(gw) // 2] / 1000, max(w.d.get('q_us_max', 0) for w in gw) / 1000,
            max(w.d.get('tot_us_max', 0) for w in gw) / 1000))
    ex = collections.Counter()
    for w in W:
        ex.update(w.execs)
    if ex and dur > 0:
        rp = sum(c for p, c in ex.items() if p in RESPAWN_PROCS)
        out.append('process launches: %d in %.0f s (%.2f/s); known respawn loops %s' % (
            sum(ex.values()), dur, sum(ex.values()) / dur, ', '.join('%s %d' % (p, ex[p]) for p in sorted(RESPAWN_PROCS) if ex[p])))
        top = [p for p, _ in ex.most_common(12) if p not in RESPAWN_PROCS]
        if top:
            out.append('  other launches: ' + ', '.join('%s %d' % (p, ex[p]) for p in top))
        # cost of launches: CPU in windows with / without respawn execs (idle-ish windows only)
        a = [w.d.get('cpu_avg') for w in W if 'cpu_avg' in w.d and sum(c for p, c in w.execs.items() if p in RESPAWN_PROCS - {'lskdd', 'xpcproxy'}) > 0]
        b = [w.d.get('cpu_avg') for w in W if 'cpu_avg' in w.d and sum(c for p, c in w.execs.items() if p in RESPAWN_PROCS - {'lskdd', 'xpcproxy'}) == 0]
        if a and b:
            out.append('  CPU avg in windows with a bluetoothd/fairplayd/wifip2pd respawn %.1f %% vs without %.1f %%' % (sum(a) / len(a), sum(b) / len(b)))
    tm = [w.d.get('bat') for w in W if 'bat' in w.d]
    if tm:
        out.append('battery %.1f..%.1f C, thermal levels %s' % (min(tm), max(tm), sorted({int(w.d.get('therm', 0)) for w in W if 'therm' in w.d})))
    ma = [w.d.get('mem_avail') for w in W if 'mem_avail' in w.d]
    if ma:
        out.append('memory avail %.0f..%.0f MiB; pageins total %d; AS evictions %d binds %d waits %d; IOMMU maps %d unmaps %d' % (
            min(ma), max(ma), sum(w.d.get('vm_pageins', 0) for w in W), sum(w.d.get('GPU AS.evict', 0) for w in W),
            sum(w.d.get('GPU AS.binds', 0) for w in W), sum(w.d.get('GPU AS.waits', 0) for w in W),
            sum(w.d.get('FB IOMMU.maps', 0) for w in W), sum(w.d.get('FB IOMMU.unmaps', 0) for w in W)))
    if userslow:
        out.append('userland (Nord2Metal) messages: ' + ', '.join('%s %d' % kv_ for kv_ in userslow.most_common(8)))
    if jank:
        out.append('')
        out.append('== NORD2 JANK events (%d)' % len(jank))
        cls = collections.Counter(j[1].get('why', '?') for j in jank)
        out.append('by cause: ' + ', '.join('%s %d' % c for c in cls.most_common()))
        for t0, d, s in jank[:400]:
            out.append('  [log t %6.0f] uptime %9.3f s  gap of %d refreshes (%.0f ms)  %s' % (t0, d['t_ms'] / 1000, d['k'], d['k'] * 11.2, d['why']))
    if perf_t:
        out.append('')
        out.append('== presents per second (NORD2 PERF T, %d s with presents)' % len(perf_t))
        for t0, d, s in perf_t[:3000]:
            out.append('  [log t %6.0f] uptime %9.0f s  %3d presents/s %s' % (t0, d['t_ms'] / 1000, d['pres'], '#' * (d['pres'] // 3)))
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--csv')
    ap.add_argument('--json')
    ap.add_argument('--from', dest='t_from', type=float)
    ap.add_argument('--to', dest='t_to', type=float)
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--events-only', action='store_true')
    ap.add_argument('--peaks', type=int, default=5)
    args = ap.parse_args()
    wins, jank, perf_t, userslow, lines = parse(args.log)
    rows = analyse(wins, args)
    print(summarize(wins, rows, jank, perf_t, userslow, lines, args))
    if args.csv:
        keys = sorted({k for w, _, _ in rows for k, v in w.d.items() if isinstance(v, (int, float))})
        with open(args.csv, 'w') as f:
            f.write('t,' + ','.join(keys) + ',execs,events\n')
            for w, ev, nexec in rows:
                f.write('%.1f,' % w.t0 + ','.join(str(w.d.get(k, '')) for k in keys) + ',%d,"%s"\n' % (nexec, ' | '.join(ev).replace('"', "'")))
    if args.json:
        with open(args.json, 'w') as f:
            json.dump([{'t': w.t0, 'd': w.d, 'execs': dict(w.execs), 'events': ev} for w, ev, _ in rows], f)


if __name__ == '__main__':
    main()
