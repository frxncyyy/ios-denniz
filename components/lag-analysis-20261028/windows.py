#!/usr/bin/env python3
"""Per-window delivery and cost table from an xnu-live.log (text only).

Windows are the kernel's NORD2 FB PRESENT reports (one per >=5 s with at least
one present).  Every other record is attributed to the FB window in whose
console span it was printed.  Output: a table on stdout and JSON (--json).

usage: windows.py LOG [--json OUT] [--perf]   (--perf: also the 2 s TEGU_PERF windows)
"""
import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import lagparse as lp  # noqa: E402

F, S = lp.first, lp.second


def new_acc():
    return {k: 0 for k in (
        'cmds', 'perf_jobs', 'gpu_jobs', 'batched', 'exec_ms', 'gpu_ms', 'cpu_ms', 'cpu_blits', 'shadow_kb', 'max_cmd_ms',
        'perf_win_ms', 'k_jobs_frame', 'enc', 'enc_fs', 'enc_off', 'b_draws', 'b_jobs', 'jobs_fs_load', 'jobs_fs_clear',
        'jobs_off_load', 'jobs_off_clear', 'pass_att1', 'pass_att2', 'pass_att3', 'present_cmds', 'present_us_sum',
        'queue_us_sum', 'pd0', 'pd1', 'pd2_4', 'pd5_16', 'pd17', 'clear_only', 'pass_imported', 'tile_jobs', 'tiles_target',
        'tiles_frag', 'rt0_full', 'rt1_full', 'e_draws', 'e_passes', 'e_draw_ms', 'e_plan_ms', 'e_variant_ms', 'e_pass_ms',
        'e_plan_miss', 'e_win_ms', 'pso_mali_ms', 'pso_air_ms', 'pso_cache_miss', 'pso_miss_n', 'pso_miss_ms', 'pso_miss_max',
        'var_fallback', 'var_standins', 'var_queued', 'var_compiled', 'fail_n', 'fail_jobs', 'refused_n', 'tex_refused',
        'k_n', 'k_win_ms', 'k_q', 'k_run', 'k_tot', 'k_clean', 'k_inv', 'k_ret', 'k_gap', 'k_run_max', 'k_clean_kib',
        'sb_nil', 'sb_transform', 'sb_flow', 'sb_lines', 'sb_fail', 'sb_pso_miss_ms', 'blur_fallback', 'mem_kib')}


