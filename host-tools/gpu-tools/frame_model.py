#!/usr/bin/env python3
"""Device-log calibrated frame-cost model for the Nord2 Metal -> Mali JM UI path.

Host-only, standard library only (no numpy).  Reads the always-on summary
records the frontend prints on the device console (compute_frontend.m
tegu_perf_note):

  TEGU_PERF        window_ms commands jobs gpu_jobs cpu_blits exec_ms gpu_ms cpu_ms
                   shadow_kb batched encode_ms compile_ms compiles ...
  TEGU_PERF_FRAME  kernel_jobs (= gpu_jobs - batched), mean_exec_us ...
  TEGU_PERF_BATCH  enc enc_fs enc_off draws jobs jobs_fs=LOAD/CLEAR jobs_off=LOAD/CLEAR
  TEGU_PERF_BREAK  other (non-generic kernel jobs) ...
  TEGU_PERF_PSO    air_ms mali_ms cache_mem cache_disk cache_pack cache_miss

What the numbers mean (read the source, not the names):
  * gpu_ms   = wall time inside kernel job calls (they return after GPU
               completion): this is submit + GPU + cache-maintenance latency
               of every KERNEL job, NOT pure shader time.
  * encode_ms = wall time between commandBuffer creation and commit summed
               over the window's commands.  It includes CoreAnimation's own work
               between the two calls, pipeline compile stalls (compile_ms) and
               any thread/CPU contention.  It is an upper bound for our
               frontend's encode cost, not a measurement of it.
  * windows are closed by the first command after >=2 s, so
    commands/window_ms is the completion rate seen by the frontend (demand
    driven), not the panel refresh rate.

Model (fit by non-negative least squares, relative-error weights):
  gpu_ms(window)    = sum_class cost[class] * kernel_jobs[class]
                      class in fs_load fs_clear off_load off_clear other
  encode_ms(window) = e_cmd*commands + e_pass*encoders + e_draw*draws
                      (windows with no pipeline compiles only)
  compile_ms        = c_miss*cache_miss + c_disk*cache_disk + c_mem*cache_mem
  cpu_ms            = shadow/cpu-blit term, tiny, reported only

Run `python3 gpu/tools/frame_model.py` for the fit report, `--json` for data.
"""
import argparse
import json
import math
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
GPU = HERE.parent
HW = GPU / 'build/hardware-20261025'

# Logs used for fitting.  c6jb is the only build with render-pass batching
# (kernel jobs < gpu jobs) and the full TEGU_PERF_BATCH/BREAK/PSO records;
# the others are per-draw-job builds on an older kernel, kept as reference
# series for per-kernel-job cost drift (never mixed into the c6jb fit).
FIT_LOGS = ['c6jb-test/ui-usb/xnu-live.log', 'c6jb-test/ui-usb-boot2/xnu-live.log']
REFERENCE_LOGS = {
    'c6hc': ['c6hc-test/ui-usb/xnu-live.log'],
    'c6ic': ['c6ic-test/ui-usb/xnu-live.log', 'c6ic-test/ui-usb-boot2/xnu-live.log',
             'c6ic-test/ui-usb-sameboot-reread/xnu-live.log'],
    'c6gc': ['c6gc-test/ui-usb/xnu-live.log'],
    'c6fc': ['c6fc-test/ui-usb/xnu-live.log'],
}

KV = re.compile(r'(?<![A-Za-z0-9_])([a-z_0-9]+)=(-?\d+)(?=[ |\n/]|$)')
MARK = re.compile(r'TEGU_(PERF(?:_[A-Z]+)?\+?|GPU_MEMORY) ')


def parse_records(path):
    """Yield (kind, {key: first int value}, raw) for each TEGU_PERF* record.

    The console interleaves lines from backboardd and SpringBoard, so a record
    can be torn by another process's text; every key=value token is validated
    on its own and a token cut short by the tear is dropped (never guessed)."""
    text = pathlib.Path(path).read_bytes().decode('latin1')
    for line in text.split('\n'):
        for seg in re.split(r'\|(?=[A-Za-z]+\|)', line):
            m = MARK.search(seg)
            if not m:
                continue
            kv = {}
            for k, v in KV.findall(seg[m.end():]):
                kv.setdefault(k, int(v))      # first wins ('other' appears twice in BREAK)
            ab = re.search(r'ab=([-0-9.]+:[A-Z01]+)', seg)    # in-boot A/B toggle word (ab_schedule.inc)
            if ab:
                kv['_ab'] = ab.group(1)
            fs = re.search(r'max_job_fs=(\S+)', seg)
            if fs:
                kv['_max_job_fs'] = fs.group(1)
            jf = re.search(r'jobs_fs=(\d+)/(\d+)', seg)
            jo = re.search(r'jobs_off=(\d+)/(\d+)', seg)
            if jf:
                kv['fs_load'], kv['fs_clear'] = int(jf.group(1)), int(jf.group(2))
            if jo:
                kv['off_load'], kv['off_clear'] = int(jo.group(1)), int(jo.group(2))
            yield 'TEGU_' + m.group(1), kv, seg


