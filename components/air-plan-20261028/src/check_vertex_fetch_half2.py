#!/usr/bin/env python3
"""Check half2 descriptors against pinned Mesa arch9 XML, under ASan/UBSan."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--mesa', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
a = p.parse_args()
source = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / 'half2-test'
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-D_GNU_SOURCE',
                    *(['-DHAVE_ENDIAN_H'] if sys.platform.startswith('linux') else []),
                    '-isystem', str(a.mesa.resolve() / 'include'),
                    '-isystem', str(a.build.resolve()),
                    '-isystem', str(a.mesa.resolve() / 'src'),
                    '-isystem', str(a.build.resolve() / 'src'),
                    '-isystem', str(a.mesa.resolve() / 'src/panfrost'),
                    str(source / 'vertex_fetch.c'), str(source / 'vertex_fetch_half2_host_test.c'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
