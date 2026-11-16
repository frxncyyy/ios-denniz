#!/usr/bin/env python3
"""Build and check the bounded native AIR texture gate on a host.

All build products and generated fixtures go under the required fresh --work
directory beneath research/. This checks metadata/ABI behavior only; it makes
no GPU or device claim.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def run(command, *, env=None, cwd=None):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(command, check=True, cwd=cwd, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translator', type=Path, required=True,
                        help='pinned metal2vulkan checkout at 720ef1ce9ae2a2068999a8675717418b9416283e')
    parser.add_argument('--work', type=Path, required=True,
                        help='fresh output directory beneath this repository research/')
    parser.add_argument('--baseline', type=Path,
                        help='pre-texture host bridge .so (defaults to research/tegu-gpu/air-bridge-blend/target/release)')
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[3]
    work = args.work.resolve()
    research = (repo / 'research').resolve()
    if not work.is_relative_to(research) or work == research:
        parser.error('--work must be a child directory of this repository research/')
    if work.exists():
        parser.error('--work must not exist; choose a fresh directory so outputs stay isolated')
    translator = args.translator.resolve()
    baseline = (args.baseline.resolve() if args.baseline else
                repo / 'research/tegu-gpu/air-bridge-blend/target/release/libtegu_air_bridge.so')
    if not baseline.is_file():
        parser.error(f'baseline bridge not found: {baseline}; pass --baseline')
    work.mkdir(parents=True)

    env = dict(os.environ)
    env['CARGO_NET_OFFLINE'] = 'true'
    metal = repo / 'tools/tegu/metal'
    tests = metal / 'texture_gate_tests'
    profile = translator / 'crates/metal2vulkan-validation/profiles/vk12-bare.json'
    canonical = repo / 'research/tegu-gpu/texture-air-canonical-r1'

    run([sys.executable, metal / 'build_air_bridge.py', '--translator', translator,
         '--work', work], env=env)
    crate = work / 'Cargo.toml'
    run(['cargo', 'test', '--release', '--offline', '--manifest-path', crate, '--lib'], env=env)
    bridge = work / 'target/release/libtegu_air_bridge.so'
    if not bridge.is_file():
        raise SystemExit(f'expected host bridge was not built: {bridge}')

    run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
         f'-I{metal}', '-x', 'c', metal / 'air_bridge.h'])
    fixtures = work / 'fixtures'
    run([sys.executable, '-I', tests / 'make_fixtures.py', '--repo', repo, '--out', fixtures])
    results = work / 'results'
    results.mkdir()
    for label, library in (('baseline', baseline), ('texture', bridge)):
        run([sys.executable, '-I', tests / 'dump_old_abi.py', '--bridge', library,
             '--fixtures', fixtures, '--profile', profile, '--out', results / f'old-{label}'])
    run(['diff', '-r', results / 'old-baseline', results / 'old-texture'])
    print('PASS: existing C ABI outputs are byte-identical', flush=True)

    run([sys.executable, '-I', tests / 'test_texture_gate.py', '--bridge', bridge,
         '--fixtures', fixtures, '--profile', profile, '--canonical', canonical,
         '--out', results / 'texture-gate'])
    exports = {}
    for label, library in (('baseline', baseline), ('texture', bridge)):
        output = subprocess.check_output(['nm', '-D', '--defined-only', library], text=True)
        exports[label] = sorted(line.split()[-1] for line in output.splitlines()
                                if ' T tegu_' in line)
    expected = sorted(exports['baseline'] + ['tegu_air_render_pipeline_native_check_with_texture'])
    if exports['texture'] != expected:
        raise SystemExit('export set changed beyond the one additive texture gate')
    print('PASS: export set adds only tegu_air_render_pipeline_native_check_with_texture', flush=True)
    print(f'ALL PASS; outputs: {work}', flush=True)


if __name__ == '__main__':
    main()
