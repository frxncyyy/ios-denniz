#!/usr/bin/env python3
"""Objective PASS/WARN/FAIL report of an overnight stress run (stress_run.py windows + console log segments).

  stress_verdict.py STRESS_DIR [--json OUT.json] [--baseline PHOTOS.png] [--allow-crash proc,proc] [--strict-crash] [--game-proc SubwaySurf]
                   [--coords coords.json] [--no-shots]

STRESS_DIR holds one directory per window (w01, w02, ...), each with steps.jsonl + summary.json (stress_run.py) and the console
segment named in summary.json 'log' (or console.log next to it). Window 1's log is txn_g.sh's ui-usb/xnu-live.log (from boot):
records before the window's 'start' event are boot-time and only reported as info. Byte offsets (steps' log_offset) map every
console record to a phase; records are cut at every known prefix (console lines are interleaved mid-line).

Requirements -> verdict (exit 0 PASS, 1 FAIL, 2 NO DATA, 3 WARN only):
 R1 UI at a fixed 90 fps     every UI phase (spam photos switcher ccnc settings apps rotate): FIXED90 PASS of the 'NORD2 FB PACE'
                              records in it (pace_report.py rule: panel 88.5..90.5 Hz, >= 50 active, k1 >= 98 %, active_fps >= 87;
                              WARN = k1 >= 90 %); the first PACE record of a phase is skipped (it straddles the previous phase);
                              plus no 'NORD2 FB PANEL: 90 Hz reverted|fault' and p90_reverts = 0.
 R2 Subway Surfers >= 60 fps  PACE active_fps >= 60 between the 'subway play' event and the phase end; no CRASH/F CRASH/jetsam of
    and no crash              the game; game seen in the log (WARN if not).
 R3 Photos grid intact, 90    photos phase FIXED90 + screenshot checkpoints (shotcheck.py, defect in both pictures 1 s apart = FAIL)
                              + driver proxies (SURF freeze, PHOTOS frame sampled black texels, NORD2 PERF PHOTOS fps) as WARN/info.
                              Screenshots taken while the overlay composes (kern.n2fbhw >= 2 and hw_frames growing around the shot)
                              are INVALID (the snapshot reads the stale CPU panel buffer) and kept only for review.
 R4 no crash/glitch/leak      kernel 'CRASH p[pid] sig', userland 'F CRASH', jetsam 'memorystatus: killing_*', 'NORD2 HANG' stacks,
                              panic, reboot (capture REBOOT / stress rc 4), console loss (PHONE_LOST / rc 3 / capture LOST) = FAIL;
                              GPU: JOBS fault>0, PRFCNT AS0 fault, GPU HW error != 0 = FAIL; HW overlay: AUTO-DISABLE, BACKOFF
                              exhausted = FAIL, BACKOFF = WARN, HW 'faults' growth = FAIL; SURF USE imbalance/LEAK = FAIL; GPU private
                              MiB slope > 60 MiB/h with growth > 150 MiB = FAIL (> 20 MiB/h and > 50 MiB = WARN); FB POOL 'full for' =
                              WARN; slow draw/commit >= 100 ms in UI phases and ALLOC SLOW > 20 ms = WARN (stutter evidence).
"""
import importlib.util, json, re, sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import shotcheck  # noqa: E402

UI_PHASES = ('spam', 'photos', 'switcher', 'ccnc', 'settings', 'apps', 'rotate')
TESTED_PROCS = ('MobileSlideShow', 'Preferences', 'AppStore', 'MobileSafari', 'MobileSMS', 'SubwaySurf', 'SpringBoard', 'backboardd')
UI_DAEMONS = ('SpringBoard', 'backboardd', 'PhotosUIService', 'photoanalysisd', 'assetsd', 'MTLCompilerService', 'IOSurfaceRoot', 'mediaserverd')
# crash policy: sig 9 = killed (switcher card kill / launchd / jetsam) -> INFO; tested apps, UI daemons, any SIGSEGV/SIGBUS -> FAIL;
# other daemons -> WARN.  --allow-crash procs -> INFO.  --strict-crash: every non-sig-9 crash -> FAIL.


def load_pace():
    for p in (HERE.parent / 'fbpace-20261112' / 'pace_report.py', HERE / 'pace_report.py'):
        if p.exists():
            spec = importlib.util.spec_from_file_location('pace_report', p); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
            return m
    raise SystemExit('pace_report.py not found (fbpace-20261112)')


PR = load_pace()


def pace_parse(s):
    """pace_report.parse, tolerant of records cut by interleaving ('k=1/2/' -> key dropped; pace_report.parse raises there)"""
    d = {}
    for k, v in PR.KV.findall(s):
        parts = v.split('/')
        try: vals = [float(x) for x in parts]
        except ValueError: continue
        d[k] = vals if len(parts) > 1 else vals[0]
    for k in ('refresh_per_present_1/2/3/4/5+', 'ivl_ms<=12.5/18/24/35/50/100', 'rate_s<30/45/62/75/85/88/88+'):
        if k in d and not isinstance(d[k], list): del d[k]
    return d

