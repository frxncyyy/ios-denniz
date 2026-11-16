#!/usr/bin/env python3
"""Per-A/B-phase analysis of a device UI log (xnu-live.log) from an in-boot
A/B run (metal/tegu/ab_schedule.inc; Nord2Metal.bundle/ABSchedule.conf).

Reads the frontend's TEGU_PERF window records (TEGU_PERF, _FRAME, _BATCH,
_BREAK, _PSO, _DISK, _ENCODE; every one tagged ab=<phase>.<cycle>:PF.NI.AP.TF.JB.BM
since the A/B build), TEGU_AB / TEGU_AB_DISABLED, TEGU_COMMAND_FAILED,
TEGU_GPU_MEMORY(_TOP), TEGU_PSO_MISS and the kernel's NORD2 GPU JOBS stats line
(kernel 0089/0093/0094: q/run/tot/clean/inv/wait/ret us, gpu_khz, bat_cC,
gpu_cC, soc_cC, lvts, irq). Kernel lines carry no tag: each one belongs to the
toggle word last seen in the log (console order).

Output: per toggle word (all cycles of the same variant pooled) windows,
commands ("frames": the frontend counts command buffers; CA usually commits
one per display frame plus offscreen ones), draws, kernel jobs, full-screen vs
other jobs, gpu_ms per kernel job, our encode (TEGU_PERF_ENCODE draw_ms) per
draw and CA's own time (TEGU_PERF encode_ms = commit-create minus our draw
time, per command), commands/s and ms/command, failures/disabled toggles,
memory, PSO misses, kernel timings; then baseline vs each variant with deltas,
and a refit of the per-job GPU cost from the kernel run_us/tot_us.
A log without ab tags (pre-A/B builds, e.g. c6jb) is one 'untagged' variant.

Usage: analyze_ab_run.py LOG [--json OUT.json] [--windows] [--skip-initial] [--proc NAME|all]
  --proc          frontend records of one process (default backboardd when the
                  log has any, the compositor whose frames matter; 'all' pools
                  every process). Kernel stats lines are system-wide and kept.
  --skip-initial  drop phase 0 (boot baseline: first frames, pipeline compiles)
                  from the baseline pool (default: kept separately as 'boot').
"""
import argparse
import json
import math
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import frame_model as fm  # noqa: E402  (KV regex, nnls)

KV = fm.KV
KJ = re.compile(r'(?<![A-Za-z0-9_])([A-Za-z_0-9]+)=(-?\d+)(?=[ |\n/:~]|$)')  # kernel line: bat_cC etc.
AB = re.compile(r'ab=([-0-9.]+):((?:[A-Z]{2}[01])+)')
SEG = re.compile(r'\|(?=[A-Za-z][A-Za-z0-9._-]*\|)')
PROC = re.compile(r'^([A-Za-z][A-Za-z0-9._-]*)\|')
TOGGLE_NAMES = {'PF': 'NORD2_METAL_PREFRAME', 'NI': 'TEGU_NATIVE_INDEXED', 'AP': 'TEGU_ASYNC_PIPELINE',
                'TF': 'TEGU_TILE_FETCH', 'JB': 'TEGU_JM_BATCH', 'BM': 'NORD2_METAL_BATCH_MAX(128)',
                'EX': 'NORD2_METAL_EXTENT'}


def toggles(word):
    """'PF0NI1AP0TF0JB1BM0' -> {'PF': 0, 'NI': 1, ...}."""
    return {word[i:i + 2]: int(word[i + 2]) for i in range(0, len(word), 3)}


def kv(text):
    out = {}
    for k, v in KV.findall(text):
        out.setdefault(k, int(v))
    return out


def pair(text, key):
    """key=a/b -> (a, b) ints, or None."""
    m = re.search(r'(?<![A-Za-z0-9_])' + key + r'=(-?\d+)/(-?\d+)', text)
    return (int(m.group(1)), int(m.group(2))) if m else None