def add_record(a, e):
    kv = e.get('kv') or {}
    t, p = e['type'], e['proc']
    g = lambda k, d=0: F(kv.get(k, d))  # noqa: E731
    if p == 'backboardd':
        if t == 'TEGU_PERF':
            a['cmds'] += g('commands'); a['perf_jobs'] += g('jobs'); a['gpu_jobs'] += g('gpu_jobs')
            a['batched'] += g('batched'); a['exec_ms'] += g('exec_ms'); a['gpu_ms'] += g('gpu_ms'); a['cpu_ms'] += g('cpu_ms')
            a['cpu_blits'] += g('cpu_blits'); a['shadow_kb'] += g('shadow_kb'); a['max_cmd_ms'] = max(a['max_cmd_ms'], g('max_command_ms'))
            a['perf_win_ms'] += g('window_ms')
        elif t == 'TEGU_PERF_FRAME':
            a['k_jobs_frame'] += g('kernel_jobs')
        elif t == 'TEGU_PERF_BATCH':
            a['enc'] += g('enc'); a['enc_fs'] += g('enc_fs'); a['enc_off'] += g('enc_off'); a['b_draws'] += g('draws')
            a['b_jobs'] += g('jobs')
            if isinstance(kv.get('jobs_fs'), tuple):
                a['jobs_fs_load'] += kv['jobs_fs'][0]; a['jobs_fs_clear'] += kv['jobs_fs'][1]
            if isinstance(kv.get('jobs_off'), tuple):
                a['jobs_off_load'] += kv['jobs_off'][0]; a['jobs_off_clear'] += kv['jobs_off'][1]
        elif t == 'TEGU_PERF_PASS':
            att = kv.get('att')
            if isinstance(att, tuple) and len(att) == 3:
                a['pass_att1'] += att[0]; a['pass_att2'] += att[1]; a['pass_att3'] += att[2]
            d = kv.get('draws')
            if isinstance(d, tuple) and len(d) == 5:
                for k, v in zip(('pd0', 'pd1', 'pd2_4', 'pd5_16', 'pd17'), d):
                    a[k] += v
            a['clear_only'] += g('clear_only'); a['pass_imported'] += g('imported')
            n = g('present')
            if 'present_us' in kv and 'queue_us' in kv:
                a['present_cmds'] += n; a['present_us_sum'] += n * g('present_us'); a['queue_us_sum'] += n * g('queue_us')
        elif t == 'TEGU_PERF_RT':
            tl = kv.get('tiles')
            if isinstance(tl, tuple) and len(tl) == 3:
                a['tile_jobs'] += tl[0]; a['tiles_target'] += tl[1]; a['tiles_frag'] += tl[2]
            a['rt0_full'] += g('rt0_full'); a['rt1_full'] += g('rt1_full')
        elif t == 'TEGU_PERF_ENCODE':
            a['e_draws'] += g('draws'); a['e_passes'] += g('passes'); a['e_draw_ms'] += g('draw_ms'); a['e_plan_ms'] += g('plan_ms')
            a['e_variant_ms'] += g('variant_ms'); a['e_pass_ms'] += g('pass_ms'); a['e_plan_miss'] += g('plan_miss')
            a['e_win_ms'] += g('window_ms')
        elif t == 'TEGU_PERF_PSO':
            a['pso_mali_ms'] += g('mali_ms'); a['pso_air_ms'] += g('air_ms'); a['pso_cache_miss'] += g('cache_miss')
        elif t == 'TEGU_PSO_MISS':
            a['pso_miss_n'] += 1; a['pso_miss_ms'] += g('ms'); a['pso_miss_max'] = max(a['pso_miss_max'], g('ms'))
        elif t == 'TEGU_PERF_VARIANT':
            a['var_fallback'] += g('fallback_draws'); a['var_standins'] += g('standins'); a['var_queued'] += g('queued')
            a['var_compiled'] += g('compiled')
        elif t == 'TEGU_COMMAND_FAILED':
            a['fail_n'] += 1; a['fail_jobs'] += g('jobs')
        elif t == 'TEGU_PASS_REFUSED':
            a['refused_n'] += 1
        elif t == 'TEGU_TEXTURE_REFUSED':
            a['tex_refused'] += 1
        elif t == 'TEGU_PIPELINE_FALLBACK' and ('blur' in e['text'] or 'downsample' in e['text']):
            a['blur_fallback'] += 1
        elif t == 'TEGU_GPU_MEMORY':
            a['mem_kib'] = max(a['mem_kib'], g('private_kib') + g('internal_kib'))
    elif p == 'SpringBoard':
        tx = e['text']
        if t == 'SpringBoard[':
            a['sb_lines'] += 1
        elif 'nil layer' in tx:
            a['sb_nil'] += 1
        elif 'CATransformLayer' in tx:
            a['sb_transform'] += 1
        elif 'UICollectionViewFlowLayout is not defined' in tx:
            a['sb_flow'] += 1
        elif t == 'TEGU_COMMAND_FAILED':
            a['sb_fail'] += 1
        elif t == 'TEGU_PSO_MISS':
            a['sb_pso_miss_ms'] += g('ms')
    elif t == 'NORD2 GPU JOBS':
        n = g('n')
        if n and 'run_us' in kv and isinstance(kv.get('ret_us'), tuple):
            a['k_n'] += n; a['k_win_ms'] += g('win_ms')
            for k, f in (('k_q', 'q_us'), ('k_run', 'run_us'), ('k_tot', 'tot_us'), ('k_clean', 'clean_us'),
                         ('k_inv', 'inv_us'), ('k_ret', 'ret_us'), ('k_gap', 'gap_us')):
                a[k] += n * F(kv.get(f, 0))
            a['k_run_max'] = max(a['k_run_max'], S(kv['run_us']) or 0)
            a['k_clean_kib'] += g('clean_KiB')


