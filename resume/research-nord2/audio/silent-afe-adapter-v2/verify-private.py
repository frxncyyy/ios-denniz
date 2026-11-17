#!/usr/bin/env python3
"""Source-only AFE adapter checks. Never builds an image or touches hardware."""
from pathlib import Path
import subprocess
import tempfile

p = Path(__file__).resolve().parent
r = p.parents[3]
rev5 = r / 'research/nord2/audio/revision5-review/candidate'
iddev = r / 'research/nord2/audio/id-integration-current/proposed/bsd/dev'
bt = r / 'research/ci-39398655023/buildtree'
canon = r / 'research/ci-39158169212/buildtree/xnu/bsd/dev'
cc = r / 'tools/nord2-xnu-cc.py'
nm = r.parent / 'toolchains/proton-clang/bin/llvm-nm'

def run(args):
    x = subprocess.run([str(a) for a in args], cwd=r, text=True, capture_output=True)
    if x.returncode:
        raise RuntimeError(f'{args}\n{x.stdout}\n{x.stderr}')
    return x.stdout + x.stderr

with tempfile.TemporaryDirectory(prefix='nord2-naa-') as d:
    tmp = Path(d)
    test = tmp / 'adapter-test'
    run(['clang','-std=c11','-O1','-g','-DNORD2','-fsanitize=address,undefined',
         '-fno-omit-frame-pointer','-Wall','-Wextra','-Wno-return-type',
         '-I'+str(rev5/'shim/nord2-xnu'),'-I'+str(p),
         p/'test-adapter.c',p/'audio_afe_adapter.c',
         rev5/'shim/nord2-xnu/audio_afe.c',rev5/'shim/nord2-xnu/audio_id.c',
         '-o',test])
    for case in ('no-arm','success','fault-first','fault-last','wrong-value','wrong-phase',
                 'wrong-fill','worker-change','worker-after-write','live-read-fault','observe-fail'):
        out = run([test,case])
        assert 'adapter case PASS' in out
    native_test = tmp/'native-v2-test'
    run(['clang','-std=c11','-O1','-g','-DNORD2','-fsanitize=address,undefined',
         '-fno-omit-frame-pointer','-Wno-return-type','-I'+str(p),'-I'+str(iddev),
         '-I'+str(rev5/'tools/fixtures'),p/'test-native-v2.c',p/'audio_afe_adapter.c',
         rev5/'shim/nord2-xnu/audio_id.c',rev5/'shim/nord2-xnu/audio_i2c8.c',
         rev5/'shim/nord2-xnu/audio_afe.c','-o',native_test])
    assert 'PASS v2 same-measurement-worker consume and single AFE VA' in run([native_test])
    obj = tmp/'adapter.o'
    run(['python3',cc,bt,'bsd',p/'audio_afe_adapter.c',obj,
         '-I'+str(iddev),'-I'+str(p)])
    symbols = run([nm,obj])
    assert ' T _naa_prepare' in symbols and ' T _naa_run' in symbols
    sweet = (iddev/'sweet_touch.c').read_text()
    assert sweet.count('#include "audio_id_bind.inc"') == 1
    sweet = sweet.replace('#include "audio_id_bind.inc"',
                          '#include "audio_id_bind.v2.inc"\n#include "audio_afe_native.inc"')
    assert 'naa_native_prepare();' not in sweet
    src = tmp/'sweet_touch.naa.c'
    src.write_text(sweet)
    obj = tmp/'sweet_touch.naa.o'
    run(['python3',cc,bt,'bsd',src,obj,
         '-I'+str(canon),'-I'+str(iddev),'-I'+str(p)])
    symbols = run([nm,obj])
    assert ' t _naa_native_prepare' in symbols and ' U _naa_prepare' in symbols
    assert ' U _naa_run' not in symbols
print('PASS source-only adapter v2: 11 ASan/UBSan core cases, native lifetime/VA fixture, normal ARM64 whole TU; no call site')