def parse(path):
    """Events in console order: dicts with 'type', 'proc', 'ab' (phase, cycle,
    word) or None, and their fields."""
    text = pathlib.Path(path).read_bytes().decode('latin1')
    events = []
    for line in text.split('\n'):
        for seg in SEG.split(line):
            pm = PROC.match(seg)
            proc = pm.group(1) if pm else None
            body = seg[pm.end():] if pm else seg
            ab = AB.search(body)
            tag = None
            if ab:
                ph = ab.group(1)
                if ph == '-':
                    tag = (None, None, ab.group(2))
                else:
                    p, _, c = ph.partition('.')
                    tag = (int(p), int(c or 0), ab.group(2))
            if 'NORD2 GPU JOBS:' in body:
                d = {}
                for k_, v_ in KJ.findall(body[body.index('NORD2 GPU JOBS:') + 15:]):
                    d.setdefault(k_, int(v_))
                for key in ('q_us', 'run_us', 'tot_us', 'clean_us', 'inv_us', 'gap_us', 'wait_us', 'ret_us'):
                    pr = pair(body, key)
                    if pr:
                        d[key], d[key + '_max'] = pr
                m = re.search(r'seqw=(\d+)/(\d+)/(\d+)/(\d+)', body)
                if m:
                    d['seq_waits'], d['seq_jobs'], d['seq_us'], d['seq_us_max'] = map(int, m.groups())
                m = re.search(r'irq=(\d+):(\d+)/(\d+)/(\d+)/(\d+)', body)
                if m:
                    d['irq_state'], d['irq_tops'], d['irq_spurious'], d['irq_sleeps'], d['irq_timeouts'] = map(int, m.groups())
                m = re.search(r'lvts=(\d+)(~?)', body)
                if m:
                    d['lvts'], d['lvts_default_cal'] = int(m.group(1)), m.group(2) == '~'
                events.append(dict(type='KJOBS', proc=proc, ab=None, **d))
                continue
            m = re.search(r'TEGU_(PERF(?:_[A-Z]+)?|AB_DISABLED|AB|COMMAND_FAILED|GPU_MEMORY_TOP|GPU_MEMORY|PSO_MISS)\b', body)
            if not m:
                continue
            kind = m.group(1)
            rest = body[m.end():]
            d = kv(rest)
            if kind == 'PERF_BATCH':
                jf, jo = re.search(r'jobs_fs=(\d+)/(\d+)', rest), re.search(r'jobs_off=(\d+)/(\d+)', rest)
                if jf:
                    d['fs_load'], d['fs_clear'] = int(jf.group(1)), int(jf.group(2))
                if jo:
                    d['off_load'], d['off_clear'] = int(jo.group(1)), int(jo.group(2))
            elif kind == 'AB_DISABLED':
                t = re.search(r'toggle=(\S+)', rest)
                d['toggle'] = t.group(1) if t else '?'
            elif kind == 'PSO_MISS':
                d['stage'] = rest.split()[0] if rest.split() else '?'
            elif kind == 'GPU_MEMORY':
                s = re.search(r'site=(\S+)', rest)
                d['site'] = s.group(1) if s else '?'
            elif kind == 'PERF':
                f = re.search(r'max_job_fs=(\S+)', rest)
                d['max_job_fs'] = f.group(1) if f else None
            events.append(dict(type=kind, proc=proc, ab=tag, **d))
    return events


def group_key(tag):
    if tag is None:
        return 'untagged'
    return tag[2]


