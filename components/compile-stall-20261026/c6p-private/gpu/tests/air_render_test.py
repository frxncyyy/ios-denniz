#!/usr/bin/env python3
"""End-to-end host oracle: authored AIR metadata -> SPIR-V -> Mali arch9.

The pinned metal2vulkan translator takes Apple AIR.  Its public validation
fixtures include a no-resource vertex function and a constant-color fragment.
LLVM's textual IR syntax moved after LLVM 15, so this test assembles copies
with a caller-supplied llvm-as/llvm-link (15.x works) after removing only the
newer spelling that does not affect semantics.  The translator profile selects
the Vulkan feature ceiling for translation; the Mali compiler still targets
our fixed G77 arch9 ID.  Nothing here executes on a GPU.

Usage:
  python3 gpu/tests/air_render_test.py \
    --translator gpu/build/cargo-target/release/metal2vulkan \
    --llvm-bin /path/to/llvm-15/bin \
    --profile /path/to/metal2vulkan/.../nvidia-rtx5080-laptop.json
"""
import argparse
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
BUILD = WORK / 'build'
OUT = BUILD / 'air-test'
COMPILER = BUILD / 'mesa-build/src/panfrost/clc/nord2_mali_compile'
REF = (WORK.parent.parent / 'references' / 'metal2vulkan-tegu-pin' /
       'crates/metal2vulkan-validation')
FIXTURES = REF / 'fixtures/public'
PROFILE = REF / 'profiles/nvidia-rtx5080-laptop.json'


def run(cmd):
    return subprocess.run(cmd, check=True, text=True, capture_output=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--translator', type=Path, required=True)
    p.add_argument('--llvm-bin', type=Path, required=True)
    p.add_argument('--profile', type=Path, default=PROFILE)
    p.add_argument('--compiler', type=Path, default=COMPILER)
    p.add_argument('--fix-references', type=Path, default=FIXTURES)
    args = p.parse_args()

    for path, why in ((args.translator, 'translator'),
                      (args.llvm_bin / 'llvm-as', 'llvm-as'),
                      (args.llvm_bin / 'llvm-link', 'llvm-link'),
                      (args.profile, 'profile'),
                      (args.compiler, 'arch9 compiler')):
        if not path.exists():
            raise SystemExit(f'missing {why}: {path}')

    OUT.mkdir(parents=True, exist_ok=True)
    # Runtime copies, never committed: public fixtures remain read-only.
    vertex = (args.fix_references / 'vertex_amplification_id_layer.ll').read_text()
    fragment = (args.fix_references / 'fragment_constant_color.ll').read_text()
    vertex = (vertex.replace(' captures(none)', '')
                    .replace(' memory(none)', '')
                    .replace('splat (float 2.000000e+00)',
                             '<float 2.000000e+00, float 2.000000e+00>')
                    .replace('splat (float -1.000000e+00)',
                             '<float -1.000000e+00, float -1.000000e+00>'))
    (OUT / 'vertex.ll').write_text(vertex)
    (OUT / 'fragment.ll').write_text(fragment)
    (OUT / 'vertex.bc').unlink(missing_ok=True)
    (OUT / 'fragment.bc').unlink(missing_ok=True)
    (OUT / 'combined.bc').unlink(missing_ok=True)
    run([str(args.llvm_bin / 'llvm-as'), str(OUT / 'vertex.ll'),
         '-o', str(OUT / 'vertex.bc')])
    run([str(args.llvm_bin / 'llvm-as'), str(OUT / 'fragment.ll'),
         '-o', str(OUT / 'fragment.bc')])
    run([str(args.llvm_bin / 'llvm-link'), str(OUT / 'vertex.bc'),
         str(OUT / 'fragment.bc'), '-o', str(OUT / 'combined.bc')])

    tr = run([str(args.translator), str(OUT / 'combined.bc'),
              str(OUT / 'render.spv'), '--profile', str(args.profile),
              '--function', 'vertex_amplification_id_layer',
              '--fragment', 'fragment_constant_color',
              '--color-format', '0:RGBA8Unorm',
              '--vertex-amplification-count', '2'])
    if 'PASS' not in tr.stdout:
        raise SystemExit(f'translator did not report PASS: {tr.stdout!r}')

    result = {'translator_commit': '720ef1ce9ae2a2068999a8675717418b9416283e',
              'stages': {}}
    for stage, suffix, want_idvs, want_output in (
            ('vertex', 'vert', True, True),
            ('fragment', 'frag', False, True)):
        spv = OUT / f'render.{suffix}.spv'
        if not spv.exists() or spv.stat().st_size < 20:
            raise SystemExit(f'{stage}: translator produced no SPIR-V')
        if struct.unpack('<I', spv.read_bytes()[:4])[0] != 0x07230203:
            raise SystemExit(f'{stage}: output is not SPIR-V')
        code = OUT / f'render.{suffix}.bin'
        meta_path = OUT / f'render.{suffix}.json'
        dis = OUT / f'render.{suffix}.dis'
        run([str(args.compiler), stage, str(spv), str(code), str(meta_path),
             str(dis)])
        meta = json.loads(meta_path.read_text())
        if meta['arch'] != 9 or meta['gpu_id'] != '0x0000000090000000':
            raise SystemExit(f'{stage}: wrong arch metadata: {meta}')
        if not code.exists() or code.stat().st_size != meta['binary_size']:
            raise SystemExit(f'{stage}: arch9 binary size mismatch')
        if bool(meta['idvs']) != want_idvs:
            raise SystemExit(f'{stage}: unexpected IDVS flag: {meta}')
        if want_output and not int(meta['outputs_written'], 16):
            raise SystemExit(f'{stage}: no outputs reflected: {meta}')
        result['stages'][stage] = {
            'spirv_bytes': spv.stat().st_size,
            'mali_bytes': code.stat().st_size,
            'fau_words': meta['fau_words'],
            'idvs': meta['idvs'],
            'outputs_written': meta['outputs_written'],
        }
    print(json.dumps(result, indent=2))
    print('AIR -> SPIR-V -> arch9 render pipeline PASS (host; no GPU execution)')


if __name__ == '__main__':
    main()
