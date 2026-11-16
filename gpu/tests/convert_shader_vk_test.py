#!/usr/bin/env python3
"""A3 surface-conversion shaders on the host GPU (host only, no Mali claim).

Compiles tests/shaders/nord2_preload.vert + nord2_{ycbcr,xr}_convert.frag to
SPIR-V (the exact modules tools/generate_convert.c gives the pinned arch9
compiler) and runs tests/convert_shader_vk_test.c, which draws them on the
host's Vulkan device with the frontend's staging layout and push block and
compares every texel with the production CPU decoders (ycbcr_decode.h, the
XR half tables): all 24 YCbCr formats x full/video x BT.601/709/2020, odd
sizes, extreme codes, and the four XR kinds. Must be bit-exact.

Exit 0 on PASS; prints SKIP and exits 0 when no Vulkan device exists.
Usage: python3 gpu/tests/convert_shader_vk_test.py
"""
import pathlib
import subprocess
import sys

G = pathlib.Path(__file__).resolve().parents[1]
OUT = G / 'build/render-lead-a3-20261028/vk'
OUT.mkdir(parents=True, exist_ok=True)
spv = {}
for name, src in (('vert', 'nord2_preload.vert'), ('ycbcr', 'nord2_ycbcr_convert.frag'), ('xr', 'nord2_xr_convert.frag')):
    spv[name] = OUT / (name + '.spv')
    subprocess.run(['glslangValidator', '-V', str(G / 'tests/shaders' / src), '-o', str(spv[name])],
                   check=True, stdout=subprocess.DEVNULL)
exe = OUT / 'convert_shader_vk_test'
subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                '-I' + str(G / 'metal'), str(G / 'tests/convert_shader_vk_test.c'), '-lvulkan', '-o', str(exe)], check=True)
r = subprocess.run([str(exe), str(spv['vert']), str(spv['ycbcr']), str(spv['xr'])],
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
out = r.stdout.decode('utf-8', 'replace')
sys.stdout.write(out)
sys.stderr.write(r.stderr.decode('utf-8', 'replace')[-4000:])
if r.returncode == 77:
    print('convert_shader_vk_test: SKIP (no Vulkan device; the shader model is unverified on this host)')
    sys.exit(0)
assert r.returncode == 0, r.returncode
assert 'CONVERT_VK ycbcr=243 xr=8' in out and 'mismatches=0' in out, out
print('convert_shader_vk_test: PASS')
