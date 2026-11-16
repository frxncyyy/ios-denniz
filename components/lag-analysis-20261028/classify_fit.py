#!/usr/bin/env python3
"""Window classes and per-command cost fits over the perfwin JSON of one or
more runs.

Classes (backboardd TEGU_PERF windows with >= 8 commands):
  flat      kernel jobs/cmd <= 3            (one composite pass, no blur pyramid)
  blurfail  failed blur-pyramid commands: TEGU_COMMAND_FAILED in the window or
            kj/cmd >= 35 with exec per kernel job < 0.45 ms (jobs counted, command aborted)
  heavy     everything else (multi-pass: backdrop capture + blur/downsample + composite)
Fits (least squares, heavy windows only, per command):
  exec_ms      = a*kj + b*screens + c      (frontend execute, serial)
  k_ret_ms     = a*kj + b*screens          (kernel submit -> wait return, all jobs)
  k_run_ms     = a*kj + b*screens          (GPU run + <=1 poll interval)
  residual_ms  = exec - k_ret   = a*kj + b*draws (+c)
usage: classify_fit.py PERF_JSON... [--json OUT]"""
import argparse
import json
import pathlib
import statistics as st


def solve(rows, ys):
    n = len(rows[0])
    A = [[sum(r[i] * r[j] for r in rows) for j in range(n)] for i in range(n)]
    b = [sum(r[i] * y for r, y in zip(rows, ys)) for i in range(n)]
    for i in range(n):  # Gauss-Jordan
        p = max(range(i, n), key=lambda k: abs(A[k][i]))
        A[i], A[p], b[i], b[p] = A[p], A[i], b[p], b[i]
        for k in range(n):
            if k != i and A[i][i]:
                f = A[k][i] / A[i][i]
                A[k] = [x - f * y for x, y in zip(A[k], A[i])]
                b[k] -= f * b[i]
    coef = [b[i] / A[i][i] for i in range(n)]
    pred = [sum(c * x for c, x in zip(coef, r)) for r in rows]
    my = sum(ys) / len(ys)
    ss = sum((y - my) ** 2 for y in ys) or 1
    r2 = 1 - sum((y - p) ** 2 for y, p in zip(ys, pred)) / ss
    return coef, r2


def cls(w):
    c = w.get('commands') or 0
    kj = w.get('kj_per_cmd') or 0
    ex = w.get('exec_per_cmd') or 0
    if w.get('fail') or (kj >= 35 and kj and ex / kj < 0.45):
        return 'blurfail'
    if kj <= 3:
        return 'flat'
    return 'heavy'


def q(vals, p):
    vals = sorted(v for v in vals if v is not None)
    if not vals:
        return None
    k = (len(vals) - 1) * p
    lo = int(k)
    hi = min(lo + 1, len(vals) - 1)
    return round(vals[lo] + (vals[hi] - vals[lo]) * (k - lo), 2)