# ---------------------------------------------------------------- record extraction
CUT = re.compile(r'\n|NORD2 (?!_)|(?<![A-Z] )CRASH |memorystatus: ')


def bodies(text, prefix):
    """(pos, body) for every occurrence of the literal prefix; the body ends at a newline or at the next record start"""
    out = []; i = text.find(prefix)
    while i >= 0:
        s = i + len(prefix); m = CUT.search(text, s); e = m.start() if m else len(text)
        out.append((i, text[s:e])); i = text.find(prefix, s)
    return out


def kvnum(body):
    d = {}
    for k, v in re.findall(r'([a-z_][a-z0-9_]*)=(-?\d+)', body): d.setdefault(k, int(v))
    return d


def extract(text):
    """-> list of events (pos, kind, data)"""
    E = []
    add = lambda pos, kind, **d: E.append((pos, kind, d))
    for pos, b in bodies(text, 'NORD2 FB PACE: '):
        if b.startswith('panel_hz='):
            add(pos, 'pace', s=b, d=pace_parse(b), p90=int((re.search(r'p90_reverts=(\d+)', b) or [0, 0])[1]),
                backoffs=int((re.search(r'hw_backoffs=(\d+)', b) or [0, 0])[1]))
    for pos, b in bodies(text, 'NORD2 FB PRESENT: '):
        d = kvnum(b); add(pos, 'present', n=d.get('n', 0), timeouts=d.get('timeouts', 0))
    for pos, b in bodies(text, 'NORD2 FB HW: '):
        if b.startswith('mode='):
            d = kvnum(b); lf = re.search(r'last_fault=(\S+)', b)
            add(pos, 'hw', mode=d.get('mode'), hw=d.get('hw_frames'), cpu=d.get('cpu_frames'), faults=d.get('faults'),
                race=d.get('race', d.get('hw_race')), last_fault=lf.group(1) if lf else None,
                hard=sum(d.get(k, 0) for k in ('ovl_und', 'smi_und', 'eof_abn', 'dsi_und', 'fd_timeout', 'readback_bad')))
        elif b.startswith('AUTO-DISABLE'): add(pos, 'hw_autodisable', s=b[:160])
        elif b.startswith('BACKOFF exhausted'): add(pos, 'hw_backoff_exhausted', s=b[:160])
        elif b.startswith('BACKOFF'): add(pos, 'hw_backoff', s=b[:160])
    for pos, b in bodies(text, 'NORD2 FB HW MODE: '):
        if 'fallback' in b: add(pos, 'hw_autodisable', s='HW MODE ' + b[:120])
    for pos, b in bodies(text, 'NORD2 FB PANEL: '):
        m = re.match(r'90 Hz (reverted|fault): reason=(\S+)', b)
        if m: add(pos, 'p90_revert', s=b[:160])
        elif b.startswith('60 Hz'): add(pos, 'p60_forced', s=b[:120])
    for pos, b in bodies(text, 'NORD2 GPU JOBS: '):
        d = kvnum(b); add(pos, 'jobs', fault=d.get('fault', 0), full=d.get('full', 0), n=d.get('n', 0))
    for pos, b in bodies(text, 'NORD2 PRFCNT: '):
        if 'AS0 fault' in b and 'stale' not in b: add(pos, 'gpu_mmu_fault', s=b[:140])
        elif b.startswith('incomplete'): add(pos, 'gpu_prfcnt_incomplete', s=b[:120])
    for pos, b in bodies(text, 'NORD2 GPU HW: '):
        d = kvnum(b)
        if d.get('error', 0): add(pos, 'gpu_hw_error', s=b[:140])
    for pos, b in bodies(text, 'NORD2 GPU IRQ: '): add(pos, 'gpu_irq', s=b[:120])
    for pos, b in bodies(text, 'NORD2 HANG: '):
        m = re.match(r'(?:ALL )?(\S+?)\[(\d+)\](?: main thread stack| \d+ threads|:)', b)
        if m: add(pos, 'hang', proc=m.group(1), pid=int(m.group(2)), s=b[:120])
    for m in re.finditer(r'(?<![A-Z] )CRASH (\S+?)\[(\d+)\] sig (\d+)', text):
        add(m.start(), 'crash', proc=m.group(1), pid=int(m.group(2)), sig=int(m.group(3)), src='kernel')
    for m in re.finditer(r'F CRASH (?:<([^>]+)> )?signal (\d+)', text):
        add(m.start(), 'ucrash', proc=m.group(1), sig=int(m.group(2)), src='userland')
    for m in re.finditer(r'memorystatus: (killing_\w+|sending suspended process)[^\n]*?pid (\d+) \[([^\]]*)\]', text):
        add(m.start(), 'jetsam', how=m.group(1), pid=int(m.group(2)), proc=m.group(3))
    for m in re.finditer(r'task_exceeded_cpulimit: killing pid (\d+) \[([^\]]*)\]', text):
        add(m.start(), 'jetsam', how='cpulimit', pid=int(m.group(1)), proc=m.group(2))
    for m in re.finditer(r'panic\(|Kernel panic|Debugger called|PANIC ', text): add(m.start(), 'panic', s=text[m.start():m.start() + 120].split('\n')[0])
    for m in re.finditer(r'NORD2 USB: native console (?:for 45 seconds|continuous)', text): add(m.start(), 'bootmark')
    for m in re.finditer(r'NORD2 AUTOTEST: reset asked', text): add(m.start(), 'reset_asked')
    for pos, b in bodies(text, 'NORD2 MEM: '):
        d = kvnum(b); add(pos, 'mem', free=d.get('free'), avail=d.get('avail'))
    for pos, b in bodies(text, 'NORD2 MEM GPU: '):
        d = kvnum(b); m = re.search(r'private=(\d+) MiB', b); add(pos, 'memgpu', private=int(m.group(1)) if m else None, bos=d.get('bos_live'))
    for pos, b in bodies(text, 'NORD2 FB POOL: '):
        m = re.search(r'used (\d+) (?:MiB )?of (\d+)', b)
        if b.startswith('full for'): add(pos, 'pool_full', s=b[:140])
        if m: add(pos, 'pool', used=int(m.group(1)), never=int((re.search(r'never shown \d+ \((\d+) MiB\)', b) or [0, 0])[1]))
    for pos, b in bodies(text, 'NORD2 SURF USE: '):
        m = re.search(r'unmatched-dec (\d+) dup-inc (\d+) kernel-unmatched (\d+)/(\d+)', b)
        if m and (int(m.group(1)) or int(m.group(3)) or int(m.group(4))): add(pos, 'surf_imbalance', s=b[:200])
        m = re.search(r'free-nonzero (\d+)', b)
        if m and int(m.group(1)): add(pos, 'surf_imbalance', s='free-nonzero ' + m.group(1))
        if 'LEAK at free' in b or 'LEAK SUSPECT' in b: add(pos, 'surf_leak', s=b[:200])
        m = re.match(r'gpu holds used=(\d+)', b)
        if m: add(pos, 'gpu_holds', used=int(m.group(1)))
    for m in re.finditer(r'PERF <([^>]+)> slow draw: ([\d.]+) ms', text): add(m.start(), 'slow', proc=m.group(1), what='draw', ms=float(m.group(2)))
    for m in re.finditer(r'PERF <([^>]+)> slow: ([\d.]+) ms commit', text): add(m.start(), 'slow', proc=m.group(1), what='commit', ms=float(m.group(2)))
    for m in re.finditer(r'PERF <([^>]+)> slow blit: ([\d.]+) ms', text): add(m.start(), 'slow', proc=m.group(1), what='blit', ms=float(m.group(2)))
    for m in re.finditer(r'PERF <([^>]+)> slow completion: ([\d.]+) ms', text): add(m.start(), 'slow', proc=m.group(1), what='completion', ms=float(m.group(2)))
    for pos, b in bodies(text, 'NORD2 GPU ALLOC SLOW: '):
        d = kvnum(b); add(pos, 'alloc_slow', us=d.get('total', 0))
    for m in re.finditer(r'NORD2 PERF PHOTOS A (\S+) opt=(\d+) fps=([\d.]+)', text): add(m.start(), 'photos_fps', proc=m.group(1), fps=float(m.group(3)))
    for m in re.finditer(r'PHOTOS frame n=\d+ sampled: ([^\n]*)', text):
        vals = re.findall(r'=([0-9a-f]{8})\b', m.group(1)[:600])
        add(m.start(), 'photos_sampled', n=len(vals), black=sum(1 for v in vals if v in ('00000000', '000000ff', 'ff000000')))
    for m in re.finditer(r'SURF freeze n=\d+', text): add(m.start(), 'surf_freeze')
    E.sort(key=lambda e: e[0])
    return E


