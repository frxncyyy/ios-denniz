#!/usr/bin/env python3
"""Build a PRIVATE libnord2_mali_runtime.so from candidate gpu/metal/tegu sources.

Uses the pinned mesa-build compile/link commands verbatim, replacing only the
source path and output paths. Single job, two TUs + link; does not touch the
shared mesa-build tree outputs."""
import json, shlex, subprocess, sys, hashlib
from pathlib import Path
work = Path(__file__).resolve().parents[3]
build = work/'gpu/build/mesa-build'; tegu = work/'gpu/metal/tegu'
out = Path(sys.argv[1]).resolve(); out.mkdir(parents=True, exist_ok=True)
# Optional argv[2]: a proposed pipeline_format.h; sources are then compiled
# from a private copy so the shared candidate header is never modified.
if len(sys.argv) > 2:
    import shutil
    src = out/'src'; src.mkdir(exist_ok=True)
    for n in ('mali_compute_compiler.c', 'mali_compute_compiler.h', 'mali_graphics_compiler.c',
              'mali_graphics_compiler.h', 'mali_graphics_resources.inc'):
        shutil.copy(tegu/n, src/n)
    shutil.copy(sys.argv[2], src/'pipeline_format.h'); tegu = src
cmds = json.loads((build/'compile_commands.json').read_text())
objs = []
for name in ('mali_compute_compiler.c', 'mali_graphics_compiler.c'):
    e = next(c for c in cmds if c['file'].endswith('/clc/'+name))
    args = shlex.split(e['command']); res = []; i = 0
    while i < len(args):
        a = args[i]
        if a in ('-o', '-MF', '-MQ'): i += 2; continue
        if a == '-MD': i += 1; continue
        if a == '-c': res += ['-c', str(tegu/name)]; i += 2; continue
        res.append(a); i += 1
    o = out/(name+'.o'); res += ['-o', str(o)]
    subprocess.run(res, cwd=build, check=True); objs.append(o)
link = shlex.split(subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands',
       'src/panfrost/clc/libnord2_mali_runtime.so'], text=True).splitlines()[-1])
res = []; i = 0; lib = out/'libnord2_mali_runtime.so'
while i < len(link):
    a = link[i]
    if a == '-o': res += ['-o', str(lib)]; i += 2; continue
    if a.endswith('mali_compute_compiler.c.o'): res.append(str(objs[0])); i += 1; continue
    if a.endswith('mali_graphics_compiler.c.o'): res.append(str(objs[1])); i += 1; continue
    res.append(a); i += 1
subprocess.run(res, cwd=build, check=True)
print(lib, hashlib.sha256(lib.read_bytes()).hexdigest())
