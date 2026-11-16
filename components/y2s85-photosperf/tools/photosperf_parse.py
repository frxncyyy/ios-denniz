#!/usr/bin/env python3
"""Photos-scroll windows of the fbhw live log: per round / per display mode, the GPU, driver, allocation and present counters."""
import json, re, sys, collections

LOG, STEPS = sys.argv[1], sys.argv[2]
data = open(LOG, 'rb').read().decode('latin-1')
steps = [json.loads(l) for l in open(STEPS)]

# display mode by log offset (FB HW: mode a -> b)
modes = [(0, 0)] + [(m.start(), int(m.group(1))) for m in re.finditer(r'FB HW: mode \d -> (\d)', data)]
def mode_at(off):
    cur = 0
    for o, m in modes:
        if o <= off: cur = m
    return cur

# photos rounds: scroll window = first swipe .. the step after the last grid swipe (tap 680 = next tab)
rounds = collections.OrderedDict()
for s in steps:
    if s.get('scenario') == 'photos':
        rounds.setdefault(s['round'], []).append(s)
wins = []
for r, ss in rounds.items():
    sw = [i for i, s in enumerate(ss) if s['line'].startswith('swipe 540 1900') or s['line'].startswith('swipe 540 700')]
    a, b = ss[sw[0]], ss[sw[-1] + 1]
    # end of window: the log offset of the step AFTER the next one (log lags the step by ~1 step)
    end = ss[min(sw[-1] + 2, len(ss) - 1)]['log_offset']
    wins.append(dict(round=r, o0=a['log_offset'], o1=end, t=(b['start'] - a['start']), mode0=mode_at(a['log_offset']), mode1=mode_at(end)))

def num(pat, s, cast=float):
    return [tuple(cast(x) for x in m.groups()) for m in re.finditer(pat, s)]

