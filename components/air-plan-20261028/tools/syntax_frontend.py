#!/usr/bin/env python3
"""Syntax-only compile of the production ObjC frontend (render harness prelude, as
tests/api_surface_frontend_test.py builds it) against a staged tegu directory placed first
on the include path. usage: syntax_frontend.py STAGE_TEGU_DIR"""
import os, pathlib, subprocess, sys
G = pathlib.Path(__file__).resolve().parents[3]
stage = pathlib.Path(sys.argv[1]).resolve()
HARNESS = G / 'build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py'
text = HARNESS.read_text()
marker = "source += r'''\n/* Linux uint64_t"
ns = {'__file__': str(HARNESS), '__name__': 'api_surface_prelude'}
exec(compile(text[:text.index(marker)], str(HARNESS), 'exec'), ns)
source = ns['source']
helpers = text.split(marker, 1)[1].split('int main(int argc,char **argv){', 1)[0]
source += '/* Linux uint64_t' + helpers + '\n#include <pthread.h>\n#include <unistd.h>\nint main(void){return 0;}\n'
out = stage.parent / 'syntax_frontend.m'
out.write_text(source.replace(str(ns['TEgu']), str(stage)))
cmd = ['nice', '-n', '10', 'clang', '-fsyntax-only', '-fblocks', '-pthread', '-DTEGU_ENABLE_MSAA4', '-DTEGU_HOST_REAL_V2',
       '-Wno-incompatible-pointer-types', '-Wno-incompatible-function-pointer-types', '-fconstant-string-class=NXConstantString',
       '-I' + str(stage), '-I' + str(G / 'metal'), '-I' + str(G / 'include'), '-I' + str(G / 'build/tegu-frontend'), str(out)]
r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(r.stdout.decode()[-4000:], 'rc', r.returncode)