def parse_windows(path):
    """One dict per TEGU_PERF window with the companion records merged in."""
    wins, cur = [], None
    for kind, kv, _ in parse_records(path):
        if kind == 'TEGU_PERF':
            cur = {'P': kv}
            wins.append(cur)
        elif cur is not None and kind in ('TEGU_PERF_FRAME', 'TEGU_PERF_PSO', 'TEGU_PERF_DISK',
                                          'TEGU_PERF_BATCH', 'TEGU_PERF_BREAK'):
            cur.setdefault(kind, kv)
    out = []
    for w in wins:
        P = w['P']
        if not all(k in P for k in ('window_ms', 'commands', 'gpu_jobs', 'gpu_ms')):
            continue                          # torn beyond use; dropped, counted by caller
        B, K, F, S = (w.get('TEGU_PERF_BATCH', {}), w.get('TEGU_PERF_BREAK', {}),
                      w.get('TEGU_PERF_FRAME', {}), w.get('TEGU_PERF_PSO', {}))
        d = {
            'window_ms': P['window_ms'], 'commands': P['commands'], 'gpu_jobs': P['gpu_jobs'],
            'batched': P.get('batched'), 'cpu_blits': P.get('cpu_blits', 0),
            'gpu_ms': P['gpu_ms'], 'exec_ms': P.get('exec_ms'), 'cpu_ms': P.get('cpu_ms'),
            'encode_ms': P.get('encode_ms'), 'compile_ms': P.get('compile_ms'),
            'compiles': P.get('compiles'), 'shadow_kb': P.get('shadow_kb'),
            'max_job_us': P.get('max_job_us'), 'max_command_ms': P.get('max_command_ms'),
            'max_job_fs': P.get('_max_job_fs'),
        }
        kj = F.get('kernel_jobs')
        if kj is None and d['batched'] is not None:
            kj = d['gpu_jobs'] - d['batched']
        d['kernel_jobs'] = kj
        if B:
            d.update(draws=B.get('draws'), encoders=B.get('enc'), enc_fs=B.get('enc_fs'),
                     enc_load=B.get('enc_load'), enc_clear=B.get('enc_clear'),
                     generic_jobs=B.get('jobs'), fs_load=B.get('fs_load'), fs_clear=B.get('fs_clear'),
                     off_load=B.get('off_load'), off_clear=B.get('off_clear'))
            if K.get('other') is not None:
                d['other_jobs'] = K['other']
        if S:
            d.update(air_ms=S.get('air_ms'), mali_ms=S.get('mali_ms'), cache_mem=S.get('cache_mem'),
                     cache_disk=S.get('cache_disk'), cache_pack=S.get('cache_pack'),
                     cache_miss=S.get('cache_miss'))
        out.append(d)
    return out


# ---------------------------------------------------------------- linear algebra
def _solve(A, b):
    n = len(A)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(M[r][i]))
        if abs(M[p][i]) < 1e-12:
            return None
        M[i], M[p] = M[p], M[i]
        for r in range(i + 1, n):
            f = M[r][i] / M[i][i]
            for c in range(i, n + 1):
                M[r][c] -= f * M[i][c]
    x = [0.0] * n
    for i in range(n - 1, -1, -1):
        x[i] = (M[i][n] - sum(M[i][c] * x[c] for c in range(i + 1, n))) / M[i][i]
    return x


def _lstsq(X, y, cols):
    k = len(cols)
    A = [[sum(r[cols[i]] * r[cols[j]] for r in X) for j in range(k)] for i in range(k)]
    for i in range(k):
        A[i][i] += 1e-9
    b = [sum(r[cols[i]] * yy for r, yy in zip(X, y)) for i in range(k)]
    return _solve(A, b)


def nnls(X, y, w=None):
    """Active-set non-negative least squares (small problems)."""
    n = len(X[0])
    if w:
        X = [[v * wi for v in r] for r, wi in zip(X, w)]
        y = [v * wi for v, wi in zip(y, w)]
    active = []
    x = [0.0] * n
    for _ in range(10 * n):
        grad = [sum(r[j] * (yy - sum(r[i] * x[i] for i in range(n))) for r, yy in zip(X, y)) for j in range(n)]
        cand = [j for j in range(n) if j not in active and grad[j] > 1e-9]
        if not cand:
            break
        active.append(max(cand, key=lambda j: grad[j]))
        while True:
            s = _lstsq(X, y, active)
            if s is None:
                active.pop()
                break
            if all(v > 0 for v in s):
                x = [0.0] * n
                for j, v in zip(active, s):
                    x[j] = v
                break
            alpha = min(x[j] / (x[j] - v) for j, v in zip(active, s) if v <= 0 and x[j] - v > 0) if any(
                v <= 0 and x[j] - v > 0 for j, v in zip(active, s)) else 0
            x = [x[j] + alpha * ((s[active.index(j)] if j in active else 0) - x[j]) for j in range(n)]
            active = [j for j in active if x[j] > 1e-12]
            if not active:
                break
    return x


def _metrics(X, y, coef):
    pred = [sum(c * v for c, v in zip(coef, r)) for r in X]
    n = len(y)
    mean = sum(y) / n
    ss_res = sum((a - b) ** 2 for a, b in zip(y, pred))
    ss_tot = sum((a - mean) ** 2 for a in y) or 1.0
    mape = sum(abs(a - b) / max(a, 1.0) for a, b in zip(y, pred)) / n
    return {'n': n, 'r2': 1 - ss_res / ss_tot, 'rmse': math.sqrt(ss_res / n), 'mape': mape}


def fit_model(X, y, names, relative=True):
    w = [1.0 / max(v, 1.0) for v in y] if relative else None
    coef = nnls(X, y, w)
    m = _metrics(X, y, coef)
    # leave-one-window-out: honest out-of-sample error for tiny data sets
    errs = []
    for i in range(len(y)):
        Xi, yi = X[:i] + X[i + 1:], y[:i] + y[i + 1:]
        wi = [1.0 / max(v, 1.0) for v in yi] if relative else None
        ci = nnls(Xi, yi, wi)
        p = sum(c * v for c, v in zip(ci, X[i]))
        errs.append(abs(p - y[i]) / max(y[i], 1.0))
    m['loo_mape'] = sum(errs) / len(errs)
    m['loo_p90'] = sorted(errs)[int(0.9 * (len(errs) - 1))]
    return dict(zip(names, coef)), m


# ---------------------------------------------------------------- the calibrated model
GPU_CLASSES = ['fs_load', 'fs_clear', 'off_load', 'off_clear', 'other']


def _gpu_row(w, classes):
    return [float(w.get(c) or 0) for c in classes]


