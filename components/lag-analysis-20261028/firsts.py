#!/usr/bin/env python3
"""One-time (first-use) cost indicators per backboardd TEGU_PERF window.

For each TEGU_PERF window (backboardd), everything printed since the previous
one: PSO misses (count, ms, max), draw-time variants, plan-memo cold misses,
our encode split (TEGU_PERF_ENCODE), kernel GPU MAPs (new GPU mappings, mostly
11 MB display-size IOSurface imports), disk-cache deltas (TEGU_PERF_DISK is
cumulative), CPU shadow/blit work, async sched_us, touch gestures.
usage: firsts.py LOG [--json OUT] [--csv OUT]"""
import argparse
import csv
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import lagparse as lp  # noqa: E402

F, S = lp.first, lp.second
TOUCH = re.compile(r'NORD2 INPUT: finger=\d+ down=1 x=(\d+) y=(\d+)')


def zero():
    return {'pso_n': 0, 'pso_ms': 0, 'pso_max': 0, 'sb_pso_ms': 0, 'var_fallback': 0, 'var_standins': 0, 'var_queued': 0,
            'var_compiled': 0, 'var_prewarmed': 0, 'var_swap_max': 0, 'pm_cold': 0, 'pm_offset': 0, 'pm_extent': 0, 'pm_draw': 0,
            'pm_other': 0, 'e_draws': 0, 'e_draw_ms': 0, 'e_plan_ms': 0, 'e_variant_ms': 0, 'e_pass_ms': 0, 'e_plan_miss': 0,
            'e_plan_hit': 0, 'maps': 0, 'map_mb': 0.0, 'disk_reads': 0, 'disk_absent': 0, 'disk_hits': 0, 'disk_writes': 0,
            'disk_wkb': 0, 'mali_ms': 0, 'cache_miss': 0, 'touches': 0, 'touch_xy': '', 'refused': 0, 'fail': 0, 'tex_refused': 0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--json')
    ap.add_argument('--csv')
    a = ap.parse_args()
    evs = lp.clock(lp.events(a.log))
    out, acc, last_disk = [], zero(), None
    for e in evs:
        t, p, kv, tx = e['type'], e['proc'], e.get('kv') or {}, e['text']
        m = TOUCH.search(tx)
        if m:
            acc['touches'] += 1
            acc['touch_xy'] += f"{m.group(1)},{m.group(2)};"
            continue
        if tx.startswith('NORD2 GPU MAP') and 'bytes' in kv:
            acc['maps'] += 1
            acc['map_mb'] += F(kv['bytes']) / 1048576
            continue
        if p == 'SpringBoard' and t == 'TEGU_PSO_MISS':
            acc['sb_pso_ms'] += F(kv.get('ms', 0))
            continue
        if p != 'backboardd' or not t:
            continue
        g = lambda k: F(kv.get(k, 0))  # noqa: E731
        if t == 'TEGU_PSO_MISS':
            acc['pso_n'] += 1; acc['pso_ms'] += g('ms'); acc['pso_max'] = max(acc['pso_max'], g('ms'))
        elif t == 'TEGU_PERF_VARIANT':
            for k in ('fallback', 'standins', 'queued', 'compiled', 'prewarmed'):
                acc['var_' + k] += g(k + ('_draws' if k == 'fallback' else ''))
            sw = kv.get('swap_ms')
            if isinstance(sw, tuple):
                acc['var_swap_max'] = max(acc['var_swap_max'], sw[1])
        elif t == 'TEGU_PERF_PLANMISS':
            for k in ('cold', 'offset', 'extent', 'draw', 'other'):
                acc['pm_' + k] += g(k)
        elif t == 'TEGU_PERF_ENCODE':
            acc['e_draws'] += g('draws'); acc['e_draw_ms'] += g('draw_ms'); acc['e_plan_ms'] += g('plan_ms')
            acc['e_variant_ms'] += g('variant_ms'); acc['e_pass_ms'] += g('pass_ms'); acc['e_plan_miss'] += g('plan_miss')
            acc['e_plan_hit'] += g('plan_hit')
        elif t == 'TEGU_PERF_DISK':
            cur = {k: g(k) for k in ('reads', 'absent', 'hits', 'writes', 'wbytes')}
            if last_disk:
                acc['disk_reads'] += cur['reads'] - last_disk['reads']; acc['disk_absent'] += cur['absent'] - last_disk['absent']
                acc['disk_hits'] += cur['hits'] - last_disk['hits']; acc['disk_writes'] += cur['writes'] - last_disk['writes']
                acc['disk_wkb'] += (cur['wbytes'] - last_disk['wbytes']) // 1024
            last_disk = cur
        elif t == 'TEGU_PERF_PSO':
            acc['mali_ms'] += g('mali_ms'); acc['cache_miss'] += g('cache_miss')
        elif t == 'TEGU_PASS_REFUSED':
            acc['refused'] += 1
        elif t == 'TEGU_COMMAND_FAILED':
            acc['fail'] += 1
        elif t == 'TEGU_TEXTURE_REFUSED':
            acc['tex_refused'] += 1
        elif t == 'TEGU_PERF':
            w = {'line': e['line'], 't': e.get('t'), 'window_ms': g('window_ms'), 'commands': g('commands'),
                 'exec_ms': g('exec_ms'), 'gpu_ms': g('gpu_ms'), 'cpu_ms': g('cpu_ms'), 'cpu_blits': g('cpu_blits'),
                 'shadow_kb': g('shadow_kb'), 'max_command_ms': g('max_command_ms'), 'jobs': g('jobs'), 'batched': g('batched')}
            w.update(acc)
            w['map_mb'] = round(w['map_mb'], 1)
            out.append(w)
            acc = zero()
        elif out and t == 'TEGU_PERF_ASYNC' and 'sched' not in out[-1]:
            s = kv.get('sched_us')
            out[-1]['sched'] = list(s) if isinstance(s, tuple) else None
            out[-1]['mid_reaps'] = g('mid_reaps'); out[-1]['early'] = g('early')
        elif out and t == 'TEGU_PERF_FRAME' and 'kj' not in out[-1]:
            out[-1]['kj'] = g('kernel_jobs')
    cols = ['line', 't', 'window_ms', 'commands', 'exec_ms', 'kj', 'pso_n', 'pso_ms', 'pso_max', 'e_variant_ms', 'e_plan_ms',
            'e_draw_ms', 'e_draws', 'pm_cold', 'pm_offset', 'var_fallback', 'var_standins', 'var_compiled', 'maps', 'map_mb',
            'disk_absent', 'disk_hits', 'disk_writes', 'cpu_ms', 'shadow_kb', 'cpu_blits', 'mid_reaps', 'sched', 'touches', 'touch_xy']
    print('\t'.join(c[:8] for c in cols))
    for w in out:
        print('\t'.join(str(w.get(c, '')) if not isinstance(w.get(c), float) else f"{w[c]:.1f}" for c in cols))
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(out, indent=1))
    if a.csv:
        with open(a.csv, 'w', newline='') as fh:
            wr = csv.DictWriter(fh, fieldnames=cols, extrasaction='ignore')
            wr.writeheader()
            wr.writerows(out)


if __name__ == '__main__':
    main()
