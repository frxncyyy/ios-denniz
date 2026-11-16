#!/usr/bin/env python3
"""Run the native vertex descriptor builder against Mesa's XML packing oracle."""
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
    binary = Path(tmp) / 'vertex-fetch-test'
    subprocess.run(['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-D_GNU_SOURCE',
                    *(['-DHAVE_ENDIAN_H'] if sys.platform.startswith('linux') else []),
                    '-isystem', str(a.mesa.resolve() / 'include'),
                    '-isystem', str(a.build.resolve()),
                    '-isystem', str(a.mesa.resolve() / 'src'),
                    '-isystem', str(a.build.resolve() / 'src'),
                    '-isystem', str(a.mesa.resolve() / 'src/panfrost'),
                    str(source / 'vertex_fetch.c'), str(source / 'vertex_fetch_host_test.c'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