def analyze(events, proc=None):
    """Per toggle-word pools and per-window rows."""
    pools, windows, last = {}, [], None
    cur = {}                                   # proc -> current window row (companion records)
    schedule = []

    def pool(key):
        return pools.setdefault(key, dict(windows=0, window_ms=0, commands=0, gpu_jobs=0, kernel_jobs=0,
                                          batched=0, gpu_ms=0, exec_ms=0, cpu_ms=0, encode_ms=0, compile_ms=0,
                                          compiles=0, shadow_kb=0, draws=0, fs_jobs=0, off_jobs=0, generic_jobs=0,
                                          other_jobs=0, enc_draws=0, enc_draw_ms=0, enc_plan_ms=0, enc_variant_ms=0,
                                          enc_window_ms=0, plan_hit=0, plan_miss=0, failures=0, disabled=[],
                                          pso_air=0, pso_mali=0, pso_miss_ms=0, mem_private_kib=0, mem_bos=0,
                                          kjobs=[], phases=set(), cycles=set(), procs=set(), max_job_us=0))
    for e in events:
        tag = e.get('ab')
        if tag is not None:
            last = tag
        t = e['type']
        key = group_key(tag if tag is not None else last)
        if t == 'AB':
            schedule.append({k: v for k, v in e.items() if k not in ('type', 'ab')})
            continue
        if proc and e.get('proc') and e['proc'] != proc:
            continue                                # other UI process (kernel lines have none: kept)
        P = pool(key)
        if tag is not None and tag[0] is not None:
            P['phases'].add(tag[0])
            P['cycles'].add(tag[1])
        if e.get('proc'):
            P['procs'].add(e['proc'])
        if t == 'PERF':
            if not all(k in e for k in ('window_ms', 'commands', 'gpu_jobs', 'gpu_ms')):
                continue                            # torn record
            row = dict(key=key, phase=tag[0] if tag else None, cycle=tag[1] if tag else None, proc=e.get('proc'),
                       window_ms=e['window_ms'], commands=e['commands'], gpu_jobs=e['gpu_jobs'],
                       batched=e.get('batched', 0), gpu_ms=e['gpu_ms'], exec_ms=e.get('exec_ms', 0),
                       encode_ms=e.get('encode_ms', 0), compile_ms=e.get('compile_ms', 0))
            row['kernel_jobs'] = row['gpu_jobs'] - (row['batched'] or 0)
            windows.append(row)
            cur[e.get('proc')] = row
            P['windows'] += 1
            for k in ('window_ms', 'commands', 'gpu_jobs', 'gpu_ms', 'exec_ms', 'cpu_ms', 'encode_ms', 'compile_ms',
                      'compiles', 'shadow_kb'):
                P[k] += e.get(k, 0) or 0
            P['batched'] += e.get('batched', 0) or 0
            P['kernel_jobs'] += row['kernel_jobs']
            P['max_job_us'] = max(P['max_job_us'], e.get('max_job_us', 0) or 0)
        elif t == 'PERF_FRAME':
            row = cur.get(e.get('proc'))
            if row is not None and 'kernel_jobs' in e and row.get('_frame') is None:
                row['_frame'] = 1
                P['kernel_jobs'] += e['kernel_jobs'] - row['kernel_jobs']   # FRAME's count is authoritative
                row['kernel_jobs'] = e['kernel_jobs']
        elif t == 'PERF_BATCH':
            row = cur.get(e.get('proc'))
            if row is not None and row.get('_batch') is None:
                row['_batch'] = 1
                fs = e.get('fs_load', 0) + e.get('fs_clear', 0)
                off = e.get('off_load', 0) + e.get('off_clear', 0)
                row.update(draws=e.get('draws', 0), fs_jobs=fs, off_jobs=off)
                P['draws'] += e.get('draws', 0)
                P['fs_jobs'] += fs
                P['off_jobs'] += off
                P['generic_jobs'] += e.get('jobs', 0)
        elif t == 'PERF_BREAK':
            row = cur.get(e.get('proc'))
            if row is not None and row.get('_break') is None and 'other' in e:
                row['_break'] = 1
                P['other_jobs'] += e['other']
        elif t == 'PERF_PSO':
            P['pso_air'] += e.get('air_ms', 0)
            P['pso_mali'] += e.get('mali_ms', 0)
        elif t == 'PERF_ENCODE':
            P['enc_draws'] += e.get('draws', 0)
            P['enc_draw_ms'] += e.get('draw_ms', 0)
            P['enc_plan_ms'] += e.get('plan_ms', 0)
            P['enc_variant_ms'] += e.get('variant_ms', 0)
            P['enc_window_ms'] += e.get('window_ms', 0)
            P['plan_hit'] += e.get('plan_hit', 0)
            P['plan_miss'] += e.get('plan_miss', 0)
        elif t == 'COMMAND_FAILED':
            P['failures'] += 1
        elif t == 'AB_DISABLED':
            P['disabled'].append(e.get('toggle'))
        elif t == 'PSO_MISS':
            P['pso_miss_ms'] += e.get('ms', 0)
        elif t == 'GPU_MEMORY':
            P['mem_private_kib'] = max(P['mem_private_kib'], e.get('private_kib', 0))
            P['mem_bos'] = max(P['mem_bos'], e.get('bos', 0))
        elif t == 'KJOBS':
            P['kjobs'].append(e)
    # boot baseline (phase 0) separately unless asked to drop it
    out = {}
    for key, P in pools.items():
        P['phases'], P['cycles'], P['procs'] = sorted(P['phases']), sorted(P['cycles']), sorted(P['procs'])
        out[key] = P
    return out, windows, schedule


