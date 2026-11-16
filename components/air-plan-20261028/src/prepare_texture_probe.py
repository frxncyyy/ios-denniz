#!/usr/bin/env python3
"""Package actual compiled texture pair and C-built descriptors for GPU tests.

No shader replacement, GPU submission or native resource gate change. The
actual reflected vertex-fetch and push plans must accompany the shader pair.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


class Input(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'format', 'width', 'height',
        'levels', 'layers', 'row_stride', 'min_filter', 'mag_filter', 'mip_filter',
        'address_r', 'address_s', 'address_t', 'normalized', 'compare',
        'max_anisotropy', 'lod_min_q8', 'lod_max_q8', 'options', 'reserved')] + \
        [(n, C.c_uint64) for n in ('descriptor_address', 'texel_address', 'texel_length')]


class State(C.Structure):
    _fields_ = [('resource_table', C.c_uint64), ('descriptors', C.c_ubyte*4096)]


def main():
    source = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('pair', 'fetch', 'push', 'out'):
        p.add_argument('--'+key, type=Path, required=True)
    p.add_argument('--texture-source', type=Path, default=source/'texture_state.c',
                   help='Reviewed descriptor packer source (may be an isolated prototype)')
    a = p.parse_args()
    metadata = json.loads((a.pair/'pair_metadata.json').read_text())
    plan = metadata.get('texture_plan')
    if plan != dict(version=1, table=0, texture_index=1, sampler_index=0,
                    normalized_coordinates=1, descriptor_set=0, binding=0):
        p.error('paired compiler did not report the bounded normalized texture contract')
    subprocess.run([sys.executable, str(source/'prepare_varying_probe.py'),
                    '--pair', str(a.pair), '--fetch', str(a.fetch), '--push', str(a.push),
                    '--out', str(a.out), '--oracle', 'constant'], check=True)
    manifest = json.loads((a.out/'result.json').read_text())
    previous = (a.out/'vertices.bin').read_bytes()
    uv = ((.125, .125, 0., 1.), (.875, .125, 0., 1.), (.125, .875, 0., 1.))
    vertices = b''.join(previous[i*32:i*32+16]+struct.pack('<4f', *v)
                        for i, v in enumerate(uv))
    pixels = b''.join(struct.pack('<I', 0xff000000 | (17*x) | ((17*y) << 8) |
                                 ((17*(x ^ y)) << 16)) for y in range(16) for x in range(16))
    request, state = Input(), State()
    for name, value in dict(version=1, format=70, width=16, height=16, levels=1,
            layers=1, row_stride=64, normalized=1, max_anisotropy=1,
            descriptor_address=0x5053000, texel_address=0x5054000, texel_length=1024).items():
        setattr(request, name, value)
    with tempfile.TemporaryDirectory() as tmp:
        libpath = Path(tmp)/'texture.so'
        subprocess.run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', str(a.texture_source), '-o', str(libpath)], check=True)
        builder = C.CDLL(str(libpath)).tegu_texture_state_build
        builder.argtypes = [C.POINTER(Input), C.POINTER(State)]
        builder.restype = C.c_int
        assert builder(C.byref(request), C.byref(state)) == 0
    assert state.resource_table == 0x5053004
    for name, raw, key in [('vertices.bin', vertices, 'vertices_sha256'),
            ('fragment-descriptors.bin', bytes(state.descriptors), 'texture_descriptors_sha256'),
            ('texels.bin', pixels, 'texture_pixels_sha256')]:
        (a.out/name).write_bytes(raw)
        manifest[key] = hashlib.sha256(raw).hexdigest()
    manifest.update(oracle='texture-xy', texture_plan=plan, gpu_executed=False)
    (a.out/'result.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Packaged actual compiled normalized texture candidate; GPU_EXECUTED=0')


if __name__ == '__main__':
    main()