# ---------------------------------------------------------------- run layout
def windows(stress_dir):
    out = []
    for d in sorted(p for p in Path(stress_dir).iterdir() if p.is_dir() and re.match(r'w\d+$', p.name)):
        steps = [json.loads(l) for l in open(d / 'steps.jsonl')] if (d / 'steps.jsonl').exists() else []
        summ = json.loads((d / 'summary.json').read_text()) if (d / 'summary.json').exists() else {}
        log = Path(summ['log']) if summ.get('log') else d / 'console.log'
        if not log.exists() and (d / 'console.log').exists(): log = d / 'console.log'
        cap_ev = []
        for f in (Path(str(log) + '.events.jsonl'),):
            if f.exists(): cap_ev = [json.loads(l) for l in open(f)]
        out.append(dict(dir=d, idx=int(d.name[1:]), steps=steps, summary=summ, log=log, cap=cap_ev))
    return out


def phases_of(w):
    """-> [(name, off0, off1, t0, t1, play_off)] from phase begin/end records"""
    ph = []; cur = None; play = None
    for e in w['steps']:
        if e.get('kind') == 'phase' and e.get('event') == 'begin':
            cur = [e['phase'], e.get('log_offset') or 0, None, e['t'], None, None]
        elif e.get('kind') == 'event' and e.get('event') == 'subway play' and cur: cur[5] = e.get('log_offset')
        elif e.get('kind') == 'phase' and e.get('event') == 'end' and cur:
            cur[2], cur[4] = e.get('log_offset') or cur[1], e['t']; ph.append(tuple(cur)); cur = None
    if cur:                                        # window ended inside a phase (phone lost)
        last = w['steps'][-1]; cur[2], cur[4] = last.get('log_offset') or cur[1], last['t']; ph.append(tuple(cur))
    return ph


