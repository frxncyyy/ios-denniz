#!/usr/bin/env python3
"""Frame-cost benchmark for the Nord2 Metal -> Mali JM UI path (host only).

    python3 gpu/tools/frame_bench.py            # build if inputs changed, replay, table, history
    python3 gpu/tools/frame_bench.py --help

What it does
  1. Builds (cached on a hash of every production input) the real Objective-C
     frontend + n2mb bridge + Rust translator against the shared fake JM
     kernel - the same prelude as gpu/build/c6f-lowering-20261026/
     ca_backboardd_host_test.py - plus gpu/tools/frame_bench_main.m.
  2. Replays CA-shaped command buffers on the imported 1125x2436 display
     (tick, small, medium, large, the C6f 200-draw buffer, blur heavy), in
     the c6jb device configuration (JM batching on) and per-draw (c6ic).
  3. Per frame it records: draws, passes, GPU jobs encoded/submitted by class,
     kernel calls by selector, bytes allocated/mapped/copied, host encode
     wall/CPU/instructions and the production encode timers.
  4. Feeds the structural counts into gpu/tools/frame_model.py (fit on the
     device TEGU_PERF logs) and prints predicted device ms/frame and fps, the
     per-term breakdown against the 16.7 ms and 33.3 ms budgets, and appends
     one JSON line to gpu/build/frame-bench-20261026/history.jsonl.

Host numbers are NOT device numbers.  Host-only, nothing here touches the
phone.  The compile is one clang process at nice 10 (jobs 1) and a lock file
keeps two agents from running it at once.
"""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
import pathlib
import re
import statistics
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
G = HERE.parent
ROOT = G.parent
sys.path.insert(0, str(HERE))
import frame_model as fm  # noqa: E402

OUT = G / 'build/frame-bench-20261026'
BIN = OUT                    # build/run directory (OUT, or OUT/anchor-<commit> for an exported commit)
P = G                        # production gpu/ tree that is built (G, or an exported commit's gpu/)
PROOT = ROOT                 # its repository root (git queries); None for an export
SOURCE_TAG = None
HARNESS = G / 'build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py'
TEGU = G / 'metal/tegu'
CORPUS = G / 'build/ui-corpus-20261025'
LIBS = [G / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release',
        G / 'build/mesa-build/src/panfrost/clc']
BUDGET_60, BUDGET_30 = 1000 / 60.0, 1000 / 30.0
MODES = ('batched', 'perdraw')


def sh(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kw)


