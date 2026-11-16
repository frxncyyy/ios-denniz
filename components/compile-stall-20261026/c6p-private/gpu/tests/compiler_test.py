#!/usr/bin/env python3
"""Compile authored shaders through the real pinned arch9 Mesa backend.

Requires gpu/build/mesa-build/src/panfrost/clc/nord2_mali_compile (built by
gpu/tools/build_mesa_arch9.sh).  This is a host test: it proves NIR -> Mali
arch9 code generation/reflection, not GPU execution.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
BUILD = WORK / 'build'
COMPILER = BUILD / 'mesa-build/src/panfrost/clc/nord2_mali_compile'
OUT = BUILD / 'compiler-test'


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kw)


def glslang(src: Path, out: Path):
    subprocess.run(['glslangValidator', '-V', str(src), '-o', str(out)],
                   check=True)


def compile_stage(stage, src, tag):
    out_bin = OUT / f'{tag}.bin'
    out_json = OUT / f'{tag}.json'
    out_dis = OUT / f'{tag}.dis'
    run([str(COMPILER), stage, str(src), str(out_bin), str(out_json),
         str(out_dis)])
    meta = json.loads(out_json.read_text())
    if meta['arch'] != 9 or meta['gpu_id'] != '0x0000000090000000':
        raise SystemExit(f'{tag}: wrong arch metadata {meta}')
    if not out_bin.stat().st_size or out_bin.stat().st_size != meta['binary_size']:
        raise SystemExit(f'{tag}: binary size mismatch')
    if out_bin.stat().st_size > 4096:
        raise SystemExit(f'{tag}: binary too large for the first contract')
    return meta, out_bin, out_dis


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--compiler', type=Path, default=COMPILER)
    args = p.parse_args()
    compiler = args.compiler.resolve()
    if not compiler.exists():
        raise SystemExit(f'missing compiler {compiler}; run build_mesa_arch9.sh')
    OUT.mkdir(parents=True, exist_ok=True)

    # Real SPIR-V from glslang; no hand-written words.
    for name in ('token.comp', 'fullscreen.vert', 'fullscreen.frag'):
        glslang(HERE / 'shaders' / name, OUT / (name + '.spv'))

    token_meta, token_bin, token_dis = compile_stage(
        'compute', OUT / 'token.comp.spv', 'token')
    if not token_meta['writes_global']:
        raise SystemExit('token: expected a global store')
    if 'STORE.i32' not in token_dis.read_text():
        raise SystemExit('token: no STORE in arch9 disassembly')
    if token_meta['fau_words'] < 1:
        raise SystemExit('token: expected an FAU load')

    vs_meta, vs_bin, vs_dis = compile_stage(
        'vertex', OUT / 'fullscreen.vert.spv', 'fullscreen-vert')
    if not vs_meta['idvs'] or not vs_meta['secondary_enable']:
        raise SystemExit(f'vertex: expected IDVS+secondary, got {vs_meta}')
    if vs_bin.stat().st_size == 0:
        raise SystemExit('vertex: empty binary')

    fs_meta, fs_bin, fs_dis = compile_stage(
        'fragment', OUT / 'fullscreen.frag.spv', 'fullscreen-frag')
    if not int(fs_meta['outputs_written'], 16):
        raise SystemExit(f'fragment: no color output {fs_meta}')
    dis = fs_dis.read_text()
    if 'LD_VAR' not in dis or 'BLEND' not in dis:
        raise SystemExit('fragment: missing varying load/blend in arch9 code')

    # The embedded token bytes in the kernel driver must be exactly the bytes
    # produced by this compiler run.
    import re
    text = (WORK / 'src/Nord2GPU.cpp').read_text()
    m = re.search(r'n2_token_code\[\] = \{(.*?)\};', text, re.S)
    if not m:
        raise SystemExit('embedded token array missing')
    embedded = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})',
                                                     m.group(1)))
    if embedded != token_bin.read_bytes():
        raise SystemExit('embedded token bytes differ from compiler output; '
                         'regenerate the array before a hardware probe')

    summary = {
        'mesa_commit': '81daea66703eeea1a232446e3a36902353fc3045',
        'gpu_id': '0x90000000',
        'token_bytes': token_bin.stat().st_size,
        'vertex_bytes': vs_bin.stat().st_size,
        'fragment_bytes': fs_bin.stat().st_size,
        'vertex_idvs': vs_meta['idvs'],
        'vertex_fau_words': vs_meta['fau_words'],
        'fragment_outputs': fs_meta['outputs_written'],
        'token_sha256': __import__('hashlib').sha256(
            token_bin.read_bytes()).hexdigest(),
    }
    (OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    print('arch9 compiler tests passed (software codegen; no GPU execution)')


if __name__ == '__main__':
    main()
