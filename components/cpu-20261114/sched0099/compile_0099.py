#!/usr/bin/env python3
"""ARM64 XNU compile check of patch 0099 with the exact CI flags (jobs 1, nice 19).

osfmk sources use the CI command line recorded for osfmk/kern/sched_clutch.c,
bsd sources the one for bsd/dev/sweet_touch.c (research/ci-40034353275/build.log),
with the CI paths rewritten to the local stage (gpu/build/xnu-stage + xnu-obj).
Our patched copies in 0099/src are searched before the stage. Objects go to
0099/objs. Nothing in the stage, xnu/patches or any checkout is modified.

    compile_0099.py [names...]   default: nord2_sched.c sched_clutch.c sched_prim.c nord2_sched_sysctl.c
"""
import hashlib, json, os, shlex, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[2]                      # .../work/gpu
STAGE = WORK / 'build/xnu-stage'
OBJ = WORK / 'build/xnu-obj/xnu.obj/DEVELOPMENT_ARM64_SWEET'
LOG = Path('$NORD2_REPO/research/ci-40034353275/build.log')
CI = '/Users/runner/work/ios-sweet/ios-sweet/dxb/'
SDK_PLATFORM = '/Applications/Xcode_15.0.1.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX14.0.sdk//System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders/platform'

SOURCES = {
    'nord2_sched.c': ('osfmk', HERE / 'src/osfmk/kern/nord2_sched.c'),
    'sched_clutch.c': ('osfmk', HERE / 'src/osfmk/kern/sched_clutch.c'),
    'sched_prim.c': ('osfmk', HERE / 'src/osfmk/kern/sched_prim.c'),
    'nord2_sched_sysctl.c': ('bsd', HERE / 'src/bsd/dev/nord2_sched_sysctl.c'),
}
ANCHOR = {'osfmk': 'osfmk/kern/sched_clutch.c', 'bsd': 'bsd/dev/sweet_touch.c'}


def template(component):
    anchor = ANCHOR[component]
    with LOG.open(errors='replace') as f:
        for line in f:
            if ' -c ' in line and line.rstrip().endswith(anchor) and 'clang' in line:
                args = shlex.split(line.split('Z ', 1)[1])
                start = next(i for i, a in enumerate(args) if a.endswith('/clang'))
                return args[start:]
    raise SystemExit(f'no CI compile line for {anchor}')


def rewrite(args, component, src, out):
    new = ['clang', '-target', 'arm64-apple-macos14.0']
    skip = False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in ('-o', '-MF'):
            skip = True
            continue
        if a in ('-MD', '-MP') or a.endswith(ANCHOR[component]):
            continue
        if a.startswith('-I') and SDK_PLATFORM in a:
            a = '-I' + str(WORK / 'build/sdk-kernel-platform/platform')
        a = a.replace(CI, str(WORK / 'build') + '/')   # dxb/xnu -> build/xnu (= stage), dxb/build -> build/build (-> xnu-obj), dxb/fakeroot
        new.append(a)
    # our patched headers (sched_prim.h, nord2_sched_policy.h) before the stage's
    inc = ['-I' + str(HERE / 'src' / component)]
    if component == 'osfmk':
        inc.insert(0, '-I' + str(HERE / 'src/osfmk/kern'))
    return new[:1] + inc + new[1:] + ['-Wno-error', '-Wno-missing-include-dirs',
            # Apple-clang-only builtins, as gpu/tools/xnu_staged_compile.py CXX_FLAGS (typed-allocator signatures become empty)
            '-D__builtin_xnu_type_signature(...)=""', '-D__builtin_xnu_type_summary(...)=0', '-D__builtin_xnu_types_compatible(...)=1',
            str(src), '-o', str(out)]


def main():
    names = sys.argv[1:] or list(SOURCES)
    (HERE / 'objs').mkdir(exist_ok=True)
    results = {}
    for name in names:
        component, src = SOURCES[name]
        out = HERE / 'objs' / (src.stem + '.o')
        argv = rewrite(template(component), component, src, out)
        cwd = OBJ / component / 'DEVELOPMENT'
        env = dict(os.environ, SDKROOT='')
        print('+', name, file=sys.stderr)
        r = subprocess.run(['nice', '-n', '19'] + argv, cwd=cwd, env=env, capture_output=True, text=True)
        (HERE / 'objs' / (src.stem + '.log')).write_text(' '.join(shlex.quote(x) for x in argv) + '\n\n' + r.stdout + r.stderr)
        results[name] = {'rc': r.returncode, 'source_sha256': hashlib.sha256(src.read_bytes()).hexdigest(),
                         'object_sha256': hashlib.sha256(out.read_bytes()).hexdigest() if r.returncode == 0 and out.exists() else None,
                         'warnings': r.stderr.count('warning:'), 'errors': r.stderr.count('error:')}
        print(name, results[name]['rc'], 'errors', results[name]['errors'], file=sys.stderr)
        if r.returncode:
            print(r.stderr[-4000:], file=sys.stderr)
    (HERE / 'objs/compile-result.json').write_text(json.dumps(results, indent=1) + '\n')
    return 0 if all(v['rc'] == 0 for v in results.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
