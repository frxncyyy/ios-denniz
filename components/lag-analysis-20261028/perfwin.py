#!/usr/bin/env python3
"""Per-TEGU_PERF-window (>=2 s, backboardd) table: commands, kernel jobs, passes,
targets, tiles, failures, plus the kernel GPU JOBS lines printed since the
previous window.  usage: perfwin.py LOG [--json OUT] [--csv OUT]"""
import argparse
import csv
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import lagparse as lp  # noqa: E402

F = lp.first
FULL_TILES = 10863  # 1125x2436 in 16x16 tiles (TEGU_PERF_RT tiles of a full-screen job)


def windows(evs):
    out, cur, kern, misc = [], None, [], {'fail': 0, 'fail_jobs': 0, 'refused': 0, 'pso_ms': 0, 'pso_n': 0,
                                           'fallback_blur': 0, 'sb_nil': 0, 'sb_flow': 0, 'sb_tr': 0}
    for e in evs:
        p, t, kv = e['proc'], e['type'], e.get('kv') or {}
        if e['text'].startswith('NORD2 GPU JOBS') and isinstance(kv.get('ret_us'), tuple):
            kern.append(kv)
            continue
        if p == 'SpringBoard':
            if 'nil layer' in e['text']:
                misc['sb_nil'] += 1
            elif 'UICollectionViewFlowLayout is not defined' in e['text']:
                misc['sb_flow'] += 1
            elif 'CATransformLayer' in e['text']:
                misc['sb_tr'] += 1
            continue
        if p != 'backboardd' or not t:
            continue
        if t == 'TEGU_PERF':
            cur = {'line': e['line'], 't': e.get('t'), **{k: F(v) for k, v in kv.items() if k != 'ab'}}
            cur['max_job_fs'] = (e['text'].split('max_job_fs=')[1].split(' ')[0] if 'max_job_fs=' in e['text'] else '')
            cur.update(misc)
            n = sum(k['n'] for k in kern)
            cur['k_n'] = n
            for f in ('q_us', 'run_us', 'tot_us', 'clean_us', 'inv_us', 'ret_us', 'gap_us', 'wait_us'):
                cur['k_' + f] = round(sum(k['n'] * F(k[f]) for k in kern if f in k) / n, 1) if n else None
            cur['k_win_ms'] = sum(F(k.get('win_ms', 0)) for k in kern)
            sq = [k['seqw'] for k in kern if isinstance(k.get('seqw'), tuple) and len(k['seqw']) == 4]
            cur['k_seq_waits'] = sum(s[0] for s in sq)          # WAIT_SEQ calls (async mode)
            cur['k_seq_jobs'] = sum(s[1] for s in sq)
            cur['k_seq_dur_ms'] = round(sum(s[0] * s[2] for s in sq) / 1000, 1)
            cur['k_seq_max_us'] = max((s[3] for s in sq), default=0)
            cur['k_run_max'] = max((k['run_us'][1] for k in kern), default=None)
            out.append(cur)
            kern = []
            misc = {k: 0 for k in misc}
        elif cur is not None and t in ('TEGU_PERF_FRAME', 'TEGU_PERF_BATCH', 'TEGU_PERF_PASS', 'TEGU_PERF_RT', 'TEGU_PERF_BREAK', 'TEGU_PERF_ASYNC'):
            for k, v in kv.items():
                if k == 'ab':
                    continue
                key = t[10:].lower() + '_' + k
                cur[key] = list(v) if isinstance(v, tuple) else v
        elif t == 'TEGU_COMMAND_FAILED':
            misc['fail'] += 1; misc['fail_jobs'] += F(kv.get('jobs', 0))
        elif t == 'TEGU_PASS_REFUSED':
            misc['refused'] += 1
        elif t == 'TEGU_PSO_MISS':
            misc['pso_ms'] += F(kv.get('ms', 0)); misc['pso_n'] += 1
        elif t == 'TEGU_PIPELINE_FALLBACK' and ('blur' in e['text'] or 'downsample' in e['text']):
            misc['fallback_blur'] += 1
    return out


def derive(w):
    c = w.get('commands') or 0
    w['exec_per_cmd'] = round(w.get('exec_ms', 0) / c, 2) if c else None
    w['kj_per_cmd'] = round(w.get('frame_kernel_jobs', 0) / c, 2) if c else None
    w['cmd_per_s'] = round(c / (w['window_ms'] / 1000), 1) if c and w.get('window_ms') else None
    w['enc_per_cmd'] = round(w.get('batch_enc', 0) / c, 2) if c else None
    w['draws_per_cmd'] = round(w.get('batch_draws', 0) / c, 1) if c else None
    fs = w.get('batch_jobs_fs') or [0, 0]
    off = w.get('batch_jobs_off') or [0, 0]
    w['fsjobs_per_cmd'] = round(sum(fs) / c, 2) if c else None
    w['offjobs_per_cmd'] = round(sum(off) / c, 2) if c else None
    tiles = w.get('rt_tiles') or [0, 0, 0]
    w['tilejobs_per_cmd'] = round(tiles[0] / c, 2) if c else None
    w['screens_per_cmd'] = round(tiles[2] / FULL_TILES / c, 2) if c else None   # fragment tiles in full-screen units
    w['present_us'] = w.get('pass_present_us')
    w['ovh_us'] = round(w['k_ret_us'] - w['k_run_us'], 1) if w.get('k_ret_us') is not None else None
    k = w['k_n']
    w['k_ret_per_cmd_ms'] = round(k * w['k_ret_us'] / 1000 / c, 2) if c and k else None
    w['user_per_cmd_ms'] = round(w['exec_per_cmd'] - w['k_ret_per_cmd_ms'], 2) if w['k_ret_per_cmd_ms'] is not None else None
    w['blur'] = bool(w['fallback_blur'] or w['fail'] or 'blur' in w['max_job_fs'] or 'downsample' in w['max_job_fs'])
    return w


COLS = ['line', 't', 'window_ms', 'commands', 'cmd_per_s', 'exec_per_cmd', 'kj_per_cmd', 'k_ret_per_cmd_ms', 'user_per_cmd_ms',
        'enc_per_cmd', 'draws_per_cmd', 'fsjobs_per_cmd', 'offjobs_per_cmd', 'screens_per_cmd', 'present_us', 'k_run_us', 'ovh_us',
        'k_q_us', 'k_clean_us', 'k_inv_us', 'k_gap_us', 'cpu_ms', 'shadow_kb', 'fail', 'refused', 'pso_ms', 'sb_nil', 'sb_flow',
        'blur', 'max_job_fs']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--json')
    ap.add_argument('--csv')
    a = ap.parse_args()
    ws = [derive(w) for w in windows(lp.clock(lp.events(a.log)))]
    print('\t'.join(c[:9] for c in COLS))
    for w in ws:
        print('\t'.join('-' if w.get(c) is None else (f'{w[c]:.1f}' if isinstance(w[c], float) else str(w[c])) for c in COLS))
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(ws, indent=1))
    if a.csv:
        with open(a.csv, 'w', newline='') as fh:
            wr = csv.DictWriter(fh, fieldnames=COLS, extrasaction='ignore')
            wr.writeheader()
            wr.writerows(ws)


if __name__ == '__main__':
    main()
