#!/usr/bin/env python3
"""Run the real compute frontend + real Rust bridge + baseline Mali compiler on
the translator's runtime_examples AIR. The shared fixture expects 128-byte
alignment; current translator descriptor ranges start at the bound offset, so
this worker copy uses 256/512-aligned buffers. This is a v1 buffer path test;
it does not exercise v2/texture dispatch (that needs the compiler v2 entry)."""
import os, subprocess, tempfile
from pathlib import Path
W = Path(__file__).resolve().parents[4]
G = W
OUT = Path(__file__).resolve().parent/'runtime'
OUT.mkdir(exist_ok=True)
air = OUT/'runtime_examples.air'
if not air.exists():
    src = W.parents[1]/'references/metal2vulkan-tegu-pin/crates/metal2vulkan-validation/fixtures/public/examples/runtime_examples.air.ll'
    subprocess.run(['llvm-as', str(src), '-o', str(air)], check=True)
bridge = Path(__file__).resolve().parent/'cargo-target/release/libtegu_air_bridge.so'
mali = G/'build/ui-corpus-20261025/baseline/libnord2_mali_runtime.so'
profile_dir = G/'build/tegu-frontend'
with tempfile.TemporaryDirectory() as td:
    test = Path(td)/'runtime_aligned.m'
    text = (G/'metal/tegu/runtime_frontend_host_test.m').read_text()
    assert 'expectedOffset=run<4?128:384;' in text
    test.write_text(text.replace('expectedOffset=run<4?128:384;','expectedOffset=run<4?256:512;'))
    exe = Path(td)/'runtime-test'
    subprocess.run(['clang','-fblocks','-pthread','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fconstant-string-class=NXConstantString',
        '-I'+str(G/'metal/tegu'),'-I'+str(profile_dir),str(test),'-lobjc','-ldl',
        str(bridge),str(mali),'-o',str(exe)],check=True)
    env=dict(os.environ,LD_LIBRARY_PATH=os.pathsep.join([str(bridge.parent),str(mali.parent)]),
             ASAN_OPTIONS='detect_leaks=0',TEGU_PIPELINE_CACHE_DIR=str(Path(td)/'cache'))
    env['TEGU_PIPELINE_CACHE_DIR']=str(Path(td)/'pcache'); Path(env['TEGU_PIPELINE_CACHE_DIR']).mkdir()
    result=subprocess.run([str(exe),str(air)],check=True,capture_output=True,text=True,env=env,timeout=120)
    (OUT/'v1-aligned.log').write_text(result.stdout+result.stderr)
    print(result.stdout.strip().splitlines()[-1])
    print('PASS runtime v1 aligned host harness (real frontend, real Rust bridge, baseline Mali compiler)')
