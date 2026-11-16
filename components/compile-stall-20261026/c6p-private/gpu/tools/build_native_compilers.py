#!/usr/bin/env python3
"""Private Linux->iOS ARM64 compiler build, using a real iOS SDK.

No device, publication or installation. SDK and Rust std remain in build/.
"""
from pathlib import Path
import os, subprocess, shutil, sys, hashlib, tarfile, json
root=Path(__file__).resolve().parents[1]
apple=root/'build/apple-toolchain'
sdk=apple/'sdks/iPhoneOS16.5.sdk'
assert (sdk/'usr/include/stdlib.h').is_file()
out=root/'build/native';out.mkdir(exist_ok=True)
env=dict(os.environ,PATH=str(root/'build/venv/bin')+os.pathsep+os.environ['PATH'],SDKROOT=str(sdk),IPHONEOS_DEPLOYMENT_TARGET='16.0')
ar=Path('$HOME/Documents/Codex/2026-10-13/por/work/huawei/command-line-tools/sdk/default/openharmony/native/llvm/bin/llvm-ar')
if sys.argv[1]=='air':
    env['RUSTC']=str(apple/'rust-1.99.0/bin/rustc')
    version=subprocess.check_output([env['RUSTC'],'--version'],text=True).split()[1]
    archive=apple/f'rust-std-{version}-aarch64-apple-ios.tar.xz'
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==Path(str(archive)+'.sha256').read_text().split()[0]
    with tarfile.open(archive) as t:t.extractall(apple,filter='data')
    prefix=apple/f'rust-std-{version}-aarch64-apple-ios/rust-std-aarch64-apple-ios'
    subprocess.run([sys.executable,str(root/'metal/tegu/build_air_bridge.py'),'--translator',str(root/'build/metal2vulkan'),
        '--work',str(root.parent/'research/nord2-native-air'),'--target','aarch64-apple-ios','--static-only','--rust-sysroot',str(prefix)],env=env,check=True)
else:
    source=root/'build/mesa-native-src';build=root/'build/mesa-native-build'
    if not source.exists():
        subprocess.run(['git','clone','--shared','--no-checkout',str(root/'build/mesa-src'),str(source)],check=True)
        subprocess.run(['git','-C',str(source),'checkout','--detach','81daea66703eeea1a232446e3a36902353fc3045'],check=True)
    for name in ('mali_compute_compiler.c','mali_compute_compiler.h','mali_graphics_compiler.c','mali_graphics_compiler.h','mali_graphics_resources.inc','compiler_trace_sha256.h','pipeline_format.h','nord2_target.h'):
        shutil.copyfile(root/'metal/tegu'/name,source/'src/panfrost/clc'/name)
    (source/'src/panfrost/clc/meson.build').write_text('''
shared_library('nord2_mali_runtime', ['mali_compute_compiler.c','mali_graphics_compiler.c'],
 c_args : ['-DTEGU_MALI_LIBRARY_ONLY'], link_with : [libpanfrost_compiler],
 include_directories : [inc_include, inc_src],
 dependencies : [idep_vtn, idep_nir, idep_mesautil, libpanfrost_model_dep], build_by_default : false)
''')
    sys.path.insert(0,str(root/'metal/tegu'))
    from build_mali_compiler import OPTIONS
    cross=out/'ios.ini'
    common=['-target','arm64-apple-ios16.0','-isysroot',str(sdk)]
    cross.write_text('[binaries]\n'+ '\n'.join(f'{k} = '+repr([v]+common+(['-stdlib=libc++'] if k=='cpp' else [])) for k,v in [('c','clang'),('cpp','clang++'),('objc','clang')])+f"\nc_ld = 'lld'\ncpp_ld = 'lld'\nobjc_ld = 'lld'\nar = '{ar}'\nstrip = '/usr/bin/true'\npkg-config = '/usr/bin/false'\n[host_machine]\nsystem = 'darwin'\ncpu_family = 'aarch64'\ncpu = 'armv8'\nendian = 'little'\n[properties]\nneeds_exe_wrapper = true\n")
    cmd=['meson','setup']+(['--wipe'] if (build/'build.ninja').exists() else [])
    subprocess.run(cmd+[str(build),str(source),'--cross-file',str(cross)]+OPTIONS+['-Dspirv-tools=disabled','-Dzstd=disabled','-Dzlib=disabled','-Dexpat=disabled','-Dshader-cache=disabled'],env=env,check=True)
    target='src/panfrost/clc/libnord2_mali_runtime.dylib'
    subprocess.run(['ninja','-C',str(build),'-j'+os.environ.get('NORD2_NINJA_JOBS','1'),target],env=env,check=True)
    archives=sorted(build.rglob('*.a'));objects=sorted((build/(target+'.p')).glob('*.o'))
    # MRI addlib flattens nested archives (ordinary ar r would not).
    script='create '+str(out/'libNord2Mali.a')+'\n'+''.join('addlib '+str(a)+'\n' for a in archives)+''.join('addmod '+str(o)+'\n' for o in objects)+'save\nend\n'
    subprocess.run([str(ar),'-M'],input=script,text=True,check=True)
    shutil.copyfile(build/target,out/'libNord2Mali.dylib')
    (out/'mali-receipt.json').write_text(json.dumps({'target':'arm64-apple-ios16.0','sdk':str(sdk),'archives':[str(a) for a in archives],'device_tested':False},indent=2)+'\n')