out = []
for w in wins:
    s = data[w['o0']:w['o1']]
    R = dict(round=w['round'], mode=w['mode0'] if w['mode0'] == w['mode1'] else '%d->%d' % (w['mode0'], w['mode1']),
             span_s=round(w['t'], 1), bytes=w['o1'] - w['o0'])
    pa = num(r'PRFCNT A win_us=(\d+) took_us=\d+ busy=\d+ cores=\d+ gpu=(\d+) js0=(\d+) js1=(\d+)', s)
    if pa:
        win = sum(x[0] for x in pa)
        R['gpu_busy%'] = round(100 * sum(x[1] for x in pa) / (win * 654.0), 1)   # cycles at 654 MHz over the windows
        R['js0%'] = round(100 * sum(x[2] for x in pa) / (win * 654.0), 1)
        R['js1%'] = round(100 * sum(x[3] for x in pa) / (win * 654.0), 1)
    gj = num(r'GPU JOBS: n=(\d+) fault=\d+ full=(\d+) win_ms=(\d+) q_us=(\d+)/(\d+) run_us=(\d+)/(\d+)', s)
    if gj:
        ms = sum(x[2] for x in gj); n = sum(x[0] for x in gj)
        R['jobs/s'] = round(1000 * n / ms)
        R['job_q_us'] = round(sum(x[0] * x[3] for x in gj) / max(n, 1))
        R['job_run_us'] = round(sum(x[0] * x[5] for x in gj) / max(n, 1))
        R['job_q_max_us'] = int(max(x[4] for x in gj))
        R['ring_full/s'] = round(1000 * sum(x[1] for x in gj) / ms, 1)
        R['gpu_run_ms/s(jobs)'] = round(sum(x[0] * x[5] for x in gj) / ms, 1)
    fp = num(r'FB PRESENT: n=(\d+) avg_us=(\d+) max_us=(\d+) copy_us=(\d+)/(\d+)', s)
    fs = num(r'FB SEEN: presents=(\d+) different=(\d+) same=(\d+) gaps_ms<=20/34/67/100/more=(\d+)/(\d+)/(\d+)/(\d+)/(\d+) max_gap_ms=(\d+)', s)
    if fs:
        R['seen_presents'] = int(sum(x[0] for x in fs)); R['seen_different'] = int(sum(x[1] for x in fs))
        R['gaps>34ms'] = int(sum(x[5] + x[6] + x[7] for x in fs)); R['max_gap_ms'] = int(max(x[8] for x in fs))
    sy = num(r'FB SYNC: pushes=(\d+) .*?src_race=(\d+) busy_after=(\d+)', s)
    if len(sy) >= 1:
        R['src_race(delta)'] = int(sy[-1][1] - sy[0][1]) if len(sy) > 1 else 0
        R['pushes(delta)'] = int(sy[-1][0] - sy[0][0]) if len(sy) > 1 else 0
    hw = num(r'FB HW: mode=(\d) .*?hw_frames=(\d+) cpu_frames=(\d+)', s)
    if len(hw) > 1:
        R['hw_frames(delta)'] = int(hw[-1][1] - hw[0][1]); R['cpu_frames(delta)'] = int(hw[-1][2] - hw[0][2])
    sw_ = num(r'SWAPWAIT: level=\d+ calls=(\d+) not_ready=(\d+) blocked=\d+ timeouts=\d+ swaps_dropped=(\d+)', s)
    if len(sw_) > 1:
        R['swaps_dropped(delta)'] = int(sw_[-1][2] - sw_[0][2])
    cpu = num(r'SYS: win_ms=(\d+) cpu_busy%=([\d/]+) \(avg (\d+), big (\d+)\)', s, str)
    if cpu:
        R['cpu_avg%'] = round(sum(int(x[2]) for x in cpu) / len(cpu)); R['cpu_big%'] = round(sum(int(x[3]) for x in cpu) / len(cpu))
        R['cpu_max_core%'] = max(max(int(v) for v in x[1].split('/')) for x in cpu)
    al = num(r'ALLOC SLOW: bytes=(\d+) .*?total=(\d+) us', s)
    R['alloc_slow_n'] = len(al); R['alloc_slow_ms'] = round(sum(x[1] for x in al) / 1000, 1)
    R['alloc_slow_MB'] = round(sum(x[0] for x in al) / 2**20, 1)
    sl = collections.Counter(); slms = collections.Counter()
    for m in re.finditer(r'I SLOW TeguComputeDevice (\S+?): took ([\d.]+) ms', s):
        sl[m.group(1)] += 1; slms[m.group(1)] += float(m.group(2))
    R['slow_calls'] = {k: '%dx %.0fms' % (sl[k], slms[k]) for k in sl}
    for proc in ('backboardd', 'MobileSlideShow'):
        d = [float(x) for x in re.findall(r'W PERF <%s> slow draw: ([\d.]+) ms' % proc, s)]
        c = [float(x) for x in re.findall(r'W PERF <%s> slow: ([\d.]+) ms commit' % proc, s)]
        R['slowdraw_%s' % proc] = '%d / %.0fms' % (len(d), sum(d))
        R['slowcommit_%s' % proc] = '%d / %.0fms' % (len(c), sum(c))
    ph = [int(x) for x in re.findall(r'PHOTOS frame n=(\d+) draws', s)]
    R['photos_probe_frames'] = len(ph)
    for k, pat in (('ycbcr_odd', r'SURF ycbcr-odd'), ('young', r'young reconvert'), ('freeze', r'SURF freeze n='),
                   ('cpu_view', r'SURF cpu-view n='), ('np_import', r'np import: IOSurface')):
        R[k] = len(re.findall(pat, s))
    rep = [int(x) for x in re.findall(r'SURF ycbcr-odd[^\n]*?\[repeat (\d+)\]', s)]
    if rep: R['ycbcr_odd_repeat_max'] = max(rep)
    pf = re.findall(r'perf: 5 s: (\d+) command buffers \(([\d.]+) a second\), (\d+) draws; commit ([\d.]+) ms each \(longest ([\d.]+)\), GPU wait ([\d.]+) ms.*?; (\d+) surface copies, (\d+) KB in ([\d.]+) ms; (\d+) slow commits', s)
    R['perf5s'] = ['cb=%s(%s/s) draws=%s commit=%sms(max %s) copies=%s/%sKB/%sms slow=%s' % (a, b, c, d, e, g, h, i, j) for a, b, c, d, e, f, g, h, i, j in pf]
    al2 = re.findall(r'allocations: (\d+) buffers made \((\d+) KB, ([\d.]+) ms in the library\), (\d+) freed; (\d+) textures made \(([\d.]+) ms\), (\d+) freed \(lived (\d+) ms', s)
    R['allocs5s'] = ['buf %s(%sKB,%sms) tex %s(%sms) freed %s life %sms' % (a, b, c, e, f, g, h) for a, b, c, d, e, f, g, h in al2]
    R['tex_shapes'] = re.findall(r'textures made: ([^\n]{0,160})', s)[:3]
    R['per_draw'] = re.findall(r'per draw: ([\d.]+ us \(Gallium [\d.]+ us, \d+%\), \d+ draws)', s)
    R['labels'] = [x[:110] for x in re.findall(r'I PERF perf:    ((?:\(no label\)|\(compute\)|com\.apple)[^\n]{0,200})', s)][:4]
    nt = num(r'np time\(n/avg_us/max_us\) create=(\d+)/(\d+)/(\d+) map=\d+/\d+/\d+ submit=(\d+)/(\d+)/(\d+) wait=\d+/\d+/\d+ close=(\d+)/(\d+)/\d+ sync=(\d+)/(\d+)/(\d+) pool hit=(\d+) miss=(\d+)', s)
    if nt:
        R['np_create'] = '%d x %dus' % (sum(x[0] for x in nt), sum(x[0] * x[1] for x in nt) / max(1, sum(x[0] for x in nt)))
        R['np_submit'] = '%d x %dus' % (sum(x[3] for x in nt), sum(x[3] * x[4] for x in nt) / max(1, sum(x[3] for x in nt)))
        R['np_pool_hit/miss'] = '%d/%d' % (sum(x[11] for x in nt), sum(x[12] for x in nt))
    mg = num(r'MEM GPU: bos_live=(\d+) .*?private=(\d+) MiB', s)
    if mg: R['gpu_private_MiB'] = '%d..%d' % (min(x[1] for x in mg), max(x[1] for x in mg)); R['bos_live'] = '%d..%d' % (min(x[0] for x in mg), max(x[0] for x in mg))
    mp = re.findall(r'MEMP SpringBoard: memory pressure[^\n]{0,60}', s)
    R['mem_pressure_lines'] = len(mp)
    out.append(R)

for R in out:
    print(json.dumps(R, indent=None))
    print()