def build_model(fit_logs=None, root=HW):
    fit_logs = fit_logs or FIT_LOGS
    wins, dropped = [], 0
    for rel in fit_logs:
        p = root / rel
        ws = parse_windows(p)
        for w in ws:
            w['_log'] = rel
        wins += ws
    full = [w for w in wins if w.get('generic_jobs') is not None and w.get('other_jobs') is not None]
    model = {'logs': fit_logs, 'windows_total': len(wins), 'windows_with_batch': len(full)}
    # --- GPU (kernel job) cost, per class.  Several class sets, simplest first.
    y = [float(w['gpu_ms']) for w in full]
    variants = {
        'per_job': (['kernel_jobs'], lambda w: [float(w['kernel_jobs'])]),
        'fs_vs_rest': (['fs_job', 'rest_job'],
                       lambda w: [float((w['fs_load'] or 0) + (w['fs_clear'] or 0)),
                                  float((w['off_load'] or 0) + (w['off_clear'] or 0) + (w['other_jobs'] or 0))]),
        'load_clear': (['load_job', 'clear_job', 'other_job'],
                       lambda w: [float((w['fs_load'] or 0) + (w['off_load'] or 0)),
                                  float((w['fs_clear'] or 0) + (w['off_clear'] or 0)), float(w['other_jobs'] or 0)]),
        'five_class': (GPU_CLASSES, lambda w: [float(w[c] or 0) if c != 'other' else float(w['other_jobs'] or 0)
                                              for c in GPU_CLASSES]),
    }
    gpu_fits = {}
    for name, (cols, fn) in variants.items():
        X = [fn(w) for w in full]
        coef, met = fit_model(X, y, cols)
        gpu_fits[name] = {'coef_ms_per_job': coef, **met}
    model['gpu_fits'] = gpu_fits
    # pick the simplest variant whose leave-one-out error is within 10% (relative) of the best
    best = min(v['loo_mape'] for v in gpu_fits.values())
    chosen = next(n for n in ('per_job', 'fs_vs_rest', 'load_clear', 'five_class')
                  if gpu_fits[n]['loo_mape'] <= best * 1.10 + 1e-9)
    model['gpu_chosen'] = chosen
    # --- encode: windows with no compiles (compile_ms==0).  upper bound, see header.
    enc = [w for w in full if (w.get('compile_ms') or 0) == 0 and w.get('encode_ms') is not None
           and w['window_ms'] < 20000]
    if len(enc) >= 4:
        X = [[float(w['commands']), float(w['encoders']), float(w['draws'])] for w in enc]
        yy = [float(w['encode_ms']) for w in enc]
        coef, met = fit_model(X, yy, ['e_cmd', 'e_pass', 'e_draw'])
        coef1, met1 = fit_model([[r[2]] for r in X], yy, ['e_draw'])
        model['encode_fit'] = {'coef_ms': coef, **met}
        model['encode_fit_draw_only'] = {'coef_ms': coef1, **met1}
        model['encode_windows'] = len(enc)
    # --- compile stalls
    cw = [w for w in full if w.get('cache_miss') is not None and w.get('compile_ms') is not None]
    if len(cw) >= 4:
        X = [[float(w['cache_miss']), float(w['cache_disk']), float(w['cache_mem'])] for w in cw]
        yy = [float(w['compile_ms']) for w in cw]
        coef, met = fit_model(X, yy, ['c_miss', 'c_disk', 'c_mem'])
        model['compile_fit'] = {'coef_ms': coef, **met}
        miss = [w for w in cw if w['cache_miss']]
        if miss:
            model['compile_per_miss_air_ms'] = sum(w['air_ms'] for w in miss) / sum(w['cache_miss'] for w in miss)
            model['compile_per_miss_mali_ms'] = sum(w['mali_ms'] for w in miss) / sum(w['cache_miss'] for w in miss)
    return model, full


def predict_gpu_ms(model, shape):
    """shape: dict with per-class kernel job counts (fs_load, fs_clear, off_load, off_clear,
    other_jobs) or just kernel_jobs."""
    ch = model['gpu_chosen']
    coef = model['gpu_fits'][ch]['coef_ms_per_job']
    g = lambda k: float(shape.get(k) or 0)
    if ch == 'per_job':
        return coef['kernel_jobs'] * (g('kernel_jobs') or sum(g(k) for k in
                                                                 ('fs_load', 'fs_clear', 'off_load', 'off_clear', 'other_jobs')))
    if ch == 'fs_vs_rest':
        return coef['fs_job'] * (g('fs_load') + g('fs_clear')) + coef['rest_job'] * (g('off_load') + g('off_clear') + g('other_jobs'))
    if ch == 'load_clear':
        return (coef['load_job'] * (g('fs_load') + g('off_load')) + coef['clear_job'] * (g('fs_clear') + g('off_clear')) +
                coef['other_job'] * g('other_jobs'))
    return sum(coef[c] * g(c if c != 'other' else 'other_jobs') for c in GPU_CLASSES)


def predict_encode_ms(model, commands, encoders, draws, host_ratio=1.0):
    c = model.get('encode_fit', {}).get('coef_ms')
    if not c:
        return None
    return (c['e_cmd'] * commands + c['e_pass'] * encoders + c['e_draw'] * draws) * host_ratio


