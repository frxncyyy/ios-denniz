#!/usr/bin/env python3
"""Compile a new XNU source with the exact cached Nord 2 CI flags.

The audio native build recorded the complete C command line used by CI.  This
script reuses it against a mutable copy of the XNU tree/build headers under
gpu/build/, optionally with C++ flags for IOKit sources.

    tools/xnu_staged_compile.py SOURCE [--cxx] [--out OBJECT] [--extra FLAG ...]

No system settings, device, or read-only checkout is modified.
"""
import argparse
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

WORK = Path(__file__).resolve().parent.parent
STAGE = WORK / 'build/xnu-stage'
OBJ = WORK / 'build/xnu-obj'
OLD_ROOT = Path('$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/'
                'work/ios-sweet/research/nord2/audio/playback-20261023/'
                'install-sol/private-ci39969463181')
RESULTS = Path('$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/'
               'work/ios-sweet/research/nord2/audio/playback-20261023/'
               'ios-calibrated/native/results.json')

CXX_FLAGS = [
    '-target','arm64-apple-macos14.0','-std=gnu++1z', '-fapple-kext', '-fno-c++-static-destructors',
    '-DIOASSERT=1',
    '-D__builtin_xnu_type_signature(...)=""',
    '-D__builtin_xnu_type_summary(...)=0',
    '-D__builtin_xnu_types_compatible(...)=1',
]


def template(cxx=False):
    if cxx:
        log=RESULTS.parents[5]/"ci-40034353275/build.log"
        for line in log.read_text().splitlines():
            if "clang" in line and "SweetSurface.cpp" in line and " -c " in line:
                args=shlex.split(line);start=next(i for i,x in enumerate(args) if x.endswith("/clang") or x.endswith("/clang++"))
                return args[start:]
        raise RuntimeError("exact baseline IOKit compile command not found")

    data = json.loads(RESULTS.read_text())
    for entry in data:
        if entry.get('name') == 'audio_i2c8':
            return entry['argv']
    return data[0]['argv']


def rewrite(arg):
    text = str(arg)
    text=text.replace("/Users/runner/work/ios-sweet/ios-sweet/dxb/",str(STAGE.parent)+"/")
    if "Kernel.framework/Versions/A/PrivateHeaders/platform" in text:
        return "-I"+str(STAGE.parent/"sdk-kernel-platform/platform")
    if text.startswith(str(OLD_ROOT)):
        text = str(STAGE.parent) + text[len(str(OLD_ROOT)):]
    if text.startswith('-I' + str(OLD_ROOT)):
        text = '-I' + str(STAGE.parent) + text[len('-I' + str(OLD_ROOT)):]
    return text


def main():
    p = argparse.ArgumentParser()
    p.add_argument('source', type=Path)
    p.add_argument('--cxx', action='store_true')
    p.add_argument('--out', type=Path)
    p.add_argument('--extra', action='append', default=[])
    a = p.parse_args()
    src = a.source.resolve()
    if not src.exists():
        p.error(f'missing source {src}')
    out = a.out or (WORK / 'build/objs' / (src.stem + '.o'))
    out = out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)

    argv = [rewrite(x) for x in template(a.cxx)]
    compiler = 'clang++' if a.cxx else 'clang'
    argv[0] = compiler
    # Replace the template source and output.
    new = []
    skip = False
    for arg in argv:
        if skip:
            skip = False
            continue
        if arg == '-o':
            skip = True
            continue
        if arg.endswith('audio_i2c8.c') or arg.endswith('SweetSurface.cpp'):
            continue
        new.append(arg)
    new += ['-I' + str(WORK / 'include'), '-DNORD2=1',
]
    if a.cxx:
        new = [x for x in new if x != '-DIOKITCPP' and
               not (isinstance(x, str) and x.startswith('-I') and
                    x.endswith('/xnu/bsd'))]
        new += CXX_FLAGS
    new += a.extra
    new += [str(src), '-o', str(out)]
    env = dict(os.environ)
    env.setdefault('SDKROOT', '')
    print('+', ' '.join(shlex.quote(x) for x in new), file=sys.stderr)
    result = subprocess.run(new, cwd=OBJ / 'xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT'
                            if not a.cxx else OBJ / 'xnu.obj/DEVELOPMENT_ARM64_SWEET/iokit/DEVELOPMENT',
                            env=env)
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
