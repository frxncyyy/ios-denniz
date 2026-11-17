#!/usr/bin/env python3
"""Host-only true-MRT bridge/target-token test through a fake JM model."""
import os, re, subprocess
from pathlib import Path
HERE = Path(__file__).resolve().parent
GPU = HERE.parents[3]
OUT = HERE/'runtime'; OUT.mkdir(exist_ok=True)
CC = os.environ.get('CC','cc')
common = [CC,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
 '-Wno-misleading-indentation','-Wno-array-parameter',
 '-I'+str(GPU/'include'),'-I'+str(GPU/'metal'),'-I'+str(GPU/'metal/tegu'),
 '-I'+str(GPU/'build/tegu-frontend'),'-DUTIL_ARCH_LITTLE_ENDIAN=1','-DUTIL_ARCH_BIG_ENDIAN=0',
 '-I'+str(GPU/'build/mesa-build/src/panfrost'),'-I'+str(GPU/'build/mesa-src/src/panfrost'),
 '-I'+str(GPU/'build/mesa-build/src'),'-I'+str(GPU/'build/mesa-src/src'),
 '-I'+str(GPU/'build/mesa-build/include'),'-I'+str(GPU/'build/mesa-src/include')]
sources=[str(HERE/'tests/check_mrt_bridge.c'),str(GPU/'metal/nord2_metal_bridge.c'),
 str(GPU/'metal/nord2_metal_render.c'),str(GPU/'metal/tegu/blend_state.c'),
 str(GPU/'src/nord2_gpu_jm.c'),str(GPU/'src/nord2_gpu_desc.c')]
def run(name,extra):
 exe=OUT/name
 subprocess.run(common+list(extra)+sources+['-o',str(exe),'-lm'],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True,
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'),timeout=120)
 (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
 m=re.search(r'n2mb MRT: (\d+) checks, (\d+) required failures',result.stdout+result.stderr)
 assert result.returncode==0 and m and int(m.group(2))==0, result.stdout+result.stderr
 print(f'PASS {name}: {m.group(1)} checks, 0 required failures')
# Release first (thermal-limited); one sanitizer pass only after it is green.
run('n2mb_mrt', [])
run('n2mb_mrt_asan', ['-fsanitize=address,undefined','-fno-sanitize-recover=all'])