def configure(anchor_commit=None):
    """Point the build at the working tree (default) or at an exported commit (the device-run anchor).
    Support files (harness, Rust libs, corpus, metallib, generated headers) always come from the main tree."""
    global BIN, P, PROOT, TEGU, SOURCE_TAG
    if not anchor_commit:
        return
    tag = 'anchor-' + anchor_commit
    BIN = OUT / tag
    src = BIN / 'src'
    if not (src / 'gpu/metal').exists():
        src.mkdir(parents=True, exist_ok=True)
        r = subprocess.run('git -C %s archive %s gpu | tar -x -C %s' % (ROOT, anchor_commit, src), shell=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if r.returncode:
            raise SystemExit('frame_bench: cannot export %s: %s' % (anchor_commit, r.stderr))
        full = sh(['git', '-C', str(ROOT), 'rev-parse', anchor_commit]).stdout.strip()
        (src / 'SOURCE_COMMIT').write_text(full + '\n')
    P, PROOT, SOURCE_TAG = src / 'gpu', None, anchor_commit
    TEGU = P / 'metal/tegu'


# ------------------------------------------------------------------ provenance
def repo_state():
    if PROOT is None:
        full = (P.parent / 'SOURCE_COMMIT').read_text().strip()
        return {'head': full[:12], 'status': [], 'diff_sha256': 'export', 'dirty_files': 0, 'export_of': SOURCE_TAG}
    head = sh(['git', '-C', str(ROOT), 'rev-parse', '--short=12', 'HEAD']).stdout.strip()
    status = sh(['git', '-C', str(ROOT), 'status', '--short', '--', 'gpu']).stdout.splitlines()
    diff = sh(['git', '-C', str(ROOT), 'diff', 'HEAD', '--', 'gpu']).stdout
    return {'head': head, 'status': status, 'diff_sha256': hashlib.sha256(diff.encode()).hexdigest()[:16],
            'dirty_files': len(status)}


def _sha(paths):
    h = hashlib.sha256()
    for p in sorted(paths, key=str):
        h.update(str(p.relative_to(G) if str(p).startswith(str(G)) else p).encode())
        h.update(p.read_bytes())
    return h.hexdigest()


def input_files():
    files = []
    for ext in ('*.c', '*.h', '*.inc', '*.m'):
        files += [p for p in (P / 'metal').rglob(ext) if p.is_file()]
    files += [P / 'src/nord2_gpu_desc.c', P / 'src/nord2_gpu_jm.c', P / 'tests/nord2_metal_bridge_test.c', HARNESS,
              HERE / 'frame_bench_main.m', HERE / 'frame_bench.py']
    files += list((P / 'include').glob('*.h')) + list((G / 'build/tegu-frontend').glob('*.h'))
    return [p for p in files if p.exists()]


def lib_stamp():
    out = {}
    for d in LIBS:
        for name in ('libtegu_air_bridge.so', 'libnord2_mali_runtime.so'):
            p = d / name
            if p.exists():
                st = p.stat()
                out[name] = '%d:%d' % (st.st_size, st.st_mtime_ns)
    return out


def corpus_args():
    jobs = {x['id']: x for x in json.load(open(CORPUS / 'graphics-jobs.json'))['jobs']}
    air = lambda job, stage: str(CORPUS / jobs[job][stage]['air_saved_as'])
    qc = str(G / 'build/native/QuartzCore.metallib')
    return [air('graphics-00139', 'vertex'), air('graphics-00139', 'fragment'), air('graphics-01847', 'fragment'),
            air('graphics-00089', 'vertex'), air('graphics-00160', 'fragment'),
            air('graphics-00168', 'vertex'), air('graphics-00168', 'fragment'), qc, qc, air('graphics-00165', 'fragment')]


# ------------------------------------------------------------------ source generation
def gen_source(use_head_compute):
    """Same prelude/patching as ca_backboardd_host_test.py (read-only), plus counters."""
    import runpy  # noqa: F401  (documented: prelude is exec'd, not imported)
    text = HARNESS.read_text()
    prelude = text.split("path = WORK / 'vs_texture_host_test.m'", 1)[0]
    ns = {'__file__': str(P / 'build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py'), '__name__': 'frame_bench_prelude'}  # G/_root derive from this path
    real = subprocess.check_output

    def tree_show(cmd, *a, **k):
        if not use_head_compute and isinstance(cmd, list) and cmd[:2] == ['git', '-C'] and 'show' in cmd \
                and cmd[-1].startswith('HEAD:'):
            return ((PROOT or P.parent) / cmd[-1][5:]).read_bytes()      # working tree (or export), not HEAD
        return real(cmd, *a, **k)
    subprocess.check_output = tree_show
    # The harness reads the working-tree compute frontend unless told to take the
    # git copy; route through tree_show (export / HEAD) for anchors and the HEAD fallback.
    old_env = os.environ.get('HARNESS_COMPUTE_FROM_HEAD')
    if use_head_compute or PROOT is None:
        os.environ['HARNESS_COMPUTE_FROM_HEAD'] = '1'
    try:
        exec(compile(prelude, str(HARNESS), 'exec'), ns)
    finally:
        subprocess.check_output = real
        if old_env is None:
            os.environ.pop('HARNESS_COMPUTE_FROM_HEAD', None)
        else:
            os.environ['HARNESS_COMPUTE_FROM_HEAD'] = old_env
    source = ns['source']
    prefix = source.split('int main(int argc,char **argv){', 1)[0]
    tail_src = (TEGU / 'runtime_frontend_host_test.m').read_text().split(
        'NSUInteger tegu_host_constant_data_size(NSUInteger type)', 1)[1]
    tail = 'NSUInteger tegu_host_constant_data_size(NSUInteger type)' + tail_src
    fixed_ui = (TEGU / 'fixed_ui_frontend_host_test.inc').read_text().split('static void test_fixed_ui_native_frontend', 1)[0]

    def once(s, old, new):
        assert s.count(old) == 1, 'frame_bench prelude patch target not unique/found: %r (%d)' % (old[:60], s.count(old))
        return s.replace(old, new, 1)

    # shared fake kernel: many more buffer handles, import selector, counters
    assert len(re.findall(r'#define COUNT \d+', prefix)) == 1
    prefix = re.sub(r'#define COUNT \d+', '#define COUNT 400000\n'
                    'static unsigned long long fb_calls[16],fb_alloc_bytes,fb_map_calls,fb_map_bytes,fb_live_bytes,fb_live_peak;', prefix, count=1)
    prefix = once(prefix, 'assert(b->active);free(b->data);b->data=NULL;',
                  'assert(b->active);fb_live_bytes-=b->size;free(b->data);b->data=NULL;')
    prefix = once(prefix, 'b->active=1;b->exec=(unsigned)in[1];f->live++;',
                  'b->active=1;b->exec=(unsigned)in[1];f->live++;fb_alloc_bytes+=b->size;fb_live_bytes+=b->size;'
                  'if(fb_live_bytes>fb_live_peak)fb_live_peak=fb_live_bytes;')
    prefix = once(prefix, 'b->mapped=1;f->maps++;*address=(uintptr_t)b->data;*size=b->size;return 0;',
                  'b->mapped=1;f->maps++;fb_map_calls++;fb_map_bytes+=b->size;*address=(uintptr_t)b->data;*size=b->size;return 0;')
    i = prefix.index('static int call(void *p,unsigned s,')
    j = prefix.index('switch(s) {', i)
    prefix = prefix[:j] + 'if(s<16)fb_calls[s]++;\n  ' + prefix[j:]
    imp = (' case NORD2_GPU_SEL_IMPORT_SURFACE: {\n'
           '  assert(ni==4&&*no==3&&in[0]&&in[2]);\n'
           '  unsigned h=++f->next;assert(h<COUNT);struct buffer *b=&f->buffers[h];\n'
           '  b->size=(in[2]+16383)&~16383ull;b->va=0x1000000000ull+h*0x2000000ull;\n'
           '  b->data=calloc(1,b->size);assert(b->data);b->active=1;b->exec=0;f->live++;fb_live_bytes+=b->size;\n'
           '  out[0]=h;out[1]=b->va;out[2]=b->size;*no=3;return 0;\n }\n case NORD2_GPU_SEL_SYNC:')
    prefix = once(prefix, ' case NORD2_GPU_SEL_SYNC:', imp)
    prefix = once(prefix, 'static void *test_si_base(void *p){return ((struct TestIOSurface*)p)->data;}',
                  'static unsigned char *c6f_big_surface;\nstatic void *test_si_base(void *p){return c6f_big_surface&&'
                  '((struct TestIOSurface*)p)->width>64?c6f_big_surface:((struct TestIOSurface*)p)->data;}')
    main = (HERE / 'frame_bench_main.m').read_text()
    shim = ''
    if 'TEGU_ENC_SLOTS' not in (TEGU / 'render_encoder.inc').read_text():        # older source (e.g. the c6jb anchor) has no production encode timers
        shim = ('#include <time.h>\nstatic double tegu_enc_clock(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);'
                'return (double)t.tv_sec+t.tv_nsec*1e-9;}\nenum {TEGU_ENC_DRAW,TEGU_ENC_PLAN,TEGU_ENC_VARIANT,TEGU_ENC_EXPAND,TEGU_ENC_PASS,'
                'TEGU_ENC_SLOTS};\nstatic U64 tegu_enc_ns[TEGU_ENC_SLOTS]; /* shim: draw/plan/variant timers absent in this source (reported 0) */\n')
    return prefix + shim + fixed_ui + main + tail


def build(use_head_compute, log):
    BIN.mkdir(parents=True, exist_ok=True)
    src = gen_source(use_head_compute)
    (BIN / 'frame_bench.m').write_text(src)
    # FB_BRIDGE_SRC: build against a snapshot of the bridge (another agent's in-flight edit may not link).
    (BIN / 'hostbase_real_nord2_metal_bridge.c').write_bytes(
        pathlib.Path(os.environ.get('FB_BRIDGE_SRC', str(P / 'metal/nord2_metal_bridge.c'))).read_bytes())
    (BIN / 'nord2_metal_bridge.h').write_bytes((P / 'metal/nord2_metal_bridge.h').read_bytes())
    cmd = ['nice', '-n', '10', 'clang', '-fblocks', '-pthread', '-O2', '-g1',
           '-Wno-incompatible-pointer-types', '-Wno-incompatible-function-pointer-types', '-w',
           '-fconstant-string-class=NXConstantString',
           '-DTEGU_HOST_REAL_V2', '-DTEGU_INERT_PIPELINES', '-DC6F_ATTACHMENTS=2',
           '-DTEGU_JM_BATCH_DEFAULT']
    # Async submission model only where the built tree has it (anchor a5c19df does not).
    if 'n2mb_cmdpool_stats' in (P / 'metal/nord2_metal_bridge.h').read_text() and \
            'seq_model' in (P / 'tests/nord2_metal_bridge_test.c').read_text():
        cmd += ['-DFB_HAVE_ASYNC=1']
    # Device time model in the fake kernel (FB_SUBMIT_US / FB_GPU_US), where the tree's fake has it.
    if 'submit_us' in (P / 'tests/nord2_metal_bridge_test.c').read_text():
        cmd += ['-DFB_HAVE_TIME=1']
    # TEGU_JM_BATCH_DEFAULT: the c6jb batch variant's frontend define (variant.json); the replay also forces
    # the mode per frame through tegu_jm_batch_force, so this only fixes the env default.
    cmd += ['-I' + str(p) for p in (BIN, TEGU, P / 'metal', P / 'include', G / 'build/tegu-frontend')]
    cmd += [str(BIN / 'frame_bench.m'), str(BIN / 'hostbase_real_nord2_metal_bridge.c'),
            str(P / 'metal/nord2_metal_render.c'), str(P / 'src/nord2_gpu_desc.c'), str(P / 'src/nord2_gpu_jm.c')]
    cmd += ['-lobjc', '-ldl', '-lBlocksRuntime', '-Wl,--wrap=n2mb_copy'] + ['-L' + str(p) for p in LIBS]
    cmd += ['-ltegu_air_bridge', '-lnord2_mali_runtime', '-o', str(BIN / 'frame_bench_bin')]
    t = time.time()
    r = sh(cmd)
    log.write('$ %s\nrc=%d  %.0fs\n%s\n' % (' '.join(cmd), r.returncode, time.time() - t, r.stderr[-6000:]))
    return r.returncode == 0, r.stderr


def ensure_built(force, log):
    key_inputs = _sha(input_files())
    st = repo_state()
    stamp = {'inputs': key_inputs, 'libs': lib_stamp()}
    stamp_path = BIN / 'build-stamp.json'
    binary = BIN / 'frame_bench_bin'
    if not force and binary.exists() and stamp_path.exists():
        old = json.loads(stamp_path.read_text())
        if old.get('inputs') == stamp['inputs'] and old.get('libs') == stamp['libs']:
            return dict(old, cached=True)
    compute_dirty = PROOT is not None and bool(sh(['git', '-C', str(ROOT), 'diff', '--name-only', 'HEAD', '--',
                             'gpu/metal/tegu/compute_frontend.m', 'gpu/metal/tegu/runtime_pipeline.inc']).stdout.strip())
    attempts = [False] + ([True] if compute_dirty else [])
    why = ''
    for use_head in attempts:
        ok, err = build(use_head, log)
        if ok:
            stamp.update(compute_source='HEAD (working tree differs; tree build failed)' if use_head else 'working tree',
                         head=st['head'], built=datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'))
            stamp_path.write_text(json.dumps(stamp, indent=1))
            return dict(stamp, cached=False)
        why = err
    raise SystemExit('frame_bench: build failed (see %s/build.log). Last errors:\n%s' % (OUT, '\n'.join(why.splitlines()[-25:])))


# ------------------------------------------------------------------ running
def run_replay(steady, only):
    # Host builds verify every encode-memo hit (TEGU_PLAN_MEMO_VERIFY) for the gates; time without it.
    env = dict(os.environ, LD_LIBRARY_PATH=':'.join(map(str, LIBS)), ASAN_OPTIONS='detect_leaks=0',
               TEGU_PLAN_MEMO_VERIFY='0', FB_ASYNC=os.environ.get('FB_ASYNC', '1'))
    cmd = ['nice', '-n', '10', str(BIN / 'frame_bench_bin')] + corpus_args() + [str(steady)] + ([only] if only else [])
    r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    (OUT / 'last-stdout.log').write_text(r.stdout)  # raw FB_* lines (FB_VF, FB_POOL) for inspection
    frames, meta = [], {}
    for line in r.stdout.splitlines():
        if line.startswith('FB_FRAME '):
            frames.append({k: _num(v) for k, v in (t.split('=', 1) for t in line[9:].split() if '=' in t)})
        elif line.startswith(('FB_PIPES', 'FB_ENV', 'FB_ASYNC')):
            meta.update({k: _num(v) for k, v in (t.split('=', 1) for t in line.split()[1:] if '=' in t)})
    if r.returncode or not any(l == 'FB_DONE' for l in r.stdout.splitlines()):
        raise SystemExit('frame_bench: replay failed rc=%s\n%s\n%s' % (r.returncode, r.stdout[-1500:], r.stderr[-3000:]))
    return frames, meta, r.stderr


def _num(v):
    try:
        return int(v)
    except ValueError:
        try:
            return float(v)
        except ValueError:
            return v


def med(frames, key):
    v = [f[key] for f in frames if key in f]
    return statistics.median(v) if v else 0.0


METRICS = ['draws', 'passes', 'jobs_enc', 'kjobs', 'generic', 'other', 'fs_load', 'fs_clear', 'off_load', 'off_clear', 'enc',
           'enc_fs', 'enc_off', 'enc_us', 'enc_cpu_us', 'enc_ins', 'exec_us', 'proc_cpu_us', 'draw_us', 'plan_us', 'variant_us',
           'expand_us', 'pass_us', 'k_alloc', 'k_release', 'k_sync', 'k_submit', 'k_wait', 'k_import', 'k_log', 'k_other',
           'alloc_kib', 'map_calls', 'map_kib', 'copy_calls', 'copy_kib', 'surface_locks', 'peak_kib', 'invalid', 'skipped']


def aggregate(frames):
    out = {}
    for f in frames:
        out.setdefault((f['scn'], f['mode']), []).append(f)
    res = {}
    for (scn, mode), fs in out.items():
        steady = [f for f in fs if f['phase'] == 'steady']
        warm = [f for f in fs if f['phase'] == 'warm']
        cold = [f for f in fs if f['phase'] == 'cold']
        base = steady or warm or cold
        d = {m: med(base, m) for m in METRICS}
        d['frames'] = len(steady)
        d['first_enc_us'] = (warm or cold)[0]['enc_us'] if (warm or cold) else 0
        d['first_variants_new'] = (warm or cold)[0].get('variants_new', 0) if (warm or cold) else 0
        d['enc_us_min'] = min((f['enc_us'] for f in base), default=0)
        d['enc_us_max'] = max((f['enc_us'] for f in base), default=0)
        d['kernel_calls'] = d['k_alloc'] + d['k_release'] + d['k_sync'] + d['k_submit'] + d['k_wait'] + d['map_calls']
        res[(scn, mode)] = d
    return res


# ------------------------------------------------------------------ prediction
def load_baseline():
    p = OUT / 'baseline.json'
    return json.loads(p.read_text()) if p.exists() else None


def predict(model, agg, baseline):
    """Predicted device ms per frame for each scenario in the c6jb configuration.

    GPU time comes from the replayed job structure through the device fit.  Encode time is the device fit
    (6.45 ms/draw, measured on the anchor code) times the MEASURED host change versus the frozen anchor
    (the commit that ran on the device): enc_scale = host user instructions per frame now / at the anchor
    (range: the min/max of instructions, host CPU time and kernel calls).  Without an anchor it is 1.0."""
    rows = {}
    e_draw = model.get('encode_fit_draw_only', {}).get('coef_ms', {}).get('e_draw', 0.0)
    for (scn, mode), a in sorted(agg.items()):
        if a['invalid']:
            continue          # an invalid command buffer has no job structure; never predicted
        shape = {'fs_load': a['fs_load'], 'fs_clear': a['fs_clear'], 'off_load': a['off_load'], 'off_clear': a['off_clear'],
                 'other_jobs': a['other'], 'kernel_jobs': a['kjobs']}
        gpu = fm.predict_gpu_ms(model, shape)
        enc0 = e_draw * a['draws']
        b = (baseline or {}).get('agg', {}).get('%s/%s' % (scn, mode))
        r_ins = a['enc_ins'] / b['enc_ins'] if b and b.get('enc_ins') and a['enc_ins'] else None
        r_cpu = a['enc_cpu_us'] / b['enc_cpu_us'] if b and b.get('enc_cpu_us') else None
        r_call = a['kernel_calls'] / b['kernel_calls'] if b and b.get('kernel_calls') else None
        have = [r for r in (r_ins, r_cpu, r_call) if r is not None]
        scale = (r_ins if r_ins is not None else r_cpu) if have else 1.0
        lo, hi = (min(have), max(have)) if have else (1.0, 1.0)
        enc = enc0 * scale
        tot_serial = enc + gpu
        rows[(scn, mode)] = {
            'gpu_ms': gpu, 'enc_ms': enc, 'enc_ms_lo': enc0 * lo, 'enc_ms_hi': enc0 * hi, 'enc_ms_structural': enc0,
            'enc_scale': scale, 'enc_scale_range': (lo, hi), 'anchored': bool(have),
            'frame_ms_serial': tot_serial, 'frame_ms_pipelined': max(enc, gpu),
            'fps_serial': 1000.0 / tot_serial if tot_serial else 0, 'fps_pipelined': 1000.0 / max(enc, gpu, 1e-9),
            'fps_serial_range': (1000.0 / (enc0 * hi + gpu) if enc0 * hi + gpu else 0, 1000.0 / (enc0 * lo + gpu) if enc0 * lo + gpu else 0),
            'r_ins': r_ins, 'r_cpu': r_cpu, 'r_call': r_call, 'gpu_terms': gpu_terms_(model, shape),
            'enc_terms': {'encode per draw': enc},
        }
    return rows


def gpu_terms_(model, shape):
    ch = model['gpu_chosen']
    c = model['gpu_fits'][ch]['coef_ms_per_job']
    g = lambda k: float(shape.get(k) or 0)
    if ch == 'fs_vs_rest':
        return {'gpu fs-size jobs': c['fs_job'] * (g('fs_load') + g('fs_clear')),
                'gpu small/other jobs': c['rest_job'] * (g('off_load') + g('off_clear') + g('other_jobs'))}
    if ch == 'five_class':
        return {'gpu ' + k: c[k] * g(k if k != 'other' else 'other_jobs') for k in c}
    return {'gpu kernel jobs': fm.predict_gpu_ms(model, shape)}


# ------------------------------------------------------------------ report
def device_reference(model, wins):
    """Device c6jb windows by structure -> per-command draws/jobs/ms for side-by-side."""
    ref = {}
    for r in fm.device_fps_table(model, wins):
        a = r['steady'] or r['all']
        if a:
            ref[r['type'].split(' ')[0]] = a
    return ref


def table(agg, pred, ref, out):
    p = lambda *a: print(*a, file=out)
    p('')
    p('== Host replay (steady median per frame; host != device) and device-model prediction (c6jb config = batched) ==')
    hdr = ('%-10s %-7s %5s %4s %5s %5s %4s | %7s %6s %7s %5s | %6s %6s %6s %5s %4s | %6s %6s %6s %6s | %5s %5s')
    p(hdr % ('scenario', 'mode', 'draws', 'pass', 'jobs', 'kjobs', 'calls', 'enc_us', 'cpu_us', 'ins(k)', 'us/dr', 'allocK',
             'mapK', 'copyK', 'peakM', 'inv', 'gpu_ms', 'enc_ms', 'frm_ms', 'serial', 'fps_s', 'fps_p'))
    for (scn, mode), a in sorted(agg.items(), key=lambda kv: (kv[0][1] != 'batched', kv[0][0])):
        pr = pred.get((scn, mode))
        if mode == 'perdraw':
            pr = None
        if a['invalid']:
            print('%-10s %-7s INVALID: command buffer refused by the frontend (not replayed as a frame)' % (scn, mode))
            continue
        p(hdr % (scn, mode, '%.0f' % a['draws'], '%.0f' % a['passes'], '%.0f' % a['jobs_enc'], '%.0f' % a['kjobs'],
                 '%.0f' % a['kernel_calls'], '%.0f' % a['enc_us'], '%.0f' % a['enc_cpu_us'], '%.0f' % (a['enc_ins'] / 1e3),
                 '%.0f' % (a['enc_us'] / a['draws'] if a['draws'] else 0), '%.0f' % a['alloc_kib'], '%.0f' % a['map_kib'],
                 '%.0f' % a['copy_kib'], '%.1f' % (a['peak_kib'] / 1024.0), '%.0f' % a['invalid'],
                 '%.0f' % pr['gpu_ms'] if pr else '-', '%.0f' % pr['enc_ms'] if pr else '-',
                 '%.0f' % pr['frame_ms_pipelined'] if pr else '-', '%.0f' % pr['frame_ms_serial'] if pr else '-',
                 '%.1f' % pr['fps_serial'] if pr else '-', '%.1f' % pr['fps_pipelined'] if pr else '-'))
    p('  columns: jobs = GPU jobs encoded, kjobs = kernel submissions after JM batching, calls = fake-kernel calls')
    p('  (alloc+release+sync+submit+wait+map), allocK/mapK/copyK = KiB allocated/mapped/bridge-copied per frame,')
    p('  fps_s = 1000/(enc+gpu) serialized, fps_p = 1000/max(enc,gpu) pipelined (commit executes on a queue thread).')
    p('')
    p('== Per-term cost vs the 16.7 ms (60 fps) and 33.3 ms (30 fps) budgets (batched, predicted device ms) ==')
    p('%-10s %8s | %s' % ('scenario', 'frame_ms', 'terms (ms, % of frame)'))
    for (scn, mode), pr in sorted(pred.items()):
        if mode != 'batched':
            continue
        terms = dict(pr['enc_terms'])
        terms.update(pr['gpu_terms'])
        tot = sum(terms.values()) or 1.0
        txt = '  '.join('%s %.0f (%.0f%%)' % (k, v, 100 * v / tot) for k, v in sorted(terms.items(), key=lambda kv: -kv[1]) if v > 0.5)
        p('%-10s %8.0f | %s' % (scn, tot, txt))
        p('%-10s over 33.3 ms by %.0f ms (%.1fx), over 16.7 ms by %.0f ms; fps serial if encode were kernel-call-bound (pessimistic) %.1f .. instruction-scaled %.1f' % (
            '', max(tot - BUDGET_30, 0), tot / BUDGET_30, max(tot - BUDGET_60, 0), *pr['fps_serial_range']))
    if ref:
        p('')
        p('== Device reference (c6jb windows, per command, steady) vs host-replay shape ==')
        p('%-8s %7s %6s %9s %9s | host scenario draws kjobs' % ('type', 'draws', 'kjobs', 'gpu_ms', 'enc_ms'))
        for k, a in ref.items():
            host = {'tiny': 'tick', 'small': 'small', 'medium': 'medium', 'large': 'large'}.get(k)
            hb = agg.get((host, 'batched')) if host else None
            p('%-8s %7.1f %6.1f %9.1f %9.1f | %s' % (k, a['draws_per_cmd'], a['kernel_jobs_per_cmd'], a['gpu_ms_per_cmd'],
                                                     a['encode_ms_per_cmd'],
                                                     ('%s %.0f %.0f' % (host, hb['draws'], hb['kjobs'])) if hb else '-'))


def anchor_table(model, agg, pred, baseline, out):
    p = lambda *a: print(*a, file=out)
    base = baseline['agg']
    p('')
    p('== Current tree vs device-run anchor %s (measured on the host; batched = c6jb config) ==' % baseline.get('anchor_commit'))
    p('%-10s | %5s %6s %6s | %8s %8s %6s | %7s %7s %6s | %6s %6s | %9s %9s' % (
        'scenario', 'draws', 'kj_an', 'kj_now', 'kins/dr_a', 'kins/dr_n', 'scale', 'enc_us_a', 'enc_us_n', 'x', 'calls_a', 'calls_n',
        'ms_anchor', 'ms_now'))
    for (scn, mode), a in sorted(agg.items()):
        b = base.get('%s/%s' % (scn, mode))
        if mode != 'batched' or not b or a['invalid'] or scn == 'cold_variants':
            continue
        pa = None
        shape_b = {'fs_load': b['fs_load'], 'fs_clear': b['fs_clear'], 'off_load': b['off_load'], 'off_clear': b['off_clear'],
                   'other_jobs': b['other']}
        gpu_a = fm.predict_gpu_ms(model, shape_b)
        enc_a = model['encode_fit_draw_only']['coef_ms']['e_draw'] * b['draws']
        pr = pred[(scn, mode)]
        p('%-10s | %5.0f %6.0f %6.0f | %8.0f %8.0f %6.3f | %7.0f %7.0f %6.2f | %6.0f %6.0f | %5.0f(%4.1f) %5.0f(%4.1f)' % (
            scn, a['draws'], b['kjobs'], a['kjobs'], b['enc_ins'] / 1e3 / b['draws'], a['enc_ins'] / 1e3 / a['draws'], pr['enc_scale'],
            b['enc_us'], a['enc_us'], a['enc_us'] / b['enc_us'] if b['enc_us'] else 0, b['kernel_calls'], a['kernel_calls'],
            gpu_a + enc_a, 1000 / (gpu_a + enc_a), pr['frame_ms_serial'], pr['fps_serial']))
    p('  ms (fps serial) predicted for the device; anchor column = what c6jb should show on the device with these shapes.')
    p('  scale = host user instructions per frame now / anchor (range of instructions, cpu time, kernel calls: see JSON enc_scale_range).')
    p('  It assumes device encode time is CPU-bound frontend work, so it shrinks with the host instruction count.  Cross-check below:')
    p('  the same change priced with the anchor-measured device/host CPU-time ratio.  If the 6.45 ms/draw was instead bound by kernel calls,')
    p('  the pessimistic fps in the per-term table applies; the next device run (TEGU_PERF_ENCODE) decides.')
    p('')
    p('== Kernel calls per kernel job (batched; anchor -> now): alloc release map submit wait other | total ==')
    for (scn, mode), a in sorted(agg.items()):
        b = base.get('%s/%s' % (scn, mode))
        if mode != 'batched' or not b or a['invalid'] or scn == 'cold_variants' or not a['kjobs'] or not b['kjobs']:
            continue
        per = lambda d: [d[k] / d['kjobs'] for k in ('k_alloc', 'k_release', 'map_calls', 'k_submit', 'k_wait', 'k_other')]
        pa, pn = per(b), per(a)
        p('%-10s | %s | %4.1f -> %4.1f' % (scn, '  '.join('%3.1f->%3.1f' % (x, y) for x, y in zip(pa, pn)),
                                            sum(pa), sum(pn)))


def calibration_lines(model, agg, baseline, out):
    p = lambda *a: print(*a, file=out)
    ec = model.get('encode_fit_draw_only', {}).get('coef_ms', {}).get('e_draw')
    base = (baseline or {}).get('agg', {})
    rows = [(s, a, base.get('%s/batched' % s)) for (s, m), a in agg.items() if m == 'batched' and a['draws'] and s != 'cold_variants']
    if ec and rows and all(r[2] for r in rows):
        p('')
        p('== Host -> device CPU ratio, measured on the ANCHOR code (device encode ms per draw / anchor host CPU ms per draw) ==')
        for s, a, b in sorted(rows):
            hb = b['enc_cpu_us'] / b['draws'] / 1000.0
            p('  %-10s anchor host %.2f ms/draw (%.1f Minstr/draw), device %.2f ms/draw -> device/host %.1fx;  tree host %.3f ms/draw x ratio = %.2f ms/draw' % (
                s, hb, b['enc_ins'] / 1e6 / b['draws'], ec, ec / hb, a['enc_cpu_us'] / a['draws'] / 1000.0, a['enc_cpu_us'] / a['draws'] / 1000.0 * ec / hb))
        p('  A 3-4x device/host ratio with identical Minstr/draw on the host is what a CPU-bound per-draw hash (not CA time, not kernel calls) predicts.')
    elif rows:
        p('\n(no anchor frozen: run --anchor-commit <device-run commit> --freeze for the host->device CPU ratio)')


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--steady', type=int, default=4, help='steady frames per scenario/mode (default 4)')
    ap.add_argument('--scenario', help='only this scenario (tick small medium large ca200 blur_heavy)')
    ap.add_argument('--rebuild', action='store_true', help='force a rebuild')
    ap.add_argument('--no-history', action='store_true', help='do not append to history.jsonl')
    ap.add_argument('--anchor-commit', help='build/replay this commit (exported with git archive; the working tree is not touched), '
                    'e.g. a5c19df = the source that ran on the device (c6jb)')
    ap.add_argument('--freeze', action='store_true', help='freeze this run as baseline.json (the anchor every later run is measured against)')
    ap.add_argument('--json', action='store_true', help='print the JSON record instead of the table')
    ap.add_argument('--model-only', action='store_true', help='only fit/print the device model')
    ap.add_argument('--wait', action='store_true', help='wait for another frame_bench run instead of failing')
    ap.add_argument('--what-if', default='', help='custom what-if column (see frame_model.py --help)')
    ap.add_argument('--note', default='', help='free text stored with the history line')
    a = ap.parse_args()

    configure(a.anchor_commit)
    model, wins = fm.build_model()
    if a.model_only:
        fm.report(model, wins, fm.reference_series())
        return
    OUT.mkdir(parents=True, exist_ok=True)
    BIN.mkdir(parents=True, exist_ok=True)
    lock = open(OUT / '.lock', 'w')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | (0 if a.wait else fcntl.LOCK_NB))
    except BlockingIOError:
        raise SystemExit('frame_bench: another run holds %s/.lock (use --wait)' % OUT)
    st = repo_state()
    with open(OUT / 'build.log', 'a') as log:
        log.write('== %s head=%s dirty=%d\n' % (datetime.datetime.now().isoformat(timespec='seconds'), st['head'], st['dirty_files']))
        built = ensure_built(a.rebuild, log)
    t0 = time.time()
    frames, meta, stderr = run_replay(a.steady, a.scenario)
    agg = aggregate(frames)
    baseline = None if a.anchor_commit else load_baseline()
    if a.freeze:
        if not a.anchor_commit and st['dirty_files']:
            print('frame_bench: WARNING freezing a dirty working tree as the anchor; the anchor should be the device-run commit (--anchor-commit)')
        (OUT / 'baseline.json').write_text(json.dumps({
            'head': st['head'], 'diff_sha256': st['diff_sha256'], 'anchor_commit': a.anchor_commit, 'when': datetime.datetime.now().isoformat(timespec='seconds'),
            'agg': {'%s/%s' % k: v for k, v in agg.items()}}, indent=1))
    anchor_pred = predict(model, agg, None) if a.anchor_commit else None
    pred = predict(model, agg, baseline)
    if baseline is None and not a.anchor_commit:
        print('frame_bench: no baseline.json - run once with --anchor-commit <device-run commit> --freeze; encode scale is 1.0 (unmeasured) until then')
    ref = device_reference(model, wins)
    base_agg = (baseline or {}).get('agg', {})
    def _shape(x):
        return {'fs_load': x['fs_load'], 'fs_clear': x['fs_clear'], 'off_load': x['off_load'], 'off_clear': x['off_clear'], 'other_jobs': x['other']}
    shapes = []
    for (scn, mode), x in sorted(agg.items()):
        if mode != 'batched' or x['invalid'] or scn == 'cold_variants':
            continue
        b = base_agg.get('%s/%s' % (scn, mode))
        pr = pred[(scn, mode)]
        shapes.append((scn, _shape(x), x['draws'], x['passes'], pr['enc_scale'] if pr['anchored'] else None,
                       _shape(b) if b else None, b['draws'] if b else None))
    therm = {}
    try:
        therm = json.loads(pathlib.Path('$NORD2_REPO/research/nord2/'
                                        'gpu/overnight-20261024/claude-metal-orchestrator-20261025/runtime/resume-20261026/thermal-status.json').read_text())
    except Exception:
        pass
    rec = {
        'ts': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'), 'note': a.note,
        'git': st, 'build': {k: built.get(k) for k in ('inputs', 'libs', 'compute_source', 'head', 'built', 'cached')},
        'role': 'anchor' if a.anchor_commit else 'current', 'anchor_commit': a.anchor_commit,
        'baseline': ({'head': baseline['head'], 'anchor_commit': baseline.get('anchor_commit'), 'when': baseline['when']} if baseline else None),
        'host': {'meta': meta, 'replay_s': round(time.time() - t0, 1), 'temp_c': therm.get('temperature_c')},
        'model': {'logs': model['logs'], 'gpu_chosen': model['gpu_chosen'], 'gpu': model['gpu_fits'][model['gpu_chosen']],
                  'encode': model.get('encode_fit_draw_only'), 'encode3': model.get('encode_fit'), 'compile': model.get('compile_fit')},
        'whatif': {'%s/%s' % (it[0], nm): wi for it in shapes for nm, ov in fm.PATH for wi in [fm.whatif_row(model, it, ov)]},
        'scenarios': {'%s/%s' % k: dict(v, **{'pred': {kk: vv for kk, vv in pred[k].items()} if k in pred else None})
                      for k, v in agg.items()},
    }
    if not a.no_history:
        with open(OUT / 'history.jsonl', 'a') as h:
            h.write(json.dumps(rec, sort_keys=True) + '\n')
    (OUT / 'latest.json').write_text(json.dumps(rec, indent=1, sort_keys=True))
    if a.json:
        json.dump(rec, sys.stdout, indent=1, sort_keys=True)
        print()
        return
    print('frame_bench  head=%s  dirty=%d (diff %s)  build=%s  compute_src=%s  host_temp=%s  ins_counter=%s' % (
        st['head'], st['dirty_files'], st['diff_sha256'], 'cached' if built.get('cached') else 'rebuilt',
        built.get('compute_source'), therm.get('temperature_c'), meta.get('ins_counter')))
    for s in st['status']:
        print('   ' + s)
    if baseline:
        print('anchor: %s (%s) frozen %s' % (baseline.get('anchor_commit'), baseline['head'], baseline['when']))
    fm.report(model, wins, fm.reference_series())
    table(agg, pred, ref, sys.stdout)
    fm.scenario_table(model, shapes, title='(host-replayed shapes)', extra=fm.parse_overrides(a.what_if))
    if baseline:
        anchor_table(model, agg, pred, baseline, sys.stdout)
    calibration_lines(model, agg, baseline, sys.stdout)
    print('\nhistory: %s%s' % (OUT / 'history.jsonl', ' (not written)' if a.no_history else ''))


if __name__ == '__main__':
    main()
