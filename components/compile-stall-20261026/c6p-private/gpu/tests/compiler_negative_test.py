#!/usr/bin/env python3
"""Negative compiler tests: malformed input must fail without producing code."""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent / 'build'
CC = BUILD / 'mesa-build/src/panfrost/clc/nord2_mali_compile'
OUT = BUILD / 'compiler-negative'
OUT.mkdir(parents=True, exist_ok=True)

def must_fail(name, args):
    p = subprocess.run([str(CC)] + args, capture_output=True, text=True)
    if p.returncode == 0:
        raise SystemExit(f'{name}: unexpectedly succeeded')
    return p.stderr.strip().splitlines()[0] if p.stderr.strip() else ''

# Bad magic, truncated, empty, unsupported stage.
bad = OUT / 'bad.spv'
bad.write_bytes(b'\x00\x01\x02\x03' * 8)
must_fail('bad magic', ['compute', str(bad), str(OUT / 'x.bin'),
                        str(OUT / 'x.json'), str(OUT / 'x.dis')])
empty = OUT / 'empty.spv'
empty.write_bytes(b'')
must_fail('empty', ['compute', str(empty), str(OUT / 'x.bin'),
                    str(OUT / 'x.json'), str(OUT / 'x.dis')])
try:
    must_fail('bad stage', ['geometry', str(bad), str(OUT / 'x.bin'),
                            str(OUT / 'x.json'), str(OUT / 'x.dis')])
except SystemExit:
    raise
if (OUT / 'x.bin').exists() and (OUT / 'x.bin').stat().st_size:
    raise SystemExit('failure produced code')
print('all negative compiler tests passed')
