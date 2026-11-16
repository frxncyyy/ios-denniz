#!/usr/bin/env python3
"""Host test: analyze_nord2_sched.py on lines in the exact printf formats of nord2_sched.c."""
import importlib.util, re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('an', HERE.parent / 'analyze_nord2_sched.py')
an = importlib.util.module_from_spec(spec); spec.loader.exec_module(an)

src = (HERE.parent / 'src/osfmk/kern/nord2_sched.c').read_text()
# every printf format the analyzer depends on must still be in the kernel source
for frag in ['"NORD2 SCHED AB phase=%u mode=0x%x t_ms=%llu\\n"',
             '"NORD2 SCHED mode 0x%x pri>=%d ab=%u: big busy %llu%% elig %llu%%, little busy %llu%% elig %llu%%\\n"',
             '"NORD2 SCHED watch %s %llu ms/s on big %llu%% %llu runs/s; elig-on-little-while-big-idle %llu/s; "',
             '"placed big-idle %llu/s big-preempt %llu/s spill %llu/s kept-little %llu/s pull %llu/s (ipi %llu/s)\\n"',
             '"NORD2 CPUCLK"', '" %d:%u"', '" MHz big 0x%x"', '"NORD2 SCHED init: big CPUs 0x%x']:
    assert frag in src, frag

log = """NORD2 SCHED init: big CPUs 0xf0 (ADT), mode 0x0, eligible pri >= 47, stats 1, ab 120 s (mode 0xf), clk 300 s, csram 1
NORD2 CPUCLK 0:1999 1:2000 2:2001 3:2000 4:1200 5:1201 6:1199 7:1500 MHz big 0xf0 csram cur 0/7/9 vproc 0/0 sched 0x0/0x0/0x0 tbl0 0x0 0x0
NORD2 SCHED mode 0x0 pri>=47 ab=0: big busy 5% elig 1%, little busy 40% elig 20%
NORD2 SCHED watch ca 300 ms/s on big 10% 60 runs/s; elig-on-little-while-big-idle 50/s; placed big-idle 0/s big-preempt 0/s spill 0/s kept-little 0/s pull 0/s (ipi 0/s)
|backboardd| TEGU_PERF_ENCODE ab=0.0:PF1 window_ms=1000 draws=100 passes=7 draw_ms=30 plan_hit=40 plan_miss=60 bytes=0
|backboardd| TEGU_PERF ab=0.0:PF1 window_ms=1000 commands=10 jobs=13 exec_ms=260 gpu_ms=250 encode_ms=114|backboardd| 417 compiles=17
NORD2 SCHED AB phase=1 mode=0xf t_ms=165000
NORD2 SCHED mode 0xf pri>=47 ab=1: big busy 30% elig 25%, little busy 20% elig 2%
NORD2 SCHED watch ca 150 ms/s on big 97% 60 runs/s; elig-on-little-while-big-idle 1/s; placed big-idle 100/s big-preempt 5/s spill 3/s kept-little 40/s pull 2/s (ipi 2/s)
|backboardd| TEGU_PERF_ENCODE ab=0.0:PF1 window_ms=1000 draws=200 passes=7 draw_ms=20 plan_hit=150 plan_miss=50 bytes=0
|backboardd| TEGU_PERF ab=0.0:PF1 window_ms=2000 commands=40 jobs=13 exec_ms=800 gpu_ms=700 encode_ms=114
"""
r = an.analyze(log)
assert r['init'].startswith('big CPUs 0xf0'), r['init']
assert r['cpuclk_samples'] == 1
assert r['cpuclk'][0]['median'] == 1999 and not r['cpuclk'][0]['below_nominal']
assert r['cpuclk'][4]['below_nominal'] and r['cpuclk'][7]['below_nominal'] and r['cpuclk'][7]['nominal'] == 3000
assert 'csram cur 0/7/9' in r['cpuclk_extra_last']
p0, p1 = r['phases'][0], r['phases'][1]
assert p0['cmd_per_s'] == 10.0 and p0['exec_ms_per_cmd'] == 26.0 and p0['encode_ms_per_draw'] == 0.3 and p0['plan_miss_pct'] == 60.0
assert p0['ca_on_big_pct'] == 10 and p0['big_busy_pct'] == 5 and p0['little_elig_pct'] == 20
assert p1['cmd_per_s'] == 20.0 and p1['exec_ms_per_cmd'] == 20.0 and p1['encode_ms_per_draw'] == 0.1
assert p1['ca_on_big_pct'] == 97 and p1['elig_on_little_while_big_idle_per_s'] == 1
# a log without 0099 lines
r = an.analyze('NORD2 GPU JOBS: n=5\n')
assert r['init'] is None and r['cpuclk_samples'] == 0 and r['phases'] == {}
print('test_analyze_nord2_sched PASS')
