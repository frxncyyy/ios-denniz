#!/usr/bin/env python3
"""API-surface host test: events/fences/queues/encoders/device factories (host only, no GPU claim).

Builds the production Objective-C frontend (compute_frontend.m with its .inc files) exactly like
tests/texture_format_frontend_test.py (the render harness prelude is executed, not copied), links the
real AIR bridge / Mali compiler / n2mb bridge over the fake JM kernel, and runs
metal/tegu/api_surface_host_test.inc, which holds the cases.  Cases are independent oracles (ordering by
thread interleaving, retain/release balance via the fixture's live counter, exact refusal behaviour).

Usage: python3 gpu/tests/api_surface_frontend_test.py
Environment: TEGU_RELEASE=1 builds without ASan/UBSan; API_FAST=1 builds -O0 without sanitizers (iteration only).
"""
import os
import pathlib
import subprocess
import sys

G = pathlib.Path(__file__).resolve().parents[1]
HARNESS = G / 'build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py'
OUT = G / 'build/api-surface-20261027/host'
OUT.mkdir(parents=True, exist_ok=True)

text = HARNESS.read_text()
marker = "source += r'''\n/* Linux uint64_t"
pre = text[:text.index(marker)]
ns = {'__file__': str(HARNESS), '__name__': 'api_surface_prelude'}
exec(compile(pre, str(HARNESS), 'exec'), ns)
source = ns['source']
helpers = text.split(marker, 1)[1].split('int main(int argc,char **argv){', 1)[0]
source += '/* Linux uint64_t' + helpers
source += '\n#include <pthread.h>\n#include <unistd.h>\n'
source += (ns['TEgu'] / 'api_surface_host_test.inc').read_text()
source += (ns['TEgu'] / 'runtime_frontend_host_test.m').read_text().split(
    'NSUInteger tegu_host_constant_data_size(NSUInteger type)', 1)[1].join(
    ['NSUInteger tegu_host_constant_data_size(NSUInteger type)', ''])
path = OUT / 'api_surface_frontend_test.m'
path.write_text(source)
bridge_c = OUT / 'bridge_real.c'
_bridge = (G / 'metal/nord2_metal_bridge.c').read_text()
if os.environ.get('API_PATCH_CLEAR_PIXEL'):
    # stopgap while the bridge's n2mb_clear has 'uint8_t pixel[8]' for clear_pixel(...,pixel[16]) (reported on the board)
    _bridge = _bridge.replace('{uint8_t pixel[8];clear_pixel(source,pixel);memcpy((void *)(uintptr_t)(slot>=0?b->clear_slab->cpu+offset:color->cpu),pixel,8);}',
                              '{uint8_t pixel[16];clear_pixel(source,pixel);memcpy((void *)(uintptr_t)(slot>=0?b->clear_slab->cpu+offset:color->cpu),pixel,16);}')
bridge_c.write_text(_bridge)
libs = [os.environ.get('TEGU_AIR_LIB_DIR', str(G / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release')),
        str(G / 'build/mesa-build/src/panfrost/clc')]
cmd = ['nice', '-n', '10', 'clang', '-fblocks', '-pthread', '-O0' if os.environ.get('API_FAST') else '-O1', '-g',
       '-DTEGU_ENABLE_MSAA4', '-DTEGU_HOST_REAL_V2', '-DTEGU_EVENT_WAIT_LIMIT_S=1']
if not os.environ.get('TEGU_RELEASE') and not os.environ.get('API_FAST'):
    cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
cmd += ['-Wno-incompatible-pointer-types', '-Wno-incompatible-function-pointer-types',
        '-fconstant-string-class=NXConstantString']
cmd += ['-I' + str(p) for p in (ns['TEgu'], G / 'metal', G / 'include', G / 'build/tegu-frontend')]
cmd += os.environ.get('API_EXTRA_CFLAGS', '').split()
cmd += [str(path), str(bridge_c), str(G / 'metal/nord2_metal_render.c'),
        str(G / 'src/nord2_gpu_desc.c'), str(G / 'src/nord2_gpu_jm.c')]
cmd += ['-lobjc', '-ldl', '-lBlocksRuntime', '-lm'] + ['-L' + p for p in libs] + [
    '-ltegu_air_bridge', '-lnord2_mali_runtime', '-o', str(OUT / 'api_surface_frontend_test')]
subprocess.run(cmd, check=True)
# Fixture is hand-written LLVM IR, not Apple AIR: frontend-feature test, not codegen -> m2v code path (coordinator decision a, 2026-10-28).
env = dict(os.environ, LD_LIBRARY_PATH=':'.join(libs), ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1', TEGU_AIR_DIRECT='0')
R = HARNESS.parent
air = R / 'authored' / 'probe.air'
if not air.exists():
    subprocess.run(['llvm-as', str(R / 'vertex_texture_probe.ll'), '-o', str(air)], check=True)
compute_air = OUT / 'metal_probe_compute.air'
subprocess.run(['llvm-as', str(G / 'tests/shaders/metal_probe_compute.ll'), '-o', str(compute_air)], check=True)
result = subprocess.run([str(OUT / 'api_surface_frontend_test'), str(air), str(compute_air)], env=env,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
out = result.stdout.decode('utf-8', 'replace')
err = result.stderr.decode('utf-8', 'replace')
sys.stdout.write(out)
(OUT / 'run.stderr.log').write_text(err)
if result.returncode:
    sys.stderr.write(err[-6000:])
    sys.exit(result.returncode)
assert 'api_surface_frontend_test: PASS' in out, out
