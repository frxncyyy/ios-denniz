#!/usr/bin/env python3
"""tools/build_native_compilers.py mali without 'meson setup --wipe': copy the compiler sources
into the configured iOS Mesa build, ninja -j1 the runtime dylib, then the same MRI archive,
dylib copy and receipt as the tool (incremental: only changed objects recompile)."""
from pathlib import Path
import os, subprocess, shutil, json
root=Path('$NORD2_WORK/gpu')
apple=root/'build/apple-toolchain';sdk=apple/'sdks/iPhoneOS16.5.sdk';out=root/'build/native'
env=dict(os.environ,PATH=str(root/'build/venv/bin')+os.pathsep+os.environ['PATH'],SDKROOT=str(sdk),IPHONEOS_DEPLOYMENT_TARGET='16.0')
ar=Path('$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin/llvm-ar')
source=root/'build/mesa-native-src';build=root/'build/mesa-native-build'
assert (build/'build.ninja').exists()
for name in ('mali_compute_compiler.c','mali_compute_compiler.h','mali_graphics_compiler.c','mali_graphics_compiler.h','mali_graphics_resources.inc','compiler_trace_sha256.h','pipeline_format.h','nord2_target.h'):
    shutil.copyfile(root/'metal/tegu'/name,source/'src/panfrost/clc'/name)
target='src/panfrost/clc/libnord2_mali_runtime.dylib'
subprocess.run(['nice','-n','10','ninja','-C',str(build),'-j1',target],env=env,check=True)
archives=sorted(build.rglob('*.a'));objects=sorted((build/(target+'.p')).glob('*.o'))
script='create '+str(out/'libNord2Mali.a')+'\n'+''.join('addlib '+str(a)+'\n' for a in archives)+''.join('addmod '+str(o)+'\n' for o in objects)+'save\nend\n'
subprocess.run([str(ar),'-M'],input=script,text=True,check=True)
shutil.copyfile(build/target,out/'libNord2Mali.dylib')
(out/'mali-receipt.json').write_text(json.dumps({'target':'arm64-apple-ios16.0','sdk':str(sdk),'archives':[str(a) for a in archives],'device_tested':False,'incremental':'compile-stall native_mali_incremental.py'},indent=2)+'\n')
print('native mali done')
