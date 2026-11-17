#!/usr/bin/env python3
"""Run the compute v2 host suites against the real bridge/parser sources."""
import ctypes as C
import hashlib, json, os, subprocess
from pathlib import Path
W = Path(__file__).resolve().parents[4]
ROOT = W
OUT = Path(__file__).resolve().parent/'v2-out'
OUT.mkdir(parents=True, exist_ok=True)
CC = os.environ.get('CC','cc')
common = [CC,'-std=c11','-Wall','-Wextra','-Wno-misleading-indentation','-Wno-array-parameter',
          '-I'+str(ROOT/'include'),'-I'+str(ROOT/'metal'),'-I'+str(ROOT/'metal/tegu'),
          '-I'+str(ROOT/'build/tegu-frontend')]
def run(name, sources, out, extra=(), args=(), want=None):
    exe = OUT/name
    cmd = common+list(extra)+[str(Path(__file__).parent/'tests'/s) for s in sources]+['-o',str(exe)]
    subprocess.run(cmd, check=True)
    result = subprocess.run([str(exe), *args], check=True, capture_output=True, text=True,
                            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
    text=result.stdout+result.stderr
    (OUT/(name+'.log')).write_text(text)
    if want is not None:
        assert want in text, text
    print(result.stdout.strip())
    return hashlib.sha256(exe.read_bytes()).hexdigest()

class Blob(C.Structure):
    _fields_=[('bytes',C.POINTER(C.c_uint8)),('length',C.c_size_t)]

def real_package():
    """Compile the selected real corpus SPIR-V with lowering's private L2
    buffer-table compiler. The package carries bindings 0..30 and the w19
    extended-table mask; the C bridge test supplies all 11 resources through
    the fake JM. No GPU execution."""
    spv=W/'build/deepseek-parallel-20261025/workers/corpus_inputs/evidence-current-57db4be4-8861e0a0/compute_all/runs/ci-3d730e2e38ded1afdebb/compute.spv'
    libpath=W/'build/lowering-claude-20261025/lib-buftable/libnord2_mali_runtime.so'
    if not spv.exists() or not libpath.exists():
        raise RuntimeError('missing real corpus SPV or lowering lib-buftable')
    lib=C.CDLL(str(libpath));fn=lib.tegu_mali_compile_compute_v2
    fn.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_size_t,C.POINTER(Blob),C.c_void_p,C.c_size_t]
    fn.restype=C.c_int
    lib.tegu_mali_free.argtypes=[C.POINTER(Blob)]
    data=spv.read_bytes();blob=Blob();err=C.create_string_buffer(1024)
    rc=fn(data,len(data),None,0,C.byref(blob),err,len(err))
    if rc:
        raise RuntimeError(f'private buftable compiler refused: {err.value!r}')
    packet=C.string_at(blob.bytes,blob.length) if blob.length else b''
    lib.tegu_mali_free(C.byref(blob))
    if not packet:
        raise RuntimeError('private buftable compiler emitted no packet')
    path=OUT/'real-buftable.tgp2';path.write_bytes(packet)
    return str(path), hashlib.sha256(packet).hexdigest(), hashlib.sha256(libpath.read_bytes()).hexdigest()
rows = {}
rows['pipeline_format_v2'] = run('check_pipeline_format_v2', ['check_pipeline_format_v2.c'], OUT)
V9FLAGS=('-Wno-unused-parameter','-DUTIL_ARCH_LITTLE_ENDIAN=1','-DUTIL_ARCH_BIG_ENDIAN=0','-I'+str(ROOT/'build/mesa-build/src'),'-I'+str(ROOT/'build/mesa-src/src'),'-I'+str(ROOT/'build/mesa-src/src/panfrost'),'-I'+str(ROOT/'build/mesa-src/include'),'-I'+str(ROOT/'build/mesa-build/include'))
bridge_sources=['check_compute_v2_bridge.c',
    ROOT/'metal/nord2_metal_bridge.c', ROOT/'src/nord2_gpu_jm.c', ROOT/'src/nord2_gpu_desc.c',
    ROOT/'metal/nord2_metal_render.c', ROOT/'metal/tegu/blend_state.c']
rows['bridge_v2'] = run('check_compute_v2_bridge', bridge_sources, OUT, V9FLAGS)
real_pkg,real_pkg_hash,real_lib_hash=real_package()
rows['bridge_real'] = run('check_compute_v2_bridge_real', bridge_sources, OUT, V9FLAGS,
    (real_pkg,), 'PASS real corpus buffer table')
rows['pipeline_format_v2_asan'] = run('check_pipeline_format_v2_asan',
    ['check_pipeline_format_v2.c'], OUT, ('-fsanitize=address,undefined','-g'))
rows['bridge_v2_asan'] = run('check_compute_v2_bridge_asan', bridge_sources, OUT,
    ('-fsanitize=address,undefined','-g')+V9FLAGS)
rows['bridge_real_asan'] = run('check_compute_v2_bridge_real_asan', bridge_sources, OUT,
    ('-fsanitize=address,undefined','-g')+V9FLAGS, (real_pkg,), 'PASS real corpus buffer table')
(OUT/'result.json').write_text(json.dumps({'gpu_executed': False, 'phone_accessed': False,
    'binaries': rows, 'real_package_sha256': real_pkg_hash,
    'real_compiler_sha256': real_lib_hash}, indent=2)+'\n')
print('PASS compute v2 host suites (release + ASan/UBSan)')
