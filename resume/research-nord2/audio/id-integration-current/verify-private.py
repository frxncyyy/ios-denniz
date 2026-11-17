#!/usr/bin/env python3
"""Compile the private audio-ID integration with real NORD2 CI headers."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile

P = Path(__file__).resolve().parent
ROOT = P.parents[3]
PROPOSED = P / 'proposed/bsd/dev'
BT = ROOT / 'research/ci-39398655023/buildtree'
CANON = ROOT / 'research/ci-39158169212/buildtree/xnu/bsd/dev'
BOARD = BT / 'build/xnu.obj/EXPORT_HDRS/pexpert/pexpert/arm64/SWEET.h'
CC = ROOT / 'tools/nord2-xnu-cc.py'
NM = ROOT.parent / 'toolchains/proton-clang/bin/llvm-nm'
AUDIO = ROOT / 'research/nord2/audio/revision5-review/candidate'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(argv):
    cp = subprocess.run([str(x) for x in argv], cwd=ROOT, capture_output=True, text=True)
    if cp.returncode:
        raise RuntimeError(f'{argv!r}\n{cp.stdout}\n{cp.stderr}')
    return cp.stdout + cp.stderr

assert sha(BOARD) == '2d69309edcdbaedc78c947055d66cefd38fe4e54fe49f7f8f110ffc4ba48db25'
assert '#define NORD2' in BOARD.read_text()
assert sha(P / 'proposed/bsd/dev/audio_id_bind.inc') == sha(AUDIO / 'shim/nord2-xnu/audio_id_bind.inc')
bind = (PROPOSED / 'audio_id_bind.inc').read_text()
entry = bind.index('nab_audio_id(void)')
gate = bind.index('if (level != 1 && level != 2) { return; }', entry)
mapping = bind.index('ml_io_map(', entry)
assert entry < gate < mapping
assert 'naf_cycle(' not in bind[entry:]  # AFE streaming stays uncalled.
sweet = (PROPOSED / 'sweet_touch.c').read_text()
assert sweet.count('#include "audio_id_bind.inc"') == 1
assert sweet.count('\tnab_audio_id();') == 1
assert sweet.count('if (!nab_prepare_return()) { return -1; }') == 1

objects = {}
with tempfile.TemporaryDirectory(prefix='nord2-audio-current-') as temp:
    # Check the three-file proposal against the exact copied current sources,
    # never against the active repository checkout.
    patch_root = Path(temp) / 'patch-check'
    for rel, original in (('bsd/dev/sweet_touch.c', CANON/'sweet_touch.c'),
                          ('bsd/conf/files', CANON.parent/'conf/files'),
                          ('tools/nord2-mkpatch.sh', ROOT/'tools/nord2-mkpatch.sh')):
        target = patch_root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, target)
    with (P/'integration-current.patch').open('rb') as patch_file:
        cp = subprocess.run(['patch','-p1','-s'], cwd=patch_root,
                            stdin=patch_file, capture_output=True, text=True)
    assert cp.returncode == 0, cp.stdout + cp.stderr
    for rel in ('bsd/dev/sweet_touch.c','bsd/conf/files','tools/nord2-mkpatch.sh'):
        assert (patch_root/rel).read_bytes() == (P/'proposed'/rel).read_bytes()
    for name in ('sweet_touch', 'audio_id', 'audio_i2c8', 'audio_afe'):
        src = PROPOSED / (name + '.c')
        obj = Path(temp) / (name + '.o')
        extra = ['-I' + str(PROPOSED)]
        if name == 'sweet_touch':
            extra.insert(0, '-I' + str(CANON))
        run([sys.executable, CC, BT, 'bsd', src, obj] + extra)
        objects[name] = run([NM, obj])
assert '_nord2_input_prepare_return' in objects['sweet_touch']
assert ' U _naf_gate' in objects['sweet_touch']
assert ' U _na8_probe' in objects['sweet_touch']
assert ' U _nau_run' in objects['sweet_touch']
assert ' T _naf_gate' in objects['audio_afe']
assert ' T _naf_cycle' in objects['audio_afe']
assert ' T _na8_probe' in objects['audio_i2c8']
assert ' T _nau_run' in objects['audio_id']

host = run([sys.executable, AUDIO / 'tools/test-nord2-audio-id.py'])
result = {
    'status': 'PASS_SOURCE_ONLY_NO_DEVICE',
    'source_commit': '88588a074718e54dd4f1fa63142c304899fff0f4',
    'ci_header_tree': str(BT.relative_to(ROOT)),
    'normal_exported_sweet_header_sha256': sha(BOARD),
    'component_compile': 'bsd DEVELOPMENT ARM64 SWEET, actual source header, no -DNORD2 override',
    'nonempty_whole_tu': True,
    'nonempty_audio_objects': ['audio_id', 'audio_i2c8', 'audio_afe'],
    'host_binding_test': host.strip().splitlines(),
    'afe_streaming_callsite': False,
    'device_access': False,
}
out = P / 'verification'
out.mkdir(exist_ok=True)
(out / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS private current-source audio ID integration; nonempty ARM64 objects')
