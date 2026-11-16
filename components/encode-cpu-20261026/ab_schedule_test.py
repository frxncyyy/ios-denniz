#!/usr/bin/env python3
"""Host test: in-boot A/B feature toggles (metal/tegu/ab_schedule.inc).

Builds the real working-tree frontend + bridge through async_frontend_test.py
(same fake kernel and deferred-execution wrapper) with ab_schedule_main.m as
main and a schedule file in $TEGU_AB_SCHEDULE; the test drives the boot clock.
Checks every toggle's effect, the command-boundary snapshot, the failure
disable, and that each TEGU_PERF window carries exactly one toggle word.
Also unit-checks the schedule parser/packager inputs. No GPU, phone or publish.
"""
import os, pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
schedule = HERE / 'ab_test_schedule.conf'
schedule.write_text('''# test schedule
baseline 10
phase 10 TEGU_NATIVE_INDEXED=0
phase 10 TEGU_JM_BATCH=1
phase 10 NORD2_METAL_PREFRAME=1
phase 10 TEGU_ASYNC_PIPELINE=1
phase 10 NORD2_METAL_BATCH_MAX=128
phase 10 TEGU_NATIVE_INDEXED=0 TEGU_JM_BATCH=1
repeat 1
''')
env = dict(os.environ, FA_MAIN=str(HERE / 'ab_schedule_main.m'), FA_NAME='ab_schedule_test',
           FA_PASS='PASS ab schedule', TEGU_AB_SCHEDULE=str(schedule))
r = subprocess.run([sys.executable, str(HERE / 'async_frontend_test.py')], env=env,
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
out = r.stdout.decode('utf-8', 'replace')
print('\n'.join(l for l in out.splitlines() if l.startswith(('AB_', 'TEGU_AB', 'TEGU_PERF ', 'PASS', 'FAIL')) or 'Assert' in l))
ok = r.returncode == 0
# Per-window tagging: both builds (ASan, release) print the same tags.
need = ['TEGU_AB schedule phases=6 baseline_s=10 cycle_s=60 repeat=1',
        'TEGU_PERF ab=0.0:PF0NI1AP0TF0JB0BM1EX1 ', 'TEGU_PERF ab=1.0:PF0NI0AP0TF0JB0BM1EX1 ', 'TEGU_PERF ab=2.0:PF0NI1AP0TF0JB1BM1EX1 ',
        'TEGU_PERF ab=3.0:PF1NI1AP0TF0JB0BM1EX1 ', 'TEGU_PERF ab=4.0:PF0NI1AP1TF0JB0BM1EX1 ', 'TEGU_PERF ab=5.0:PF0NI1AP0TF0JB0BM1EX1 ', 'TEGU_PERF ab=1.1:PF0NI0AP0TF0JB0BM1EX1 ',
        'TEGU_AB_DISABLED toggle=TEGU_NATIVE_INDEXED value=0', 'TEGU_AB_DISABLED toggle=TEGU_JM_BATCH value=1']
for n in need:
    if out.count(n) < 2:
        print('MISSING (x2 builds):', n)
        ok = False
# A window never mixes words: every TEGU_PERF line has exactly one tag.
for l in out.splitlines():
    if l.startswith('TEGU_PERF ') and len(re.findall(r'ab=', l)) != 1:
        print('BAD WINDOW', l)
        ok = False
print('PASS' if ok else 'FAIL', 'ab_schedule_test')
sys.exit(0 if ok else 1)