def time_map(w):
    pts = sorted({(e['log_offset'], e['t']) for e in w['steps'] if e.get('log_offset') is not None and 't' in e})
    def f(off):
        if not pts: return None
        if off <= pts[0][0]: return pts[0][1]
        for (o0, t0), (o1, t1) in zip(pts, pts[1:]):
            if o0 <= off <= o1: return t0 if o1 == o0 else t0 + (t1 - t0) * (off - o0) / (o1 - o0)
        return pts[-1][1]
    return f


def fixed90(paces):
    tot = {}
    for e in paces: PR.add(tot, e['d'])
    if not tot.get('active'): return None, 'no active PACE intervals'
    k = tot['refresh_per_present_1/2/3/4/5+']; te = sorted(x for x in tot['te'] if x)
    te = te[len(te) // 2] if te else 0
    s = PR.verdict(int(tot['active']), int(k[0]), tot['active'] / tot['act_us'] * 1e6, te)
    return s.split()[1], s


def slope(pts):
    """least squares y over x hours -> (slope per hour, r2, growth, span_h)"""
    if len(pts) < 3: return None
    xs = [p[0] / 3600 for p in pts]; ys = [p[1] for p in pts]; n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n; sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0: return None
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys)); b = sxy / sxx
    syy = sum((y - my) ** 2 for y in ys); r2 = (sxy * sxy / (sxx * syy)) if syy else 1.0
    return b, r2, ys[-1] - ys[0], xs[-1] - xs[0]


# ---------------------------------------------------------------- verdict
class Report:
    """items aggregated by (level, req, message with numbers masked): first message + count"""
    def __init__(self): self.items = []; self._ix = {}
    def add(self, req, level, msg):
        key = (level, req, re.sub(r'\d+', '#', msg))
        if key in self._ix: self._ix[key]['count'] += 1; self._ix[key]['last'] = msg; return
        it = dict(req=req, level=level, msg=msg, count=1, last=None); self._ix[key] = it; self.items.append(it)
    def level(self, req=None):
        lv = [i['level'] for i in self.items if req is None or i['req'] == req]
        return 'FAIL' if 'FAIL' in lv else 'WARN' if 'WARN' in lv else 'PASS'