def summary(ws):
    out = {'windows': len(ws), 'commands': sum(w['commands'] for w in ws),
           'seconds': round(sum(w['window_ms'] for w in ws) / 1000, 1)}
    c = out['commands'] or 1
    tot = lambda k: sum(w.get(k) or 0 for w in ws)  # noqa: E731
    out['cmd_per_s'] = round(out['commands'] / out['seconds'], 1) if out['seconds'] else None
    out['present_cmd_per_s'] = round(tot('pass_present') / out['seconds'], 1) if out['seconds'] else None
    out['kj_per_cmd'] = round(tot('frame_kernel_jobs') / c, 2)
    out['exec_ms_per_cmd'] = round(tot('exec_ms') / c, 2)
    out['passes_per_cmd'] = round(sum(sum(w.get('pass_att') or [0]) for w in ws) / c, 2)
    out['draws_per_cmd'] = round(tot('batch_draws') / c, 1)
    out['screens_per_cmd'] = round(sum((w.get('rt_tiles') or [0, 0, 0])[2] for w in ws) / 10863 / c, 2)
    out['fs_jobs_per_cmd'] = round(sum(sum(w.get('batch_jobs_fs') or [0]) for w in ws) / c, 2)
    out['off_jobs_per_cmd'] = round(sum(sum(w.get('batch_jobs_off') or [0]) for w in ws) / c, 2)
    out['present_us_mean'] = round(sum((w.get('pass_present_us') or 0) * (w.get('pass_present') or 0) for w in ws)
                                   / max(1, tot('pass_present')))
    out['present_queue_us_mean'] = round(sum((w.get('pass_queue_us') or 0) * (w.get('pass_present') or 0) for w in ws)
                                         / max(1, tot('pass_present')))
    kn = sum(w['k_n'] for w in ws if w.get('k_run_us') is not None)
    for f in ('q_us', 'run_us', 'clean_us', 'inv_us', 'gap_us', 'ret_us', 'wait_us'):
        out['k_' + f] = round(sum(w['k_n'] * w['k_' + f] for w in ws if w.get('k_' + f) is not None) / max(1, kn), 1)
    out['k_ovh_us'] = round(out['k_ret_us'] - out['k_run_us'], 1)
    out['k_ret_ms_per_cmd'] = round(sum(w['k_n'] * w['k_ret_us'] for w in ws if w.get('k_ret_us')) / 1000 / c, 2)
    out['k_run_ms_per_cmd'] = round(sum(w['k_n'] * w['k_run_us'] for w in ws if w.get('k_run_us')) / 1000 / c, 2)
    out['user_ms_per_cmd'] = round(out['exec_ms_per_cmd'] - out['k_ret_ms_per_cmd'], 2)
    out['user_ms_per_kj'] = round(out['user_ms_per_cmd'] / out['kj_per_cmd'], 3) if out['kj_per_cmd'] else None
    out['gpu_busy'] = round(sum(w['k_n'] * w['k_run_us'] for w in ws if w.get('k_run_us')) / 1e6 / out['seconds'], 3) if out['seconds'] else None
    out['exec_busy'] = round(tot('exec_ms') / 1000 / out['seconds'], 3) if out['seconds'] else None
    out['cpu_ms_per_cmd'] = round(tot('cpu_ms') / c, 2)
    out['shadow_kb_per_s'] = round(tot('shadow_kb') / out['seconds'], 1) if out['seconds'] else None
    out['exec_p50'] = q([w['exec_per_cmd'] for w in ws], .5)
    out['exec_p90'] = q([w['exec_per_cmd'] for w in ws], .9)
    out['kj_p50'] = q([w['kj_per_cmd'] for w in ws], .5)
    out['cmd_per_s_p50'] = q([w['cmd_per_s'] for w in ws], .5)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('perf', nargs='+')
    ap.add_argument('--json')
    a = ap.parse_args()
    res = {}
    allheavy = []
    for p in a.perf:
        ws = [w for w in json.loads(pathlib.Path(p).read_text()) if (w.get('commands') or 0) >= 8 and w.get('window_ms', 0) >= 1500]
        for w in ws:
            w['class'] = cls(w)
        r = {k: summary([w for w in ws if w['class'] == k]) for k in ('flat', 'heavy', 'blurfail')}
        heavy = [w for w in ws if w['class'] == 'heavy' and w.get('k_ret_us') is not None]
        allheavy += heavy
        res[p] = r
    fits = {}
    H = allheavy
    rows = [[w['kj_per_cmd'], w['screens_per_cmd'], 1.0] for w in H]
    fits['exec_ms = a*kj + b*screens + c'] = solve(rows, [w['exec_per_cmd'] for w in H])
    rows2 = [[w['kj_per_cmd'], w['screens_per_cmd']] for w in H]
    fits['k_run_ms = a*kj + b*screens'] = solve(rows2, [w['k_n'] * w['k_run_us'] / 1000 / w['commands'] for w in H])
    fits['k_ret_ms = a*kj + b*screens'] = solve(rows2, [w['k_n'] * w['k_ret_us'] / 1000 / w['commands'] for w in H])
    rows3 = [[w['kj_per_cmd'], w['draws_per_cmd'], 1.0] for w in H]
    fits['user_ms = a*kj + b*draws + c'] = solve(rows3, [w['exec_per_cmd'] - w['k_n'] * w['k_ret_us'] / 1000 / w['commands'] for w in H])
    rows4 = [[w['kj_per_cmd'], 1.0] for w in H]
    fits['user_ms = a*kj + c'] = solve(rows4, [w['exec_per_cmd'] - w['k_n'] * w['k_ret_us'] / 1000 / w['commands'] for w in H])
    fits['exec_ms = a*kj + c'] = solve(rows4, [w['exec_per_cmd'] for w in H])
    for p, r in res.items():
        print('==', p)
        keys = list(r['heavy'].keys())
        print(f"{'metric':22s} {'flat':>10s} {'heavy':>10s} {'blurfail':>10s}")
        for k in keys:
            print(f"{k:22s} " + ' '.join(f"{str(r[c].get(k)):>10s}" for c in ('flat', 'heavy', 'blurfail')))
    print('== fits over heavy windows of all runs, n =', len(H))
    for k, (coef, r2) in fits.items():
        print(f"{k:34s} coef={[round(x, 3) for x in coef]} R2={r2:.2f}")
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps({'classes': res, 'fits': {k: {'coef': v[0], 'r2': v[1]} for k, v in fits.items()},
                                                    'n_heavy': len(H)}, indent=1))


if __name__ == '__main__':
    main()