# ---------------------------------------------------------------- what-if scenarios
# Kernel-agent inputs (host analysis, NOT measured on device yet): GPU pinned at 350 MHz; no GPU IRQ so
# WAIT polls with IOSleep(1) (~1-2.5 ms per job, P3/patch 0090 polls at 20 us and saves ~2.4-2.6 ms per
# small job); WAIT_SEQ async = submit all, wait once.  The fitted per-job cost (fs-size 5.75 ms, rest
# 3.39 ms) is split as  cost = wait_ms + compute;  compute scales ~1/clock.
SCEN_DEFAULTS = dict(
    clock_mhz=350.0,       # GPU clock (device: 350 pinned; P3 boot-arg 654 or 886)
    wait_ms=2.5,           # per-job WAIT quantization cost today (kernel agent: 2.4-2.6 ms saved per small job)
    async_wait=False,      # WAIT_SEQ: one wait per frame instead of one per kernel job
    wait_floor_ms=0.02,    # polling quantum with P3 (20 us)
    jobs_scale=1.0,        # multiply the frame's kernel job count (e.g. a batching improvement)
    encode_scale=1.0,      # multiply the fitted encode ms/draw (SHA-256 removal: host 416-634 -> 10-37 ms = ~0.05)
    preload_area=1.0,      # fraction of the full-target area still preloaded/written back (intersect preload)
    preload_share=0.5,     # ASSUMPTION: share of the full-screen job's area-proportional time that is preload+writeback
    scalable=1.0,          # share of compute time that scales with GPU clock (1.0 = kernel agent's linear claim)
)


def scenario_ms(model, shape, draws, passes=0, **over):
    """Predicted device ms for one frame under what-if parameters.
    shape: fs_load fs_clear off_load off_clear other_jobs (kernel jobs per frame)."""
    p = dict(SCEN_DEFAULTS, **over)
    c = model['gpu_fits']['fs_vs_rest']['coef_ms_per_job']
    cur_wait = SCEN_DEFAULTS['wait_ms']
    s_rest = max(c['rest_job'] - cur_wait, 0.2)          # compute part of a small job (today)
    s_fs = max(c['fs_job'] - cur_wait, s_rest)           # compute part of a full-screen job (today)
    area = s_fs - s_rest                                 # area-proportional part of a full-screen job
    s_fs_load = s_fs - p['preload_share'] * area * (1.0 - p['preload_area'])
    f = (p['scalable'] * SCEN_DEFAULTS['clock_mhz'] / p['clock_mhz'] + (1.0 - p['scalable']))
    g = lambda k: float(shape.get(k) or 0) * p['jobs_scale']
    n_fs_load, n_fs_clear = g('fs_load'), g('fs_clear')
    n_rest = g('off_load') + g('off_clear') + g('other_jobs')
    compute = f * (s_fs_load * n_fs_load + s_fs * n_fs_clear + s_rest * n_rest)
    jobs = n_fs_load + n_fs_clear + n_rest
    quantum = max(p['wait_ms'], p['wait_floor_ms'])
    wait = quantum if p['async_wait'] else quantum * jobs   # async: one wait per frame
    e = model.get('encode_fit_draw_only', {}).get('coef_ms', {}).get('e_draw', 0.0)
    enc = e * draws * p['encode_scale']
    gpu = compute + wait
    return {'gpu_ms': gpu, 'gpu_compute_ms': compute, 'gpu_wait_ms': wait, 'enc_ms': enc, 'jobs': jobs,
            'serial_ms': gpu + enc, 'pipelined_ms': max(gpu, enc),
            'fps_serial': 1000.0 / (gpu + enc) if gpu + enc else 0.0, 'fps_pipelined': 1000.0 / max(gpu, enc, 1e-9)}


# Columns.  The first is the code that ran on the device (the anchor); '+tree' is the current tree with its MEASURED
# host encode change and job structure; every further lever is on top of the tree.  Assumptions listed above.
W = SCEN_DEFAULTS['wait_floor_ms']
PATH = [
    ('c6jb anchor', dict(anchor=True)),
    ('+tree', dict(encode_scale='fix')),
    ('+P3 polling', dict(encode_scale='fix', wait_ms=W)),
    ('+654 MHz', dict(encode_scale='fix', wait_ms=W, clock_mhz=654)),
    ('+async wait', dict(encode_scale='fix', wait_ms=W, clock_mhz=654, async_wait=True)),
    ('+isect preload', dict(encode_scale='fix', wait_ms=W, clock_mhz=654, async_wait=True, preload_area=0.25)),
    ('same @886', dict(encode_scale='fix', wait_ms=W, clock_mhz=886, async_wait=True, preload_area=0.25)),
]
SINGLE = [
    ('c6jb anchor', dict(anchor=True)), ('tree', dict(encode_scale='fix')),
    ('tree+P3 poll', dict(encode_scale='fix', wait_ms=W)), ('tree+654', dict(encode_scale='fix', clock_mhz=654)),
    ('tree+886', dict(encode_scale='fix', clock_mhz=886)), ('tree+async', dict(encode_scale='fix', async_wait=True)),
    ('tree+preload', dict(encode_scale='fix', preload_area=0.25)), ('tree+jobs x.5', dict(encode_scale='fix', jobs_scale=0.5)),
]


def whatif_row(model, item, ov):
    """item = (name, shape, draws, passes[, measured_encode_scale[, anchor_shape, anchor_draws]]).  Returns scenario_ms()."""
    name, shape, draws, passes = item[:4]
    measured = item[4] if len(item) > 4 else None
    ov = dict(ov)
    if ov.pop('anchor', False):
        if len(item) > 6 and item[5]:
            shape, draws = item[5], item[6]
        return scenario_ms(model, shape, draws, passes, **ov)
    return scenario_ms(model, shape, draws, passes, **resolve(ov, measured))


def parse_overrides(text):
    """'clock_mhz=886,async_wait=1,encode_scale=fix' -> dict (keys must be in SCEN_DEFAULTS; fix = measured/assumed encode change)."""
    ov = {}
    for item in filter(None, (text or '').split(',')):
        k, v = item.split('=')
        if k not in SCEN_DEFAULTS:
            raise SystemExit('unknown what-if parameter %r; choose from %s' % (k, ', '.join(SCEN_DEFAULTS)))
        ov[k] = bool(int(v)) if k == 'async_wait' else (v if v == 'fix' else float(v))
    return ov


