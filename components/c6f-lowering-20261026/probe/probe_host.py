#!/usr/bin/env python3
"""Host fake-kernel run of the batch-features-v9 probe sequence (no GPU, no phone).

Assembles the probe AIR (.ll -> .bc) with the host llvm-as/llvm-link, builds
frame_bench's real-frontend prelude with this directory's frame_bench_main.m
(feature replica) and runs it with the probe's feature toggles.  Exit 0 = every
feature's structural check passed (status, jobs, chain, pre-frame, LD_TILE,
WAIT_SEQ, command-pool reuse).  Pixel correctness is device-only.
"""
import os, pathlib, subprocess, sys, time
HERE = pathlib.Path(__file__).resolve().parent
GPU = HERE.parents[2]
sys.path.insert(0, str(GPU / 'tools'))
import frame_bench as fb  # noqa: E402

LL = [GPU / 'metal/tegu/varying_probe.ll', GPU / 'tests/shaders/metal_probe_compute.ll',
      GPU / 'tests/shaders/metal_probe_geometry.ll', GPU / 'tests/shaders/metal_probe_fetch.ll']


def main():
    llvm = os.environ.get('PROBE_LLVM_BIN', '/usr/bin')
    bcs = []
    for ll in LL:
        out = HERE / (ll.stem + '.bc')
        subprocess.run([llvm + '/llvm-as', str(ll), '-o', str(out)], check=True)
        bcs.append(str(out))
    linked = HERE / 'probe-host.bc'
    subprocess.run([llvm + '/llvm-link'] + bcs + ['-o', str(linked)], check=True)
    fb.HERE, fb.BIN = HERE, HERE
    src = fb.gen_source(False)
    (HERE / 'probe_host.m').write_text(src)
    (HERE / 'hostbase_real_nord2_metal_bridge.c').write_bytes((GPU / 'metal/nord2_metal_bridge.c').read_bytes())
    (HERE / 'nord2_metal_bridge.h').write_bytes((GPU / 'metal/nord2_metal_bridge.h').read_bytes())
    binary = HERE / 'probe_host_bin'
    cmd = ['nice', '-n', '10', 'clang', '-fblocks', '-pthread', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           '-Wno-incompatible-pointer-types', '-Wno-incompatible-function-pointer-types', '-w', '-fconstant-string-class=NXConstantString',
           '-DTEGU_HOST_REAL_V2', '-DTEGU_INERT_PIPELINES', '-DC6F_ATTACHMENTS=2', '-DTEGU_JM_BATCH_DEFAULT', '-DFB_HAVE_ASYNC=1',
           '-DTEGU_TILE_FETCH_DEFAULT=1']
    cmd += ['-I' + str(p) for p in (HERE, fb.TEGU, GPU / 'metal', GPU / 'include', GPU / 'build/tegu-frontend')]
    cmd += [str(HERE / 'probe_host.m'), str(HERE / 'hostbase_real_nord2_metal_bridge.c'), str(GPU / 'metal/nord2_metal_render.c'),
            str(GPU / 'src/nord2_gpu_desc.c'), str(GPU / 'src/nord2_gpu_jm.c')]
    cmd += ['-lobjc', '-ldl', '-lBlocksRuntime'] + ['-L' + str(p) for p in fb.LIBS]
    cmd += ['-ltegu_air_bridge', '-lnord2_mali_runtime', '-o', str(binary)]
    t = time.time()
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    (HERE / 'build.log').write_text(' '.join(cmd) + '\n' + r.stderr)
    if r.returncode:
        sys.exit('build failed:\n' + r.stderr[-3000:])
    print('built in %.0fs' % (time.time() - t))
    env = dict(os.environ, LD_LIBRARY_PATH=':'.join(map(str, fb.LIBS)), ASAN_OPTIONS='detect_leaks=0', TEGU_PLAN_MEMO_VERIFY='1',
               FB_ASYNC='1', TEGU_AB_SCHEDULE='/nonexistent/no-ab-schedule', NORD2_METAL_PREFRAME='1', TEGU_TILE_FETCH='1',
               TEGU_JM_BATCH='1', NORD2_METAL_BATCH_MAX='128', TEGU_NATIVE_INDEXED='1', NORD2_METAL_ASYNC='1')
    r = subprocess.run(['nice', '-n', '10', str(binary), str(linked)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (HERE / 'probe-host.log').write_text(r.stdout)
    for line in r.stdout.splitlines():
        if line.startswith('HOST'):
            print(line)
    if r.returncode or 'FB_DONE' not in r.stdout:
        print(r.stdout[-3000:])
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
