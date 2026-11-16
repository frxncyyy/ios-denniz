#!/usr/bin/env python3
"""Build an SDK-free ARM64 public Metal compositor probe; never executes it."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent

def run(command):
    subprocess.run(command, check=True)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--proof-directory', type=Path, required=True)
    p.add_argument('--metallib', type=Path, required=True, help='Local QuartzCore.metallib used later as runtime input')
    p.add_argument('--work', type=Path, required=True, help='Fresh output directory')
    p.add_argument('--stubs', type=Path, required=True, help='Explicit ARM64 iOS libSystem.tbd link stub')
    p.add_argument('--direct-bundle-device', action='store_true',
                   help='Diagnostic A/B: load /System/Library/Extensions/TeguMetal.bundle directly instead of system device discovery')
    p.add_argument('--direct-dlopen-preflight', action='store_true',
                   help='With --direct-bundle-device, dlopen the bundle executable and durably log its return before NSBundle load')
    p.add_argument('--load-two-draws', action='store_true',
                   help='Build the opt-in owned BGRA16x16 two-draw source-over probe')
    p.add_argument('--iosurface-target', action='store_true',
                   help='Build the opt-in 16x16 BGRA IOSurface selector-17 target probe')
    p.add_argument('--ordinary-iosurface-target', action='store_true',
                   help='Omit the private IOSurfaceTeguGPUShared key and exercise ordinary IOBuffer backing')
    p.add_argument('--eight-tap', action='store_true',
                   help='Qualify the exact 8-tap shader with UI vertex layout and independent constant-source pixel oracle')
    p.add_argument('--present-iosurface', action='store_true',
                   help='Build the optional selector-4/5 held-present readback; requires --iosurface-target')
    p.add_argument('--alpha', type=int, choices=(0,64,128,192,255),
                   help='First source alpha; required with --load-two-draws')
    a = p.parse_args()
    if a.eight_tap and a.load_two_draws:
        p.error('--eight-tap cannot be combined with --load-two-draws')
    if a.direct_dlopen_preflight and not a.direct_bundle_device:
        p.error('--direct-dlopen-preflight requires --direct-bundle-device')
    if a.iosurface_target and a.load_two_draws:
        p.error('--iosurface-target and --load-two-draws are separate qualification modes')
    if a.ordinary_iosurface_target and not a.iosurface_target:
        p.error('--ordinary-iosurface-target requires --iosurface-target')
    if a.present_iosurface and not a.iosurface_target:
        p.error('--present-iosurface requires --iosurface-target')
    if a.alpha is not None and not a.load_two_draws:
        p.error('--alpha requires --load-two-draws')
    if a.load_two_draws and a.alpha is None:
        p.error('--load-two-draws requires --alpha so expected bytes are fixed')
    proof, metallib, work, stubs = (x.resolve() for x in (a.proof_directory, a.metallib, a.work, a.stubs))
    if not metallib.is_file() or not stubs.is_file():
        p.error('metallib and explicit SDK stub file must exist')
    if work.exists():
        p.error(f'work directory must be fresh: {work}')
    facts_dir = work / 'facts'
    facts_dir.mkdir(parents=True)
    prepare = HERE / 'prepare_compositor_public_probe_facts.py'
    result = subprocess.run(['python3', str(prepare), '--proof-directory', str(proof), '--output', str(facts_dir)], check=True, text=True, capture_output=True)
    provenance = json.loads(result.stdout)
    expected_load = None
    if a.load_two_draws:
        expected_load=work/'expected-load-bgra.bin'
        oracle=subprocess.run(['python3',str(HERE/'compositor_public_load_oracle.py'),
            '--alpha',str(a.alpha),'--output',str(expected_load),
            '--header',str(facts_dir/'compositor_load_expected.h')],check=True,text=True,capture_output=True)
        provenance['load_oracle']=json.loads(oracle.stdout)
    raw = metallib.read_bytes()
    library_info = {'local_metallib': str(metallib), 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw),
                    'runtime_entries': ['downsample_blur_vert_lph',
                                        'downsample_8_frag_lph' if a.eight_tap else 'downsample_4_frag_lph']}
    build_info = {'direct_bundle_device': a.direct_bundle_device,
                  'direct_dlopen_preflight': a.direct_dlopen_preflight,
                  'system_device_identity_check': not a.direct_bundle_device,
                  'device_creation_mode': 'direct_bundle' if a.direct_bundle_device else 'system_default',
                  'iosurface_target': a.iosurface_target,
                  'ordinary_iosurface_target': a.ordinary_iosurface_target,
                  'eight_tap': a.eight_tap,
                  'present_iosurface': a.present_iosurface,
                  'load_two_draws':a.load_two_draws,'alpha':a.alpha}
    (work/'provenance.json').write_text(json.dumps({'proof': provenance, 'metallib': library_info,
                                                   'build_options': build_info}, indent=2)+'\n')
    (work/'probe-input.json').write_text(json.dumps(library_info, indent=2)+'\n')
    print(f"PASS supplied metallib: {len(raw)} bytes sha256={library_info['sha256']}")
    source = HERE/'compositor_public_probe.c'
    flags = ['-std=c11', '-fblocks', '-Wall', '-Wextra', '-Werror', '-O2', '-I'+str(facts_dir)]
    if a.direct_bundle_device:
        flags.append('-DTEGU_COMPOSITOR_DIRECT_BUNDLE')
    else:
        flags.append('-DTEGU_COMPOSITOR_VERIFY_SYSTEM_DEVICE_IDENTITY')
    if a.direct_dlopen_preflight:
        flags.append('-DTEGU_COMPOSITOR_DIRECT_DLOPEN_PREFLIGHT')
    if a.load_two_draws:
        flags.append('-DTEGU_COMPOSITOR_LOAD_PROBE')
    if a.iosurface_target:
        flags.append('-DTEGU_COMPOSITOR_IOSURFACE_PROBE')
    if a.ordinary_iosurface_target:
        flags.append('-DTEGU_COMPOSITOR_IOSURFACE_ORDINARY_PROBE')
    if a.eight_tap:
        flags.append('-DTEGU_COMPOSITOR_8TAP_PROBE')
    if a.present_iosurface:
        flags.append('-DTEGU_COMPOSITOR_PRESENT_IOSURFACE_PROBE')
    helper_source = HERE.parent/'present'/'held_present.c'
    syntax_sources = [str(source)] + ([str(helper_source)] if a.present_iosurface else [])
    oracle_source = HERE / 'compositor_8tap_oracle.c'
    if a.eight_tap:
        syntax_sources.append(str(oracle_source))
    run(['clang', *flags, '-fsyntax-only', *syntax_sources])
    print(f'PASS host clang syntax: {source}')
    obj = work/'compositor_public_probe.arm64.o'
    run(['clang', *flags, '--target=arm64-apple-ios16.0', '-ffreestanding', '-fno-stack-protector', '-c', str(source), '-o', str(obj)])
    if not obj.is_file() or not obj.stat().st_size:
        raise SystemExit('ARM64 object missing/empty')
    run(['file', str(obj)])
    link_inputs = [str(obj)]
    if a.eight_tap:
        oracle_obj = work / 'compositor_8tap_oracle.arm64.o'
        run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
             '--target=arm64-apple-ios16.0', '-ffreestanding', '-fno-stack-protector',
             '-c', str(oracle_source), '-o', str(oracle_obj)])
        link_inputs.append(str(oracle_obj))
    if a.present_iosurface:
        helper_obj = work/'held_present.arm64.o'
        run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '--target=arm64-apple-ios16.0',
             '-ffreestanding', '-fno-stack-protector', '-c', str(helper_source), '-o', str(helper_obj)])
        if not helper_obj.is_file() or not helper_obj.stat().st_size:
            raise SystemExit('held-present ARM64 object missing/empty')
        run(['file', str(helper_obj)])
        link_inputs.append(str(helper_obj))
    exe = work/'tegu-compositor-public-probe'
    run(['ld64.lld', '-arch', 'arm64', '-platform_version', 'ios', '16.0', '16.0', '-adhoc_codesign', '-e', '_main', *link_inputs, str(stubs), '-o', str(exe)])
    run(['file', str(exe)])
    print('PASS ARM64 iOS object compile and Mach-O link (not run; no Apple SDK/device execution)')
    launch=[str(metallib)]
    if a.load_two_draws: launch += ['--compositor-load-two-draws',str(a.alpha)]
    if a.iosurface_target: launch += ['--iosurface-target']
    if a.present_iosurface: launch += ['--present-iosurface']
    (work/'launch-arguments.json').write_text(json.dumps(launch,indent=2)+'\n')
    if expected_load is not None:
        print(f"load oracle: {expected_load} sha256={hashlib.sha256(expected_load.read_bytes()).hexdigest()}")
    print(f'phone probe runtime metallib argument: {metallib}')

if __name__ == '__main__':
    main()
