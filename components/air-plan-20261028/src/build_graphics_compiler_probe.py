#!/usr/bin/env python3
"""Build host-only graphics codegen experiment in an existing pinned Mesa build.

Run build_mali_compiler.py first. No CI, phone or iOS compiler rebuild is used.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from build_mali_compiler import PIN

p = argparse.ArgumentParser(description=__doc__)
for name in ('source', 'build', 'environment'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
source, build = a.source.resolve(), a.build.resolve()
if subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() != PIN:
    p.error('Require pinned Mesa revision ' + PIN)
if not (build / 'build.ninja').is_file():
    p.error('Run build_mali_compiler.py first')
for name, dest in [('graphics_compiler_probe.c', 'tegu_graphics_probe.c'),
                   ('mali_graphics_compiler.c', 'mali_graphics_compiler.c'),
                   ('mali_graphics_compiler.h', 'mali_graphics_compiler.h'),
                   ('mali_graphics_resources.inc', 'mali_graphics_resources.inc'),
                   ('compiler_trace_sha256.h', 'compiler_trace_sha256.h')]:
    shutil.copyfile(Path(__file__).with_name(name), source / 'src/panfrost/clc' / dest)
config = source / 'src/panfrost/clc/meson.build'
marker = '# TEGU_GRAPHICS_PROBE_BEGIN'
base = config.read_text().split(marker)[0].rstrip()
config.write_text(base + '\n\n' + marker + """
tegu_graphics_lib = shared_library('tegu_mali_graphics', ['mali_graphics_compiler.c'],
 link_with : [libpanfrost_compiler],
 include_directories : [inc_include, inc_src],
 dependencies : [idep_vtn, idep_nir, idep_mesautil, libpanfrost_model_dep],
 build_by_default : false)
executable('tegu_graphics_probe', ['tegu_graphics_probe.c'],
 link_with : [tegu_graphics_lib], build_by_default : false)
""")
env = dict(os.environ, PATH=str(a.environment.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
subprocess.run(['ninja', '-C', str(build), '-j', '4', 'src/panfrost/clc/tegu_graphics_probe'],
               check=True, env=env)