def fb_windows(evs):
    wins, acc, last_t, last_drop = [], new_acc(), None, None
    pending = None
    for e in evs:
        if e['text'].startswith('NORD2 FB PRESENT:'):
            kv = e.get('kv', {})
            if 'n' not in kv:
                continue
            w = {'line': e['line'], 't': e.get('t'), 'n': kv['n'], 'avg_us': F(kv.get('avg_us', 0)),
                 'max_us': F(kv.get('max_us', 0)), 'copy_avg': F(kv.get('copy_us', 0)), 'copy_max': S(kv.get('copy_us')),
                 'xfer_avg': F(kv.get('xfer_us', 0)), 'late_ticks': F(kv.get('late_ticks', 0)),
                 'busy_skips': F(kv.get('busy_skips', 0)), 'busy_forced': F(kv.get('busy_forced', 0))}
            w['dt'] = (w['t'] - last_t) if (w['t'] is not None and last_t is not None) else None
            last_t = w['t']
            w.update(acc)
            wins.append(w)
            pending = w
            acc = new_acc()
            continue
        if e['text'].startswith('NORD2 FB SWAPWAIT') and 'swaps_dropped' in e['text']:
            d = e.get('kv', {}).get('swaps_dropped')
            if d is not None and pending is not None and 'dropped' not in pending:
                pending['dropped_cum'] = d
                pending['dropped'] = d - last_drop if last_drop is not None else d
                last_drop = d
            continue
        add_record(acc, e)
    return wins


def derive(w):
    win_s = max(5.0, w['dt']) if w.get('dt') else 5.0
    w['win_s'] = round(win_s, 2)
    w['fps_bound'] = round(w['n'] / 5.0, 1)            # upper bound: window >= 5 s
    w['fps'] = round(w['n'] / win_s, 1)
    w['delivery'] = round(w['fps'] / 60.0, 3)
    c = w['cmds'] or 0
    w['kj_per_cmd'] = round(w['k_jobs_frame'] / c, 2) if c else None
    w['k_ret_avg'] = round(w['k_ret'] / w['k_n'], 0) if w['k_n'] else None
    w['k_run_avg'] = round(w['k_run'] / w['k_n'], 0) if w['k_n'] else None
    w['ovh_avg'] = round((w['k_ret'] - w['k_run']) / w['k_n'], 0) if w['k_n'] else None
    w['exec_per_cmd'] = round(w['exec_ms'] / c, 2) if c else None
    w['enc_per_cmd'] = round(w['enc'] / c, 2) if c else None
    w['fs_jobs_per_cmd'] = round((w['jobs_fs_load'] + w['jobs_fs_clear']) / c, 2) if c else None
    w['present_us'] = round(w['present_us_sum'] / w['present_cmds']) if w['present_cmds'] else None
    w['present_cmd_per_s'] = round(w['present_cmds'] / win_s, 1)
    w['cmd_per_s'] = round(c / win_s, 1)
    w['gpu_busy'] = round(w['k_run'] / 1e6 / win_s, 3)
    w['gpu_ret_busy'] = round(w['k_ret'] / 1e6 / win_s, 3)
    w['blur'] = bool(w['blur_fallback'] or w['fail_n'] or (w['enc_off'] and c and w['enc_off'] / c >= 1.0))
    return w


COLS = [('line', 6), ('t', 6), ('win_s', 5), ('n', 4), ('fps', 5), ('dropped', 4), ('copy_avg', 5), ('copy_max', 6),
        ('cmds', 5), ('cmd_per_s', 5), ('present_cmd_per_s', 5), ('kj_per_cmd', 6), ('fs_jobs_per_cmd', 5), ('enc_per_cmd', 5),
        ('exec_per_cmd', 6), ('present_us', 6), ('k_run_avg', 5), ('ovh_avg', 5), ('gpu_ret_busy', 5), ('e_draw_ms', 5),
        ('e_variant_ms', 6), ('pso_miss_ms', 6), ('cpu_ms', 5), ('shadow_kb', 6), ('fail_n', 3), ('refused_n', 3),
        ('sb_nil', 3), ('sb_transform', 3), ('sb_flow', 3), ('blur', 2)]


def fmt(v, w):
    if v is None:
        s = '-'
    elif isinstance(v, float):
        s = f'{v:.1f}' if abs(v) < 1000 else f'{v:.0f}'
    elif isinstance(v, bool):
        s = 'B' if v else '.'
    else:
        s = str(v)
    return s.rjust(w)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--json')
    a = ap.parse_args()
    evs = lp.clock(lp.events(a.log))
    wins = [derive(w) for w in fb_windows(evs)]
    print(' '.join(n[:w].rjust(w) for n, w in COLS))
    for w in wins:
        print(' '.join(fmt(w.get(n), wd) for n, wd in COLS))
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(wins, indent=1))


if __name__ == '__main__':
    main()