def resolve(ov, measured=None):
    """encode_scale='fix' -> the measured host change vs the device-run anchor, else the assumed 0.05."""
    ov = dict(ov)
    if ov.get('encode_scale') == 'fix':
        ov['encode_scale'] = measured if measured is not None else ENCODE_FIX_ASSUMED
    return ov


ENCODE_FIX_ASSUMED = 0.05


def scenario_table(model, shapes, out=sys.stdout, title='', extra=None):
    """shapes: list of (name, shape dict, draws, passes)."""
    p = lambda *a: print(*a, file=out)
    extra = [('custom', extra)] if extra else []
    for label, cols in (('cumulative path', PATH + extra), ('each lever alone from current device', SINGLE + extra)):
        p('')
        p('== What-if %s %s: frame ms serial (fps serial/pipelined) ==' % (label, title))
        p('%-12s %s' % ('frame', ' '.join('%-18s' % n for n, _ in cols)))
        for item in shapes:
            name = item[0]
            cells = []
            for _, ov in cols:
                r = whatif_row(model, item, ov)
                cells.append('%6.0f (%4.1f/%4.1f)' % (r['serial_ms'], r['fps_serial'], r['fps_pipelined']))
            p('%-12s %s' % (name, ' '.join('%-18s' % c for c in cells)))
    p('  levers (ASSUMPTIONS from the kernel/render agents, not device-measured): wait quantum %.1f ms/job today, '
      '%.2f ms with P3, compute scales 1/clock (scalable=%.1f), preload share of fs-job area term %.1f, intersect preload area %.2f, '
      'encode fix = %s of the fitted %.2f ms/draw.' % (
          SCEN_DEFAULTS['wait_ms'], SCEN_DEFAULTS['wait_floor_ms'], SCEN_DEFAULTS['scalable'], SCEN_DEFAULTS['preload_share'], 0.25,
          'MEASURED host instruction ratio vs the anchor (per row: %s)' % ', '.join('%s %.3f' % (i[0], i[4]) for i in shapes if len(i) > 4 and i[4] is not None) if any(len(i) > 4 and i[4] is not None for i in shapes) else 'ASSUMED x0.05', model.get('encode_fit_draw_only', {}).get('coef_ms', {}).get('e_draw', 0.0)))


def device_shapes(wins):
    """Mean per-command shape of each structural device frame type (steady windows) as scenario_table input."""
    groups = {}
    for w in wins:
        if (w.get('compile_ms') or 0) == 0 and w.get('generic_jobs') is not None:
            groups.setdefault(frame_type(w).split(' ')[0], []).append(w)
    out = []
    for t in ('tiny', 'idle_tick', 'small', 'medium', 'large'):
        ws = groups.get(t)
        if not ws:
            continue
        c = sum(w['commands'] for w in ws)
        mean = lambda k: sum((w.get(k) or 0) for w in ws) / c
        shape = {'fs_load': mean('fs_load'), 'fs_clear': mean('fs_clear'), 'off_load': mean('off_load'),
                 'off_clear': mean('off_clear'), 'other_jobs': mean('other_jobs')}
        out.append(('dev ' + t, shape, mean('draws'), mean('encoders')))
    return out


# ---------------------------------------------------------------- frame typing
def frame_type(w):
    """Structural class of a device window (per-command means).  Labels the log
    cannot support (lock screen / home / control centre / app) are NOT claimed:
    the console carries no wall clock and no UI-state markers."""
    c = max(w['commands'], 1)
    dpc = (w.get('draws') or 0) / c
    rate = c * 1000.0 / max(w['window_ms'], 1)
    if dpc <= 6 and w['window_ms'] >= 30000:
        return 'idle_tick (<=6 draws, 1 cmd/min)'
    if dpc <= 6:
        return 'tiny (<=6 draws)'
    if dpc <= 25:
        return 'small (7-25 draws)'
    if dpc <= 60:
        return 'medium (26-60 draws)'
    return 'large (>60 draws)'


def device_fps_table(model, wins):
    """Per frame type: observed device ms/frame and implied fps (steady = no-compile windows)."""
    rows = {}
    for w in wins:
        t = frame_type(w)
        c = max(w['commands'], 1)
        rows.setdefault(t, []).append(w)
    out = []
    for t, ws in sorted(rows.items()):
        steady = [w for w in ws if (w.get('compile_ms') or 0) == 0]
        def agg(sel):
            if not sel:
                return None
            cm = sum(w['commands'] for w in sel)
            gpu = sum(w['gpu_ms'] for w in sel) / cm
            enc = sum((w.get('encode_ms') or 0) for w in sel) / cm
            dr = sum((w.get('draws') or 0) for w in sel) / cm
            kj = sum((w.get('kernel_jobs') or 0) for w in sel) / cm
            return {'windows': len(sel), 'commands': cm, 'draws_per_cmd': dr, 'kernel_jobs_per_cmd': kj,
                    'gpu_ms_per_cmd': gpu, 'encode_ms_per_cmd': enc}
        a, s = agg(ws), agg(steady)
        out.append({'type': t, 'all': a, 'steady': s})
    return out