def main(argv):
    if not argv or argv[0] in ('-h', '--help'): print(__doc__); return 0
    opt = lambda k, d=None: argv[argv.index(k) + 1] if k in argv else d
    strict = '--strict-crash' in argv
    sd = Path(argv[0]); allow = set(filter(None, (opt('--allow-crash', '') or '').split(',')))
    game = opt('--game-proc', 'SubwaySurf')
    W = windows(sd)
    if not W: print('no windows (w01, w02, ...) in', sd); return 2
    R = Report(); P = []   # per phase rows
    mem_pts, priv_pts, bos_pts, pool_pts = [], [], [], []
    slow = defaultdict(lambda: [0, 0.0]); alloc = [0, 0]; fps_photos = []; shots = []
    seen_any = False; game_seen = False; boot_info = []
    holds_min = None; prox = [0, 0, 0]
    for w in W:
        if not w['log'].exists():
            R.add('R4', 'FAIL', 'window %d: console log %s missing' % (w['idx'], w['log'])); continue
        text = w['log'].read_bytes().decode('latin-1')      # 1 char == 1 byte: offsets match steps' log_offset
        E = extract(text); seen_any = seen_any or bool(E)
        tm = time_map(w); ph = phases_of(w)
        start = next((e.get('log_offset') for e in w['steps'] if e.get('event') == 'start'), 0) or 0
        end_off = max([e.get('log_offset') or 0 for e in w['steps']] + [0])
        summ = w['summary']; rc = summ.get('rc')
        if rc == 3: R.add('R4', 'FAIL', 'window %d: %s' % (w['idx'], summ.get('why')))
        if rc == 4: R.add('R4', 'FAIL', 'window %d: %s' % (w['idx'], summ.get('why')))
        if (w['dir'] / 'PHONE_LOST.txt').exists() and rc not in (3, 4):
            R.add('R4', 'FAIL', 'window %d: PHONE_LOST.txt present' % w['idx'])
        if not w['steps']: R.add('R4', 'FAIL', 'window %d: no steps recorded' % w['idx'])
        for c in w['cap']:
            if c.get('event') in ('LOST', 'REBOOT'): R.add('R4', 'FAIL', 'window %d capture: %s' % (w['idx'], c.get('event') + ' ' + str(c.get('why', c.get('new_boot', '')))))
            if c.get('event') == 'BUFFER FULL': R.add('R4', 'WARN', 'window %d capture: kernel log buffer full (lines lost until the next rewind)' % w['idx'])
        def phase_at(pos):
            for p in ph:
                if p[1] <= pos < p[2]: return p
            return None
        by_phase = defaultdict(list)
        for pos, kind, d in E:
            if pos < start:
                if kind in ('crash', 'ucrash', 'panic', 'jetsam'): boot_info.append('w%d pre-stress %s %s' % (w['idx'], kind, d.get('proc', d.get('s', ''))))
                continue
            p = phase_at(pos); pname = p[0] if p else 'between'
            by_phase[p].append((pos, kind, d))
            t = tm(pos)
            if kind == 'bootmark' and w['idx'] > 1: R.add('R4', 'FAIL', 'window %d: boot marker in the log at %d (phone rebooted)' % (w['idx'], pos))
            elif kind == 'crash':
                proc = d['proc']; sig = d['sig']
                if proc in allow or sig == 9: lvl = 'INFO'
                elif strict or proc in TESTED_PROCS or proc in UI_DAEMONS or proc == game or sig in (10, 11): lvl = 'FAIL'
                else: lvl = 'WARN'
                R.add('R2' if proc == game else 'R4', lvl, 'w%d crash %s[%d] sig %d%s (phase %s)' % (w['idx'], proc, d['pid'], sig, ' (killed)' if sig == 9 else '', pname))
            elif kind == 'ucrash':
                nxt = [e for e in E if e[1] == 'crash' and pos < e[0] <= pos + 65536][:1]
                if d.get('proc'): R.add('R2' if d['proc'] == game else 'R4', 'INFO' if d['proc'] in allow else 'FAIL', 'w%d userland crash report %s signal %d (phase %s)' % (w['idx'], d['proc'], d['sig'], pname))
                elif not nxt: R.add('R4', 'FAIL', 'w%d userland crash report signal %d, process unknown, no kernel CRASH line follows (phase %s)' % (w['idx'], d['sig'], pname))
            elif kind == 'jetsam':
                lvl = 'FAIL' if d['proc'] in TESTED_PROCS or d['proc'] == game else 'WARN'
                R.add('R2' if d['proc'] == game else 'R4', lvl, 'w%d jetsam %s %s[%d] (phase %s)' % (w['idx'], d['how'], d['proc'], d['pid'], pname))
            elif kind == 'panic': R.add('R4', 'FAIL', 'w%d panic: %s' % (w['idx'], d['s']))
            elif kind == 'hang': R.add('R4', 'FAIL', 'w%d hang: %s[%d] main-thread stack dumped (phase %s)' % (w['idx'], d['proc'], d['pid'], pname))
            elif kind == 'gpu_mmu_fault': R.add('R4', 'FAIL', 'w%d GPU MMU fault: %s' % (w['idx'], d['s']))
            elif kind == 'gpu_hw_error': R.add('R4', 'FAIL', 'w%d GPU HW error: %s' % (w['idx'], d['s']))
            elif kind == 'gpu_irq': R.add('R4', 'WARN', 'w%d %s' % (w['idx'], d['s']))
            elif kind == 'jobs' and d['fault']: R.add('R4', 'FAIL', 'w%d GPU JOBS fault=%d (phase %s)' % (w['idx'], d['fault'], pname))
            elif kind == 'hw_autodisable': R.add('R4', 'FAIL', 'w%d HW overlay %s' % (w['idx'], d['s']))
            elif kind == 'hw_backoff_exhausted': R.add('R4', 'FAIL', 'w%d HW overlay %s' % (w['idx'], d['s']))
            elif kind == 'hw_backoff': R.add('R4', 'WARN', 'w%d HW overlay %s' % (w['idx'], d['s']))
            elif kind == 'p90_revert': R.add('R1', 'FAIL', 'w%d panel %s' % (w['idx'], d['s']))
            elif kind == 'p60_forced': R.add('R1', 'FAIL', 'w%d panel forced to 60 Hz: %s' % (w['idx'], d['s']))
            elif kind == 'surf_imbalance': R.add('R4', 'FAIL', 'w%d SURF USE imbalance: %s' % (w['idx'], d['s']))
            elif kind == 'surf_leak': R.add('R4', 'FAIL', 'w%d SURF USE %s' % (w['idx'], d['s']))
            elif kind == 'gpu_holds': holds_min = d['used'] if holds_min is None else min(holds_min, d['used'])
            elif kind == 'pool_full': R.add('R4', 'WARN', 'w%d FB POOL %s' % (w['idx'], d['s']))
            elif kind == 'mem' and d['free'] is not None and t: mem_pts.append((t, d['free'] * 16 / 1024))
            elif kind == 'memgpu' and t:
                if d['private'] is not None: priv_pts.append((t, d['private']))
                if d['bos'] is not None: bos_pts.append((t, d['bos']))
            elif kind == 'pool' and t: pool_pts.append((t, d['used']))
            elif kind == 'slow':
                s = slow[(d['proc'], d['what'])]; s[0] += 1; s[1] = max(s[1], d['ms'])
                if d['ms'] >= 100 and pname in UI_PHASES + ('subway',):
                    R.add('R2' if d['proc'] == game else 'R4', 'WARN', 'w%d slow %s %s %.0f ms (phase %s)' % (w['idx'], d['proc'], d['what'], d['ms'], pname))
            elif kind == 'alloc_slow': alloc[0] += 1; alloc[1] = max(alloc[1], d['us'])
            elif kind == 'photos_fps': fps_photos.append(d['fps'])
            elif kind == 'surf_freeze': prox[0] += 1
            elif kind == 'photos_sampled': prox[1] += d['n']; prox[2] += d['black']
            if game in str(d.get('proc', '')) or (kind == 'slow' and d['proc'] == game): game_seen = True
        if game in text[start:]: game_seen = True
        # per phase rows
        for p in ph:
            ev = by_phase.get(p, [])
            name, o0, o1, t0, t1, play = p
            paces = [d for pos, k, d in ev if k == 'pace' and (name != 'subway' or play is None or pos >= play)]
            used = paces[1:] if len(paces) >= 3 else paces
            v, s = fixed90(used)
            hws = [d for pos, k, d in ev if k == 'hw' and d['hw'] is not None]
            hw_d = cpu_d = flt = None
            if len(hws) >= 2:
                hw_d = hws[-1]['hw'] - hws[0]['hw']; cpu_d = (hws[-1]['cpu'] or 0) - (hws[0]['cpu'] or 0)
                flt = (hws[-1]['faults'] or 0) - (hws[0]['faults'] or 0) + hws[-1]['hard'] - hws[0]['hard']
            pres = sum(d['n'] for pos, k, d in ev if k == 'present')
            secs = max(1e-3, (t1 or t0) - t0)
            tot = {}
            for d in used: PR.add(tot, d['d'])
            afps = tot['active'] / tot['act_us'] * 1e6 if tot.get('active') else None
            row = dict(window=w['idx'], phase=name, seconds=round(secs, 1), pace_records=len(paces), fixed90=v, pace=s, active_fps=round(afps, 1) if afps else None,
                       present_rate=round(pres / secs, 1), hw_frames=hw_d, cpu_frames=cpu_d, hw_pct=(round(100 * hw_d / (hw_d + cpu_d)) if hw_d is not None and hw_d + cpu_d else None),
                       hw_faults=flt, mode=hws[-1]['mode'] if hws else None, p90_reverts=max([d['p90'] for d in paces] + [0]))
            P.append(row)
            if flt: R.add('R4', 'FAIL', 'w%d %s: HW overlay faults +%d (last_fault %s)' % (w['idx'], name, flt, hws[-1]['last_fault']))
            if row['p90_reverts']: R.add('R1', 'FAIL', 'w%d %s: PACE p90_reverts=%d' % (w['idx'], name, row['p90_reverts']))
            if name in UI_PHASES:
                req = 'R3' if name == 'photos' else 'R1'
                if v is None: R.add(req, 'WARN', 'w%d %s: no FIXED90 data (%s)' % (w['idx'], name, s))
                elif v != 'PASS': R.add(req, v, 'w%d %s: %s' % (w['idx'], name, s))
            if name == 'subway':
                if play is None: R.add('R2', 'WARN', 'w%d subway: gameplay never started (phase too short?)' % w['idx'])
                elif afps is None: R.add('R2', 'WARN', 'w%d subway: no PACE data during play' % w['idx'])
                elif afps < 60: R.add('R2', 'FAIL', 'w%d subway: active_fps %.1f < 60' % (w['idx'], afps))
        # screenshots of this window
        hw_ev = [(pos, d) for pos, k, d in E if k == 'hw' and d['hw'] is not None]
        for e in w['steps']:
            if e.get('kind') != 'shot' or not e.get('file'): continue
            off = e.get('log_offset') or 0
            before = [d for pos, d in hw_ev if pos <= off][-1:]; after = [d for pos, d in hw_ev if pos > off][:1]
            mode = before[0]['mode'] if before else (after[0]['mode'] if after else None)
            growing = bool(before and after and (after[0]['hw'] or 0) > (before[0]['hw'] or 0))
            valid = not (mode is not None and mode >= 2 and growing) and not e.get('panel_off')
            shots.append(dict(window=w['idx'], name=e['name'], file=e['file'], checkpoint=e['checkpoint'], mode=mode, valid=valid,
                              why=None if valid else ('panel off' if e.get('panel_off') else 'kern.n2fbhw=%s with hw_frames growing: stale panel buffer' % mode)))
    if not seen_any: print('no console records in any window'); return 2
    if not game_seen and any(r['phase'] == 'subway' for r in P): R.add('R2', 'WARN', 'game process %s never seen in the log' % game)
    if holds_min not in (None, 0): R.add('R4', 'WARN', 'SURF USE gpu holds never back to 0 (min %d)' % holds_min)
    if alloc[1] > 20000: R.add('R4', 'WARN', 'ALLOC SLOW: %d events, max %.1f ms' % (alloc[0], alloc[1] / 1000))
    # memory trends
    trends = {}
    for name, pts in (('gpu_private_MiB', priv_pts), ('free_MiB', mem_pts), ('bos_live', bos_pts), ('fb_pool_used_MiB', pool_pts)):
        s = slope(sorted(pts))
        trends[name] = None if not s else dict(per_hour=round(s[0], 1), r2=round(s[1], 2), growth=round(s[2], 1), span_h=round(s[3], 2), samples=len(pts),
                                                first=sorted(pts)[0][1], last=sorted(pts)[-1][1])
    gp = trends.get('gpu_private_MiB')
    if gp:
        if gp['span_h'] < 1.0:
            if gp['growth'] > 150: R.add('R4', 'WARN', 'GPU private memory grew %d -> %d MiB in %.2f h (span < 1 h: no slope verdict)' % (gp['first'], gp['last'], gp['span_h']))
        elif gp['per_hour'] > 60 and gp['growth'] > 150 and gp['r2'] > 0.5: R.add('R4', 'FAIL', 'GPU private memory leak: %+.0f MiB/h (r2 %.2f), %d -> %d MiB' % (gp['per_hour'], gp['r2'], gp['first'], gp['last']))
        elif gp['per_hour'] > 20 and gp['growth'] > 50: R.add('R4', 'WARN', 'GPU private memory growth: %+.0f MiB/h, %d -> %d MiB' % (gp['per_hour'], gp['first'], gp['last']))
    fp = trends.get('fb_pool_used_MiB')
    if fp and fp['growth'] > 64 and (fp['per_hour'] > 30 or fp['span_h'] < 1.0): R.add('R4', 'WARN', 'FB POOL used grows %+.0f MiB/h (%d -> %d MiB)' % (fp['per_hour'], fp['first'], fp['last']))
    # screenshots
    shot_rows = []
    if '--no-shots' not in argv and shots:
        base_img = shotcheck.load(opt('--baseline')) if opt('--baseline') else None; base_name = opt('--baseline')
        pairs = defaultdict(dict)
        for s in shots:
            m = re.match(r'photos-(\w+)-([ab])$', s['name'])
            if m: pairs[(s['window'], m.group(1), s['checkpoint'] - (1 if m.group(2) == 'b' else 0))][m.group(2)] = s
        keys = sorted(pairs, key=lambda k: (k[0], k[2]))      # window, checkpoint order
        base_note = None
        if base_img is None:
            # candidates: the first 6 valid checkpoints whose two pictures look clean and agree; baseline = medoid by the symmetric
            # defect score (a corrupted first checkpoint would hide its own defects if it were taken blindly)
            cands = []
            for k in keys:
                a, b = pairs[k].get('a'), pairs[k].get('b')
                if not (a and b and a['valid'] and b['valid']): continue
                ia, ib = shotcheck.load(a['file']), shotcheck.load(b['file'])
                if shotcheck.self_ok(shotcheck.analyse(ia)) and shotcheck.self_ok(shotcheck.analyse(ib)) and shotcheck.comparable(ia, ib)[0]:
                    cands.append((a['file'], ia))
                if len(cands) >= 6: break
            if cands:
                tot = []
                for i, (fi, xi) in enumerate(cands):
                    t = 0
                    for j, (fj, xj) in enumerate(cands):
                        if i != j: t += shotcheck.analyse(xj, xi)['score'] + shotcheck.analyse(xi, xj)['score']
                    tot.append(t)
                best = min(range(len(cands)), key=lambda i: (tot[i], i))
                base_name, base_img = cands[best]
                if len(cands) == 2 and tot[best] > 0: base_note = 'only 2 candidate baselines and they disagree: baseline ambiguous'
                if len(cands) == 1: base_note = 'single candidate baseline (not cross-checked)'
        if base_note: R.add('R3', 'WARN', 'screenshots: ' + base_note)
        if base_img is None: R.add('R3', 'WARN', 'screenshots: no valid baseline (no clean Photos checkpoint in CPU-path mode)')
        n_valid = 0
        for k in keys:
            a, b = pairs[k].get('a'), pairs[k].get('b')
            if not (a and b): continue
            row = dict(window=k[0], tag=k[1], a=a['file'], b=b['file'], valid=a['valid'] and b['valid'], why=a['why'] or b['why'])
            if row['valid']:
                ra = shotcheck.analyse(shotcheck.load(a['file']), base_img); rb = shotcheck.analyse(shotcheck.load(b['file']), base_img)
                v, pers, trans, score = shotcheck.pair_verdict(ra, rb)
                row.update(verdict=v, persistent=pers, transient=trans, score=score, comparable=[ra['comparable'], rb['comparable']],
                           defects=[ra['defects'], rb['defects']])
                n_valid += 1
                if v == 'FAIL': R.add('R3', 'FAIL', 'w%d checkpoint %s: thumbnail grid corrupted %s (%s)' % (k[0], k[1], pers, a['file']))
                elif v == 'WARN' and (trans or not ra['grid_found']): R.add('R3', 'WARN', 'w%d checkpoint %s: transient %s' % (k[0], k[1], trans or 'grid not found'))
            shot_rows.append(row)
        if shots and not n_valid: R.add('R3', 'WARN', 'screenshots: none valid (all taken while the overlay composed); see README for the CPU-path checkpoint procedure')
    else:
        base_name = None
    # proxies
    rep = dict(verdict=R.level(), requirements={r: R.level(r) for r in ('R1', 'R2', 'R3', 'R4')}, items=R.items, phases=P, trends=trends,
               slow={'%s %s' % k: dict(count=v[0], max_ms=v[1]) for k, v in sorted(slow.items())}, alloc_slow=dict(count=alloc[0], max_ms=alloc[1] / 1000),
               photos_fps=dict(n=len(fps_photos), min=min(fps_photos) if fps_photos else None, avg=round(sum(fps_photos) / len(fps_photos), 1) if fps_photos else None),
               screenshots=shot_rows, baseline=base_name, boot_info=boot_info[:20], windows=len(W))
    pf, ps, sb = prox; rep['thumb_proxies'] = dict(surf_freeze=pf, photos_sampled=ps, photos_sampled_black=sb)
    if pf: R.add('R3', 'WARN', 'SURF freeze lines: %d (thumbnail converted again before it was finished)' % pf)
    rep['verdict'] = R.level(); rep['requirements'] = {r: R.level(r) for r in ('R1', 'R2', 'R3', 'R4')}; rep['items'] = R.items
    print(render(rep))
    if opt('--json'): Path(opt('--json')).write_text(json.dumps(rep, indent=1, default=str) + '\n')
    return {'PASS': 0, 'FAIL': 1, 'WARN': 3}[rep['verdict']]