def derived(P):
    d = {}
    c = P['commands'] or 0
    d['cmd_per_s'] = 1000.0 * c / P['window_ms'] if P['window_ms'] else None
    d['ms_per_cmd'] = P['exec_ms'] / c if c else None
    d['kjobs_per_cmd'] = P['kernel_jobs'] / c if c else None
    d['fs_per_cmd'] = P['fs_jobs'] / c if c else None
    d['draws_per_cmd'] = P['draws'] / c if c else None
    d['gpu_ms_per_kjob'] = P['gpu_ms'] / P['kernel_jobs'] if P['kernel_jobs'] else None
    d['our_enc_ms_per_draw'] = P['enc_draw_ms'] / P['enc_draws'] if P['enc_draws'] else None
    # CA's own CPU between commandBuffer and commit, per command: encode_ms
    # (commit - create) minus our draw time (TEGU_PERF_ENCODE draw_ms; the
    # ENCODE windows are time-sliced per process, so this is an estimate).
    d['ca_ms_per_cmd'] = (P['encode_ms'] - P['enc_draw_ms']) / c if c and P['enc_window_ms'] else None
    d['commit_create_ms_per_cmd'] = P['encode_ms'] / c if c else None
    d['plan_hit_rate'] = P['plan_hit'] / (P['plan_hit'] + P['plan_miss']) if P['plan_hit'] + P['plan_miss'] else None
    K = P['kjobs']
    n = sum(k.get('n', 0) for k in K)
    if n:
        for f in ('q_us', 'run_us', 'tot_us', 'clean_us', 'inv_us'):
            d['k_' + f] = sum(k.get(f, 0) * k.get('n', 0) for k in K) / n
        w = sum(k.get('waits', 0) for k in K)
        d['k_wait_us'] = sum(k.get('wait_us', 0) * k.get('waits', 0) for k in K) / w if w else None
        d['k_ret_us'] = sum(k.get('ret_us', 0) * k.get('waits', 0) for k in K) / w if w else None
        d['k_jobs'] = n
        d['k_full_share'] = sum(k.get('full', 0) for k in K) / n
        d['k_faults'] = sum(k.get('fault', 0) for k in K)
        for f in ('gpu_khz', 'bat_cC', 'gpu_cC', 'soc_cC', 'lvts', 'irq_state'):
            vals = [k[f] for k in K if f in k and k[f] != -9999]
            if vals:
                d['k_' + f + '_last'] = vals[-1]
                d['k_' + f + '_max'] = max(vals)
    return d


def refit(pools):
    """Per-job GPU cost from the kernel stats: n*run_us = a*full + b*(n-full)."""
    X, y_run, y_tot = [], [], []
    for P in pools.values():
        for k in P['kjobs']:
            n, full = k.get('n', 0), k.get('full', 0)
            if n < 2 or 'run_us' not in k:
                continue
            X.append([full, n - full])
            y_run.append(n * k['run_us'] / 1000.0)
            y_tot.append(n * k.get('tot_us', 0) / 1000.0)
    if len(X) < 3:
        return None
    out = {}
    for name, y in (('run', y_run), ('tot', y_tot)):
        coef, m = fm.fit_model(X, y, ['fs_ms', 'other_ms'])
        out[name] = dict(coef, r2=m['r2'], mape=m['mape'], loo_mape=m['loo_mape'], windows=m['n'])
    return out


def label(word, base):
    if word == 'untagged':
        return 'untagged'
    if word == base:
        return 'baseline'
    a, b = toggles(word), toggles(base) if base not in (None, 'untagged') else {}
    diff = ['%s%d' % (t, v) for t, v in a.items() if b.get(t) != v]
    return '+'.join(diff) or word


def fmt(v, nd=1):
    if v is None:
        return '-'
    if isinstance(v, float):
        return ('%.' + str(nd) + 'f') % v
    return str(v)