def report(model, wins, ref=None, out=sys.stdout):
    p = lambda *a: print(*a, file=out)
    p('== Device-log calibrated frame-cost model ==')
    p('fit logs: %s  windows=%d (with BATCH/BREAK records=%d)' % (', '.join(model['logs']),
                                                               model['windows_total'], model['windows_with_batch']))
    p('')
    p('GPU kernel-job cost (gpu_ms per window = sum cost*jobs):')
    for n, f in model['gpu_fits'].items():
        mark = ' <== chosen' if n == model['gpu_chosen'] else ''
        cs = '  '.join('%s=%.2f' % (k, v) for k, v in f['coef_ms_per_job'].items())
        p('  %-11s R2=%.3f RMSE=%.0fms MAPE=%.1f%% LOO-MAPE=%.1f%%  [%s]%s' % (n, f['r2'], f['rmse'], 100 * f['mape'],
                                                                         100 * f['loo_mape'], cs, mark))
    if 'encode_fit' in model:
        f, f1 = model['encode_fit'], model['encode_fit_draw_only']
        p('Encode (commit-create wall, no-compile windows, n=%d; UPPER BOUND, includes CA time/contention):' % model['encode_windows'])
        p('  cmd+pass+draw R2=%.3f MAPE=%.0f%% LOO=%.0f%% [%s]' % (f['r2'], 100 * f['mape'], 100 * f['loo_mape'],
                                                              '  '.join('%s=%.2f' % kv for kv in f['coef_ms'].items())))
        p('  draw only     R2=%.3f MAPE=%.0f%% LOO=%.0f%% [%s]' % (f1['r2'], 100 * f1['mape'], 100 * f1['loo_mape'],
                                                              '  '.join('%s=%.2f' % kv for kv in f1['coef_ms'].items())))
    if 'compile_fit' in model:
        f = model['compile_fit']
        p('Compile stalls (compile_ms): R2=%.3f MAPE=%.0f%% [%s]; per miss AIR %.0f ms + Mali %.0f ms' % (
            f['r2'], 100 * f['mape'], '  '.join('%s=%.1f' % kv for kv in f['coef_ms'].items()),
            model.get('compile_per_miss_air_ms', 0), model.get('compile_per_miss_mali_ms', 0)))
    p('')
    p('Device windows by structural frame type (per command; steady = no compile in window):')
    p('  %-34s %5s %5s %7s %6s %9s %9s %7s' % ('type', 'win', 'cmds', 'draws', 'kjobs', 'gpu ms', 'enc ms', 'fps'))
    for r in device_fps_table(model, wins):
        for lab, a in (('all', r['all']), ('steady', r['steady'])):
            if not a:
                continue
            tot = a['gpu_ms_per_cmd'] + a['encode_ms_per_cmd']
            p('  %-34s %5d %5d %7.1f %6.1f %9.1f %9.1f %7.2f' % ((r['type'] + ' ' + lab)[:34], a['windows'], a['commands'],
                                                              a['draws_per_cmd'], a['kernel_jobs_per_cmd'],
                                                              a['gpu_ms_per_cmd'], a['encode_ms_per_cmd'],
                                                              1000.0 / tot if tot else 0))
    if ref:
        p('')
        p('Reference per-draw-job builds (older kernel): gpu_ms per kernel job')
        for k, v in ref.items():
            p('  %-5s windows=%3d  gpu_ms/job mean=%.2f  median=%.2f' % (k, v['n'], v['mean'], v['median']))


