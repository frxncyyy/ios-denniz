#!/usr/bin/env python3
"""Host test for analyze_ab_run.py on a synthetic device-shaped A/B log with
known per-phase numbers (process-prefixed frontend records interleaved, an
untagged kernel stats line, a torn record), plus backward compatibility on the
pre-A/B c6jb device logs and the host A/B schedule test log when present."""
import json
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
GPU = HERE.parent
TOOL = HERE / 'analyze_ab_run.py'
BASE, TF1 = 'PF0NI1AP0TF0JB1BM0', 'PF0NI1AP0TF1JB1BM0'


def window(proc, tag, cmds, kj, batched, gpu_ms, exec_ms, enc_ms, draws, fs, off):
    jobs = kj + batched
    return [
        '|%s| TEGU_PERF ab=%s window_ms=2000 commands=%d jobs=%d gpu_jobs=%d chained=0 clean=0 cpu_blits=0 exec_ms=%d '
        'gpu_ms=%d cpu_ms=1 max_command_ms=9 max_job_us=8000 shadow_kb=0 batched=%d max_job_fs=fixed_frag_lph_cph '
        'encode_ms=%d compile_ms=0 compiles=0' % (proc, tag, cmds, jobs, jobs, exec_ms, gpu_ms, batched, enc_ms),
        '|%s| TEGU_PERF_FRAME ab=%s window_ms=2000 commands=%d mean_exec_us=1 mean_gpu_us=1 kernel_jobs=%d max_exec_us=9'
        % (proc, tag, cmds, kj),
        '|%s| TEGU_PERF_BATCH cmds=%d enc=%d enc_fs=1 enc_off=1 enc_empty=0 enc_skip=0 enc_load=1 enc_clear=1 enc_dc=0 '
        'draws=%d jobs=%d jobs_fs=%d/%d jobs_off=%d/%d' % (proc, cmds, cmds, draws, kj, fs - 2, 2, off - 1, 1),
        '|%s| TEGU_PERF_BREAK other=0 skipped=0 same_enc=0 new_enc=0 heap_scale=100 end=%d' % (proc, cmds),
        '|%s| TEGU_PERF_ENCODE ab=%s window_ms=2000 draws=%d passes=%d draw_ms=%d plan_ms=1 variant_ms=0 expand_ms=0 '
        'pass_ms=0 plan_hit=%d plan_miss=1 bytes=0' % (proc, tag, draws, cmds, draws // 10, draws - 1),
    ]


def kjobs(n, full, run_fs_us, run_other_us):
    run = (full * run_fs_us + (n - full) * run_other_us) // n
    return ('NORD2 GPU JOBS: n=%d fault=0 full=%d win_ms=2000 q_us=40/90 run_us=%d/9000 tot_us=%d/9500 clean_us=5/9 '
            'inv_us=4/8 clean_KiB=64 inv_KiB=64 waits=3 gap_us=10/20 wait_us=300/900 ret_us=320/950 seqw=3/%d/300/900 '
            'gpu_khz=349900 mux=1 bat_cC=3120 bat_ok=1 gpu_cC=-9999 soc_cC=-9999 lvts=0 irq=0:0/0/0/0'
            % (n, full, run, run + 200, n))


def synthetic(path):
    L = ['|backboardd| TEGU_AB schedule phases=2 baseline_s=60 cycle_s=90 repeat=1 defaults=0x12 uses=0x8']
    # boot baseline (phase 0): compiles, then cycles: TF1 (phase 1), baseline (phase 2)
    L += window('backboardd', '0.0:' + BASE, 60, 30, 10, 600, 900, 300, 600, 22, 8)
    L += [kjobs(30, 22, 6000, 2000)]
    for cyc in (0, 1):
        L += window('backboardd', '1.%d:%s' % (cyc, TF1), 60, 10, 50, 200, 400, 300, 600, 4, 6)
        L += ['|SpringBoard| TEGU_PERF ab=1.%d:%s window_ms=2000 commands=6 jobs=6 gpu_jobs=6 chained=0 clean=0 '
              'cpu_blits=0 exec_ms=30 gpu_ms=20 cpu_ms=0 max_command_ms=9 max_job_us=10 shadow_kb=0 batched=0 '
              'max_job_fs=- encode_ms=6 compile_ms=0 compiles=0' % (cyc, TF1)]
        L += [kjobs(16, 4, 6000, 2000)]
        if cyc == 1:
            L += ['|backboardd| TEGU_GPU_MEMORY site=command_failed private_kib=61000 internal_kib=3000 code_kib=900 imported_kib=33000 bos=212',
                  '|backboardd| TEGU_COMMAND_FAILED jobs=4 error=-536870212 first=-:0 site=0/0 job=1 kind=draw',
                  '|backboardd| TEGU_AB_DISABLED toggle=TEGU_TILE_FETCH value=1 error=-536870212 ab=1.1:' + TF1,
                  '|backboardd| TEGU_PSO_MISS mali key=1 entry=11 rt=0 vs=1 fs=1 ms=40 status=0']
        L += window('backboardd', '2.%d:%s' % (cyc, BASE), 60, 30, 10, 600, 900, 300, 600, 22, 8)
        L += [kjobs(30 + cyc, 22, 6000, 2000)]
    L += ['|backboardd| TEGU_PERF window_ms=20']             # torn: dropped
    path.write_text('\n'.join(L) + '\n')


def run(log, *extra):
    with tempfile.TemporaryDirectory() as d:
        out = pathlib.Path(d) / 'r.json'
        r = subprocess.run([sys.executable, str(TOOL), str(log), '--json', str(out), *extra],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        assert r.returncode == 0, r.stdout
        return r.stdout, json.loads(out.read_text())


def main():
    with tempfile.TemporaryDirectory() as d:
        log = pathlib.Path(d) / 'xnu-live.log'
        synthetic(log)
        text, res = run(log)
        print(text)
        V = res['variants']
        assert res['base'] == BASE and set(V) == {BASE, TF1}, V.keys()
        b, t = V[BASE], V[TF1]
        assert b['label'] == 'baseline' and t['label'] == 'TF1'
        assert b['windows'] == 3 and b['commands'] == 180 and t['windows'] == 2 and t['commands'] == 120  # backboardd
        bd, td = b['derived'], t['derived']
        assert abs(bd['cmd_per_s'] - 30.0) < 1e-9 and abs(bd['kjobs_per_cmd'] - 0.5) < 1e-9
        assert abs(bd['fs_per_cmd'] - 22 / 60) < 1e-9 and abs(bd['gpu_ms_per_kjob'] - 20.0) < 1e-9
        assert abs(bd['our_enc_ms_per_draw'] - 0.1) < 1e-9 and abs(bd['ca_ms_per_cmd'] - 4.0) < 1e-9
        assert abs(td['kjobs_per_cmd'] - 10 / 60) < 1e-9 and abs(td['cmd_per_s'] - 30.0) < 1e-9
        _, rall = run(log, '--proc', 'all')                                      # SpringBoard windows pooled too
        ta = rall['variants'][TF1]
        assert ta['windows'] == 4 and ta['commands'] == 132 and abs(ta['derived']['kjobs_per_cmd'] - 32 / 132) < 1e-9
        assert t['failures'] == 1 and t['disabled'] == ['TEGU_TILE_FETCH'] and t['pso_miss_ms'] == 40
        assert t['mem_private_kib'] == 61000
        assert bd['k_jobs'] == 91 and abs(bd['k_run_us'] - (22 * 6000 + 8 * 2000) / 30) < 40   # integer averages
        assert bd['k_gpu_khz_last'] == 349900 and bd['k_bat_cC_max'] == 3120 and 'k_gpu_cC_max' not in bd
        fit = res['refit']
        assert fit and abs(fit['run']['fs_ms'] - 6.0) < 0.05 and abs(fit['run']['other_ms'] - 2.0) < 0.05, fit
        assert 'TF1' in text and 'DISABLED TEGU_TILE_FETCH' in text and 'boot baseline (phase 0)' in text
        text2, res2 = run(log, '--skip-initial', '--windows')
        assert res2['variants'][BASE]['windows'] == 2 and res2['variants'][BASE]['commands'] == 120
        assert 'phase' in text2
    # backward compatibility: pre-A/B device logs (no ab tags, no kernel stats)
    for rel in ('build/hardware-20261025/c6jb-test/ui-usb-boot2/xnu-live.log',
                'build/hardware-20261025/c6jb-test/ui-usb/xnu-live.log'):
        p = GPU / rel
        if p.exists():
            _, r = run(p)
            assert list(r['variants']) == ['untagged'] and r['variants']['untagged']['windows'] > 0, r['variants'].keys()
            assert r['refit'] is None
    host = GPU / 'build/encode-cpu-20261026/ab-test.log'
    if host.exists():
        _, r = run(host)
        assert len(r['variants']) >= 5
    print('PASS analyze_ab_run: synthetic A/B log (per-variant pools, deltas, kernel stats, refit, failures, '
          'skip-initial), pre-A/B c6jb logs, host A/B test log')


if __name__ == '__main__':
    main()
