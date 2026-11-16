#!/usr/bin/env python3
"""Bind authored AIR fetch shaders and native descriptors for a standalone test.

No phone operations. This qualifies one strided float4 stream, not a Metal
driver. The driver integration must consume all pipeline metadata separately.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

class Graphics(C.Structure):
    _fields_ = [('code', C.c_void_p), ('size', C.c_size_t)] + [
        (name, C.c_uint32) for name in
        ('stage', 'registers', 'tls', 'fau_words', 'sysval_mask', 'push_bytes')
    ] + [('preload', C.c_uint64)]

class Attribute(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ('location', 'buffer_index', 'format', 'offset', 'stride', 'input_rate', 'divisor')]

class Buffer(C.Structure):
    _fields_ = [('address', C.c_uint64), ('length', C.c_uint64)]

class Draw(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ('first_vertex', 'vertex_count', 'instance_count', 'base_instance')]

class State(C.Structure):
    _fields_ = [('resource_table', C.c_uint64), ('buffer_mask', C.c_uint32),
                ('reserved', C.c_uint32), ('descriptors', C.c_ubyte * 4096)]

def sha(data):
    return hashlib.sha256(data).hexdigest()

p = argparse.ArgumentParser(description=__doc__)
for key in ('graphics', 'render', 'out'):
    p.add_argument('--' + key, type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=False)
fetch = json.loads((a.render / '70-vertex-fetch.json').read_text())
expected = dict(location=0, buffer_index=1, format=109, offset=16, stride=32, input_rate=0, divisor=1)
if fetch != dict(version=1, mask=1, count=1, attributes=[expected]):
    p.error('Standalone probe requires the qualified strided float4 stream')
lib = C.CDLL(str(a.graphics.resolve()))
compile = lib.tegu_mali_compile_graphics_with_vertex_inputs
compile.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32, C.POINTER(Graphics),
                    C.POINTER(C.c_uint32), C.c_void_p, C.c_size_t]
compile.restype = C.c_int
lib.tegu_mali_graphics_free.argtypes = [C.POINTER(Graphics)]
rows = []
for stage, number in (('vertex', 1), ('fragment', 2)):
    data = (a.render / f'70-{stage}.spv').read_bytes()
    result, mask, error = Graphics(), C.c_uint32(), C.create_string_buffer(512)
    assert not compile(data, len(data), number, C.byref(result), C.byref(mask), error, len(error)), error.value
    assert mask.value == (fetch['mask'] if number == 1 else 0)
    code = C.string_at(result.code, result.size)
    rows.append(dict(format=70, stage=stage, bytes=result.size, registers=result.registers,
                     tls=result.tls, fau_word_count=result.fau_words, preload=format(result.preload, 'x'),
                     sysval_mask=result.sysval_mask, air_push_bytes=result.push_bytes, sha256=sha(code)))
    (a.out / f'70-{stage}.mali').write_bytes(code)
    (a.out / f'70-{stage}.spv').write_bytes(data)
    lib.tegu_mali_graphics_free(C.byref(result))

# Add two NX pages after the qualified diagnostic arena's existing80pages.
descriptor_address, buffer_address = 0x05050000, 0x05051000
vertices = bytearray()
for position in ((-.75, -.75, .5, 1.), (.75, -.75, .5, 1.), (-.75, .75, .5, 1.)):
    vertices += struct.pack('<4f', 3., 3., .5, 1.)  # Wrong offset/stride must fail the pixel test.
    vertices += struct.pack('<4f', *position)
attributes = (Attribute * 1)(Attribute(**expected))
buffers = (Buffer * 31)()
buffers[1] = Buffer(buffer_address, len(vertices))
draw = Draw(0, 3, 1, 0)
state = State()
source = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'fetch.so'
    subprocess.run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    str(source / 'vertex_fetch.c'), '-o', str(path)], check=True)
    builder = C.CDLL(str(path)).tegu_vertex_fetch_build
    builder.argtypes = [C.c_uint32, C.POINTER(Attribute), C.c_size_t, C.POINTER(Buffer),
                        C.POINTER(Draw), C.c_uint64, C.POINTER(State)]
    builder.restype = C.c_int
    assert not builder(fetch['mask'], attributes, 1, buffers, C.byref(draw), descriptor_address, C.byref(state))
assert state.resource_table == descriptor_address | 4 and state.buffer_mask == 2
descriptors = bytes(state.descriptors)
(a.out / 'vertex-descriptors.bin').write_bytes(descriptors)
(a.out / 'vertices.bin').write_bytes(vertices)
report = dict(gpu_executed=False, fixtures=rows, vertex_fetch=dict(
    plan=fetch, resource_table=state.resource_table, descriptor_address=descriptor_address,
    buffer_address=buffer_address, buffer_bytes=len(vertices),
    descriptors_sha256=sha(descriptors), vertices_sha256=sha(vertices)),
    scope='Authored AIR and native strided vertex descriptor candidate; hardware validation pending')
(a.out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