def reference_series(root=HW):
    out = {}
    for name, logs in REFERENCE_LOGS.items():
        r = []
        for rel in logs:
            p = root / rel
            if not p.exists():
                continue
            for w in parse_windows(p):
                kj = w['kernel_jobs'] if w['kernel_jobs'] is not None else w['gpu_jobs']
                if kj > 0:
                    r.append(w['gpu_ms'] / kj)
        if r:
            r.sort()
            out[name] = {'n': len(r), 'mean': sum(r) / len(r), 'median': r[len(r) // 2]}
    return out


# ---------------------------------------------------------------- pixel / bandwidth GPU model
# The class fit above prices a kernel job by its class only (fs 5.75 ms, other 3.39 ms at 350 MHz,
# IOSleep wait included), so merging 36-128 draws into one job looks free.  This section prices
# each job from what it does, using a per-job census of a host replay (fsjobs.py: target tiles,
# tiles covered by the draws' scissors, bytes per pixel, preload / pre-frame / clean-tile mode,
# pixels and Mesa shaderdb fragment statistics per draw):
#   job_ms = F + overlap(T_compute, T_bw)
#   T_compute = [sum_draws px*cover*c(shader) + preload_px*c_pre + tiles*t_tile] / (cores*f*eff)
#               + draws*t_draw*(350/f)
#   c(shader) = max(fma, cvt, sfu, v, t, ls*w_ls, 1/px_per_clk)   (Mesa va_count_stats units:
#               core clocks per fragment; Mesa has no G77 entry and falls back to G57 rates
#               2 px / 4 texel / 32 FMA per clock per core - the same Valhall core class)
#   T_bw = (preload bytes + write-back bytes + in-memory fetch bytes) / BW
#   Preload: chained full-screen job reads and dirties every tile; INTERSECT pre-frame reads and
#   (clean_fragment_write) writes back only tiles with geometry (scissor union = upper bound);
#   CLEAR (clean tile write) writes back every tile.  F (per kernel job, not clock-scaled) and
#   BW are calibrated so that a today-style full-screen LOAD job and a small offscreen job
#   reproduce the c6jb class costs (fs 5.75, rest 3.39 ms minus the 2.5 ms IOSleep wait).
# Every other constant is an ASSUMPTION with a low/mid/high range; the output is a range.
G77 = dict(cores=9,            # Mali-G77 MC9 (GPU id 0x90800011, docs: shader_present fallback MP9)
           px_per_clk=2.0)     # Mesa pan_model G57 rates (no G77 entry); G77 core class
PIXEL_RANGES = {               # (optimistic, central, pessimistic)
    'w_ls': (1 / 16.0, 0.25, 1.0),      # Mesa counts LS per thread at 1/clk; warp-uniform loads are ~1/16
    'eff': (1.0, 0.8, 0.6),             # shader-core utilisation (warp occupancy, tile imbalance)
    'cover': (0.6, 0.8, 1.0),           # shaded fraction of each draw's scissor box
    't_draw_us': (2.0, 5.0, 10.0),      # per-draw vertex/tiler/state cost at 350 MHz (clock-scaled)
    'F_ms': (0.2, 0.5, 0.7),            # per kernel job fixed cost (job start, cache maintenance), not clock-scaled;
                                        # unmeasured split of the 0.89 ms rest job (0089 JOBS line will measure it)
    'overlap': (0.0, 0.5, 0.5),         # job = F + max(Tc,Tbw) + overlap*min(Tc,Tbw)
    'bw_gbs': (16.0, 12.0, 10.0),       # effective GPU DRAM bandwidth, not clock-scaled; c6jb fs jobs (22 MB of
                                        # preload+write-back in <=3.25 ms) bound it to >= ~9.5 GB/s
}
PIXEL_C_PRE = 0.25                      # preload FS (both variants): Mesa shaderdb 0.25 cycles (texture)
PIXEL_CAL = dict(fs_ms=5.75, rest_ms=3.39, wait_ms=2.5, fs_draws=1.5, fs_draw_px=22500.0, fs_bpp=4, fs_tiles=10863)


def pixel_params(level):
    """level 0/1/2 = optimistic/central/pessimistic."""
    return {k: v[level] for k, v in PIXEL_RANGES.items()}


def _pixel_cycles(stats, w_ls, px_per_clk):
    s = stats or {}
    return max(s.get('fma', 0), s.get('cvt', 0), s.get('sfu', 0), s.get('v', 0), s.get('t', 0),
               s.get('ls', 0) * w_ls, 1.0 / px_per_clk)


def _is_full(job):
    return job['tiles'] >= PIXEL_CAL['fs_tiles']


def pixel_job_terms(job, stats, prm, clock_mhz, tile_fetch=True, gpu=G77):
    """(T_compute_ms, bytes) of one census job; prm carries the calibrated t_tile and s_off."""
    tiles, covered, bpp = job['tiles'], job['covered'], job['bpp']
    px_tile = 256
    if job['clean']:                       # CLEAR: no preload, every tile written back
        pre_px, pre_b, wb_b = 0, 0, tiles * px_tile * bpp
    elif job['preload']:                   # chained full-screen preload job: all tiles read, dirtied
        pre_px = tiles * px_tile
        pre_b, wb_b = pre_px * bpp, pre_px * bpp
    elif job.get('preframe', 0) == 2:      # INTERSECT: only tiles with geometry
        pre_px = covered * px_tile
        pre_b, wb_b = pre_px * bpp, pre_px * bpp
    else:                                  # ALWAYS pre-frame / no preload
        pre_px = tiles * px_tile if job.get('preframe') else 0
        pre_b, wb_b = pre_px * bpp, covered * px_tile * bpp
    scale = 1.0 if _is_full(job) else prm.get('s_off', 1.0)
    shade = 0.0
    fetch_b = 0
    for d in job['draws']:
        px = d['px'] * prm['cover'] * scale
        shade += px * _pixel_cycles(stats.get(d['shader']), prm['w_ls'], gpu['px_per_clk'])
        if d['shader'] == 'luma' and not tile_fetch:
            fetch_b += px * bpp            # in-place memory fetch of the own pixel
    cyc_pre = max(PIXEL_C_PRE, 1.0 / gpu['px_per_clk'])
    cycles = shade + pre_px * cyc_pre + tiles * prm.get('t_tile', 0.0)
    t_c = cycles / (gpu['cores'] * clock_mhz * 1e3 * prm['eff']) + len(job['draws']) * prm['t_draw_us'] * 1e-3 * 350.0 / clock_mhz
    return t_c, pre_b + wb_b + fetch_b


def _pixel_combine(t_c, t_b, overlap):
    return max(t_c, t_b) + overlap * min(t_c, t_b)


def _pixel_job_ms(job, stats, prm, clock_mhz, tile_fetch):
    t_c, b = pixel_job_terms(job, stats, prm, clock_mhz, tile_fetch)
    return prm['F_ms'] + _pixel_combine(t_c, b / (prm['bw_gbs'] * 1e9) * 1e3, prm['overlap'])


def _bisect(f, lo, hi, n=60):
    """Root of increasing f on [lo, hi]; clamps to the interval."""
    if f(lo) >= 0:
        return lo
    if f(hi) <= 0:
        return hi
    for _ in range(n):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if f(mid) < 0 else (lo, mid)
    return (lo + hi) / 2


def pixel_calibrate(level, stats, before_jobs, cal=PIXEL_CAL, tile_fetch=False):
    """Two device facts (c6jb class costs at 350 MHz minus the 2.5 ms IOSleep wait) fix two unknowns:
    t_tile (core clocks per tile visited) from a today-style full-screen LOAD job (chained preload,
    fs_draws fixed-shader draws of fs_draw_px), and s_off (pixel scale of the replay's offscreen
    draws, <=1) from the mean of the BEFORE census' offscreen jobs.  Clamped values are reported."""
    prm = pixel_params(level)
    prm['s_off'] = 1.0
    job = {'tiles': cal['fs_tiles'], 'covered': cal['fs_tiles'], 'bpp': cal['fs_bpp'], 'clean': 0, 'preload': 1,
           'preframe': 0, 'draws': [{'px': cal['fs_draw_px'], 'shader': 'fixed'}] * int(round(cal['fs_draws']))}
    fs_target = cal['fs_ms'] - cal['wait_ms']
    def f_tile(t):
        prm['t_tile'] = t
        return _pixel_job_ms(job, stats, prm, 350.0, True) - fs_target
    prm['t_tile'] = _bisect(f_tile, 0.0, 5000.0)
    notes = []
    if f_tile(prm['t_tile']) > 0.05:
        notes.append('fs job over-predicted by %.2f ms at t_tile=0' % f_tile(prm['t_tile']))
    off = [j for j in before_jobs if not _is_full(j)]
    if off:
        rest_target = cal['rest_ms'] - cal['wait_ms']
        def f_off(sc):
            prm['s_off'] = sc
            return sum(_pixel_job_ms(j, stats, prm, 350.0, tile_fetch) for j in off) / len(off) - rest_target
        prm['s_off'] = _bisect(f_off, 0.0, 1.0)
        r = f_off(prm['s_off'])
        if abs(r) > 0.05:
            notes.append('offscreen mean off by %+.2f ms at s_off=%.2f' % (r, prm['s_off']))
    return prm, '; '.join(notes)


def pixel_frame_ms(jobs, stats, prm, clock_mhz, wait_ms, async_wait, tile_fetch):
    total = sum(_pixel_job_ms(j, stats, prm, clock_mhz, tile_fetch) for j in jobs)
    waits = wait_ms if async_wait else wait_ms * len(jobs)
    return total + waits


def pixel_stress(jobs, px_scale=4.0):
    """Structural bound: display draws cover px_scale times their replay scissor area and every
    tile has geometry (an INTERSECT pre-frame preload then saves nothing)."""
    out = []
    for j in jobs:
        j = dict(j)
        if _is_full(j):
            j['covered'] = j['tiles']
            j['draws'] = [dict(d, px=min(d['px'] * px_scale, j['tiles'] * 256)) for d in j['draws']]
        out.append(j)
    return out


def pixel_table(before_path, after_path, out=sys.stdout):
    p = lambda *a: print(*a, file=out)
    B, A = json.load(open(before_path)), json.load(open(after_path))
    stats = dict(B.get('shader_stats') or {}, **(A.get('shader_stats') or {}))
    before_all = [j for js in B['scenarios'].values() for j in js]
    prms = []
    p('== Pixel/bandwidth GPU model (per job from the host census; ranges = optimistic/central/pessimistic) ==')
    for lvl, name in enumerate(('optimistic', 'central', 'pessimistic')):
        prm, note = pixel_calibrate(lvl, stats, before_all, tile_fetch=B.get('tile_fetch', False))
        prms.append(prm)
        p('  %-11s t_tile=%.0f clk/tile  s_off=%.2f  %s' % (name, prm['t_tile'], prm['s_off'], note))
        p('              %s' % {k: round(v, 3) for k, v in prm.items() if k not in ('t_tile', 's_off')})
    p('  fragment cost (Mesa shaderdb, core clocks/fragment, w_ls 1/16..1): ' + ', '.join(
        '%s %.2f..%.2f' % (k, _pixel_cycles(v, 1 / 16.0, G77['px_per_clk']), _pixel_cycles(v, 1.0, G77['px_per_clk']))
        for k, v in sorted(stats.items())))
    rows = []
    p('%-10s %5s %5s | %-24s | %-24s %-24s %-24s | %-24s' % ('scenario', 'jobsB', 'jobsA', 'before @350 (today waits)',
                                                     'after @350 P3+async', 'after @654', 'after @886', 'stress after @654'))
    fmt = lambda r: '%5.1f [%5.1f..%5.1f]' % (r[1], min(r), max(r))
    for scn in A['scenarios']:
        jb, ja = B['scenarios'].get(scn, []), A['scenarios'][scn]
        rng = lambda jobs, clock, wait, asy, tile: tuple(pixel_frame_ms(jobs, stats, prm, clock, wait, asy, tile) for prm in prms)
        before = rng(jb, 350.0, PIXEL_CAL['wait_ms'], False, B.get('tile_fetch', False))
        after = [rng(ja, c, SCEN_DEFAULTS['wait_floor_ms'], True, A.get('tile_fetch', True)) for c in (350.0, 654.0, 886.0)]
        stress = rng(pixel_stress(ja), 654.0, SCEN_DEFAULTS['wait_floor_ms'], True, A.get('tile_fetch', True))
        rows.append({'scenario': scn, 'jobs_before': len(jb), 'jobs_after': len(ja), 'before_350': before,
                     'after_350': after[0], 'after_654': after[1], 'after_886': after[2], 'stress_after_654': stress})
        p('%-10s %5d %5d | %-24s | %-24s %-24s %-24s | %-24s' % (scn, len(jb), len(ja), fmt(before), fmt(after[0]), fmt(after[1]),
                                                               fmt(after[2]), fmt(stress)))
    p('  cells: GPU ms/frame central [min..max over the three parameter sets]; before = %s at 350 MHz with %.1f ms IOSleep wait per job;'
      ' after = %s with P3 polling (%.2f ms) and one async wait per frame.' % (before_path, PIXEL_CAL['wait_ms'], after_path,
                                                                            SCEN_DEFAULTS['wait_floor_ms']))
    p('  bandwidth and F are not clock-scaled; replay geometry = scissor boxes, not CoreAnimation geometry; s_off rescales the'
      ' replay offscreen (blur) pixel load so the BEFORE offscreen jobs match the device rest-job mean.')
    p('  stress = display draws x4 pixels and every tile covered (no INTERSECT saving): a structural bound for real CA geometry.')
    p('  t_tile absorbs every per-tile cost of the calibration job and is charged to every visited tile after the change too'
      ' (conservative for INTERSECT jobs, whose geometry-free tiles do no preload/write-back).')
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--windows', action='store_true', help='dump parsed fit windows')
    ap.add_argument('--pixel', nargs=2, metavar=('BEFORE_CENSUS', 'AFTER_CENSUS'),
                    help='pixel/bandwidth GPU model from two fsjobs.py census JSONs')
    ap.add_argument('--what-if', default='', help='extra custom column, e.g. clock_mhz=886,wait_ms=0.02,async_wait=1,encode_scale=0.05,jobs_scale=0.5,preload_area=0.25')
    a = ap.parse_args()
    if a.pixel:
        pixel_table(*a.pixel)
        return
    model, wins = build_model()
    ref = reference_series()
    if a.json:
        json.dump({'model': model, 'reference': ref}, sys.stdout, indent=1)
        print()
        return
    report(model, wins, ref)
    scenario_table(model, device_shapes(wins), title='(device-observed shapes)', extra=parse_overrides(a.what_if))
    if a.windows:
        for w in wins:
            print(json.dumps(w, sort_keys=True))


if __name__ == '__main__':
    main()
