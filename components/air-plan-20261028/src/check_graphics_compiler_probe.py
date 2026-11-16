#!/usr/bin/env python3
"""Run authored render fixtures through host Mali compiler. No GPU claim."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('compiler', 'fixtures', 'work'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=False)
results = []
for fmt in (70, 80):
    for stage in ('vertex', 'fragment'):
        source = a.fixtures / f'{fmt}-{stage}.spv'
        output = a.work / f'{fmt}-{stage}.mali'
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.2', str(source)], check=True)
        with (a.work / f'{fmt}-{stage}.nir.log').open('w') as log:
            r = subprocess.run([str(a.compiler.resolve()), stage, str(source), str(output)],
                               stdout=subprocess.PIPE, stderr=log, text=True, timeout=30)
        (a.work / f'{fmt}-{stage}.result.txt').write_text(r.stdout)
        r.check_returncode()
        match = re.search(r'stage=(\w+) bytes=(\d+) registers=(\d+) tls=(\d+) push=(\d+) preload=([0-9a-f]+) GPU_EXECUTED=0', r.stdout)
        assert match and match[1] == stage, r.stdout
        code = output.read_bytes()
        assert 0 < len(code) == int(match[2]) <= 4096
        assert int(match[3]) > 0 and int(match[4]) == 0
        results.append(dict(format=fmt, stage=stage, bytes=len(code), registers=int(match[3]),
                            tls=int(match[4]), fau_word_count=int(match[5]), preload=match[6],
                            sha256=hashlib.sha256(code).hexdigest()))
(a.work / 'result.json').write_text(json.dumps(dict(gpu_executed=False,
    full_graphics_compiler=False,
    experimental_fau_bytes=dict(viewport_scale=0, viewport_offset=16, shader_output_flags=32,
                                blend_descriptor=48, air_push_base=128), fixtures=results), indent=2) + '\n')
print('PASS authored vertex/fragment host codegen: RGBA8 and BGRA8; GPU_EXECUTED=0')