def render(rep):
    L = ['STRESS VERDICT: %s   (windows %d)' % (rep['verdict'], rep['windows'])]
    names = dict(R1='UI fixed 90 fps', R2='Subway Surfers >= 60 fps, no crash', R3='Photos grid intact + 90 fps', R4='no crash/glitch/leak/loss')
    for r, v in rep['requirements'].items(): L.append('  %s %-36s %s' % (r, names[r], v))
    L.append('')
    h = ['win', 'phase', 'sec', 'pace', 'FIXED90', 'act_fps', 'pres/s', 'mode', 'hw%', 'hw_flt']
    rows = [h] + [[str(p['window']), p['phase'], '%.0f' % p['seconds'], str(p['pace_records']), (p['fixed90'] or '-') if p['phase'] in UI_PHASES else 'n/a', str(p['active_fps'] or '-'),
                   str(p['present_rate']), str(p['mode'] if p['mode'] is not None else '-'), str(p['hw_pct'] if p['hw_pct'] is not None else '-'),
                   str(p['hw_faults'] if p['hw_faults'] is not None else '-')] for p in rep['phases']]
    wd = [max(len(r[i]) for r in rows) for i in range(len(h))]
    L += ['  ' + '  '.join(c.rjust(wd[i]) for i, c in enumerate(r)) for r in rows]
    L.append('')
    for k, t in rep['trends'].items():
        if t: L.append('  trend %-17s %+8.1f /h  r2 %.2f  %s -> %s  (%d samples, %.2f h)' % (k, t['per_hour'], t['r2'], t['first'], t['last'], t['samples'], t['span_h']))
    if rep['slow']:
        top = sorted(rep['slow'].items(), key=lambda kv: -kv[1]['max_ms'])[:8]
        L.append('  slow events (count/max ms): ' + '; '.join('%s %d/%.0f' % (k, v['count'], v['max_ms']) for k, v in top))
    L.append('  ALLOC SLOW %d (max %.1f ms); Photos app fps %s; thumb proxies %s' % (rep['alloc_slow']['count'], rep['alloc_slow']['max_ms'], rep['photos_fps'], rep['thumb_proxies']))
    sv = [s for s in rep['screenshots'] if s.get('valid')]
    L.append('  screenshots: %d checkpoints, %d valid, baseline %s' % (len(rep['screenshots']), len(sv), rep['baseline']))
    if rep['boot_info']: L.append('  pre-stress (info): ' + '; '.join(rep['boot_info'][:6]))
    L.append('')
    for i in rep['items']:
        if i['level'] in ('FAIL', 'WARN'): L.append('  %s %s: %s%s' % (i['level'], i['req'], i['msg'], '  [x%d, last: %s]' % (i['count'], i['last']) if i['count'] > 1 else ''))
    info = [i for i in rep['items'] if i['level'] == 'INFO']
    if info: L.append('  INFO: ' + '; '.join('%s x%d' % (i['msg'], i['count']) for i in info[:8]))
    return '\n'.join(L)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