def report(path, pools, windows, schedule, out=sys.stdout, show_windows=False, skip_initial=False):
    p = lambda *a: print(*a, file=out)
    p('== %s ==' % path)
    for s in schedule[:1]:
        p('schedule: ' + ' '.join('%s=%s' % (k, v) for k, v in s.items() if k != 'proc'))
    # baseline word: the boot phase (phase 0) if present, else the word with most windows
    base = None
    for w in windows:
        if w['phase'] == 0:
            base = w['key']
            break
    if base is None and pools:
        base = max(pools, key=lambda k: pools[k]['windows'])
    # boot phase separated from the cycling baseline (pipeline compiles, first frames)
    if base in pools and any(w['phase'] == 0 for w in windows) and any(
            w['key'] == base and w['phase'] not in (0, None) for w in windows):
        boot = {k: 0 for k in ('windows', 'commands', 'window_ms', 'compile_ms')}
        for w in windows:
            if w['key'] == base and w['phase'] == 0:
                boot['windows'] += 1
                boot['commands'] += w['commands']
                boot['window_ms'] += w['window_ms']
                boot['compile_ms'] += w['compile_ms']
        p('boot baseline (phase 0): windows=%d commands=%d window_ms=%d compile_ms=%d%s' % (
            boot['windows'], boot['commands'], boot['window_ms'], boot['compile_ms'],
            ' (excluded from baseline: --skip-initial)' if skip_initial else ' (pooled in baseline)'))
        if skip_initial:
            P = pools[base]
            for w in windows:
                if w['key'] == base and w['phase'] == 0:
                    for k in ('window_ms', 'commands', 'gpu_jobs', 'gpu_ms', 'exec_ms', 'encode_ms', 'compile_ms',
                              'kernel_jobs'):
                        P[k] -= w.get(k, 0) or 0
                    P['windows'] -= 1
                    P['draws'] -= w.get('draws', 0) or 0
                    P['fs_jobs'] -= w.get('fs_jobs', 0) or 0
                    P['off_jobs'] -= w.get('off_jobs', 0) or 0
    order = sorted(pools, key=lambda k: (k != base, k))
    D = {k: derived(pools[k]) for k in order}
    p('')
    p('%-22s %4s %6s %7s %6s %6s %6s %7s %8s %7s %7s %7s %7s %5s %4s' % (
        'variant', 'win', 'cmds', 'cmd/s', 'ms/cmd', 'kj/cmd', 'fs/cmd', 'dr/cmd', 'gpu/kj', 'enc/dr', 'CA/cmd',
        'pso_ms', 'memKiB', 'fail', 'dis'))
    for k in order:
        P, d = pools[k], D[k]
        p('%-22s %4d %6d %7s %6s %6s %6s %7s %8s %7s %7s %7d %7d %5d %4d' % (
            label(k, base)[:22], P['windows'], P['commands'], fmt(d['cmd_per_s']), fmt(d['ms_per_cmd']),
            fmt(d['kjobs_per_cmd'], 2), fmt(d['fs_per_cmd'], 2), fmt(d['draws_per_cmd']), fmt(d['gpu_ms_per_kjob'], 2),
            fmt(d['our_enc_ms_per_draw'], 3), fmt(d['ca_ms_per_cmd']), P['pso_miss_ms'], P['mem_private_kib'],
            P['failures'], len(set(P['disabled']))))
    p('  cmd/s = command buffers per second of window (CA commits about one per display frame plus offscreen ones);')
    p('  ms/cmd = execution ms per command; gpu/kj = gpu_ms per kernel job; enc/dr = our encode ms per draw')
    p('  (TEGU_PERF_ENCODE); CA/cmd = commit-create (encode_ms) minus our draw time, per command (estimate).')
    if any(pools[k]['kjobs'] for k in order):
        p('')
        p('%-22s %6s %7s %7s %7s %7s %7s %7s %7s %6s %7s %6s %6s %5s %4s' % (
            'kernel (NORD2 GPU JOBS)', 'jobs', 'q_us', 'run_us', 'tot_us', 'clean', 'inv', 'wait', 'ret', 'full%',
            'gpu_khz', 'bat_C', 'gpu_C', 'lvts', 'irq'))
        for k in order:
            d = D[k]
            if 'k_jobs' not in d:
                continue
            p('%-22s %6d %7s %7s %7s %7s %7s %7s %7s %6s %7s %6s %6s %5s %4s' % (
                label(k, base)[:22], d['k_jobs'], fmt(d.get('k_q_us'), 0), fmt(d.get('k_run_us'), 0),
                fmt(d.get('k_tot_us'), 0), fmt(d.get('k_clean_us'), 0), fmt(d.get('k_inv_us'), 0),
                fmt(d.get('k_wait_us'), 0), fmt(d.get('k_ret_us'), 0), fmt(100 * d['k_full_share'], 0),
                fmt(d.get('k_gpu_khz_last')), fmt(d['k_bat_cC_max'] / 100 if 'k_bat_cC_max' in d else None),
                fmt(d['k_gpu_cC_max'] / 100 if 'k_gpu_cC_max' in d else None), fmt(d.get('k_lvts_last')),
                fmt(d.get('k_irq_state_last'))))
    # baseline vs variants
    if base in D and len(order) > 1:
        p('')
        p('== baseline vs each variant (delta = variant - baseline; % of baseline) ==')
        metrics = [('cmd_per_s', 'cmd/s', 1), ('ms_per_cmd', 'ms/cmd', 1), ('kjobs_per_cmd', 'kjobs/cmd', 2),
                   ('fs_per_cmd', 'fs/cmd', 2), ('gpu_ms_per_kjob', 'gpu ms/kjob', 2),
                   ('our_enc_ms_per_draw', 'enc ms/draw', 3), ('commit_create_ms_per_cmd', 'commit-create ms/cmd', 1),
                   ('ca_ms_per_cmd', 'CA ms/cmd', 1),
                   ('k_run_us', 'k run_us', 0), ('k_tot_us', 'k tot_us', 0), ('k_ret_us', 'k ret_us', 0)]
        b = D[base]
        for k in order[1:]:
            d = D[k]
            parts = []
            for m, name, nd in metrics:
                if d.get(m) is None or b.get(m) is None:
                    continue
                delta = d[m] - b[m]
                rel = ' (%+.0f%%)' % (100 * delta / b[m]) if b[m] else ''
                parts.append('%s %s->%s%s' % (name, fmt(b[m], nd), fmt(d[m], nd), rel))
            dis = sorted(set(pools[k]['disabled']))
            p('%-22s windows=%d cmds=%d: %s%s' % (label(k, base), pools[k]['windows'], pools[k]['commands'],
                                                 '; '.join(parts) or 'no comparable windows',
                                                 ('; DISABLED ' + ','.join(dis)) if dis else ''))
    fit = refit(pools)
    p('')
    if fit:
        p('== per-job GPU cost refit from the kernel stats (n*avg = fs_ms*full + other_ms*(n-full)) ==')
        for name, f in fit.items():
            p('%s_us: fs %.2f ms, other %.2f ms per job (R2 %.2f, MAPE %.0f%%, LOO %.0f%%, %d windows)' % (
                name, f['fs_ms'], f['other_ms'], f['r2'], 100 * f['mape'], 100 * f['loo_mape'], f['windows']))
        p('  (frame_model c6jb fit priced 5.75 ms per full-screen and 3.39 ms per other job incl. waits)')
    else:
        p('per-job refit: needs >=3 NORD2 GPU JOBS lines with n>=2 (kernel 0089+); none usable in this log')
    if show_windows:
        p('')
        p('%-22s %5s %5s %6s %5s %5s %6s %6s %6s' % ('window', 'phase', 'cycle', 'ms', 'cmds', 'kjobs', 'gpu_ms',
                                                    'exec', 'enc'))
        for w in windows:
            p('%-22s %5s %5s %6d %5d %5d %6d %6d %6d' % (label(w['key'], base)[:22], fmt(w['phase']), fmt(w['cycle']),
                                                         w['window_ms'], w['commands'], w['kernel_jobs'], w['gpu_ms'],
                                                         w['exec_ms'], w['encode_ms']))
    return dict(base=base, variants={k: dict(label=label(k, base), derived=D[k],
                                             **{x: v for x, v in pools[k].items() if x != 'kjobs'})
                                     for k in order}, refit=fit, schedule=schedule)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log', type=pathlib.Path)
    ap.add_argument('--json', type=pathlib.Path)
    ap.add_argument('--windows', action='store_true', help='also list every window')
    ap.add_argument('--skip-initial', action='store_true', help='drop the boot baseline (phase 0) from the baseline pool')
    ap.add_argument('--proc', default=None, help="process name, or 'all' (default: backboardd if present)")
    a = ap.parse_args(argv)
    events = parse(a.log)
    proc = a.proc
    if proc is None:
        proc = 'backboardd' if any(e.get('proc') == 'backboardd' for e in events) else 'all'
    pools, windows, schedule = analyze(events, None if proc == 'all' else proc)
    print('process: %s' % proc)
    res = report(a.log, pools, windows, schedule, show_windows=a.windows, skip_initial=a.skip_initial)
    if a.json:
        a.json.write_text(json.dumps(res, indent=1, default=lambda o: sorted(o) if isinstance(o, set) else str(o)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
