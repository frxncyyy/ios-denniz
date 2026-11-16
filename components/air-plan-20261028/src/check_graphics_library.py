#!/usr/bin/env python3
"""Verify callable compiler ownership/metadata against native-qualified code."""
import argparse
import ctypes as C
from pathlib import Path
import subprocess

class Graphics(C.Structure):
    _fields_ = [('code', C.c_void_p), ('size', C.c_size_t),
                ('stage', C.c_uint32), ('registers', C.c_uint32), ('tls', C.c_uint32),
                ('fau_words', C.c_uint32), ('sysval_mask', C.c_uint32),
                ('push_bytes', C.c_uint32), ('preload', C.c_uint64)]

p = argparse.ArgumentParser(description=__doc__)
for name in ('library', 'fixtures', 'reference-code', 'work'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=False)
lib = C.CDLL(str(a.library.resolve()))
compile_shader = lib.tegu_mali_compile_graphics
compile_shader.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32, C.POINTER(Graphics), C.c_void_p, C.c_size_t]
compile_shader.restype = C.c_int
free = lib.tegu_mali_graphics_free
free.argtypes = [C.POINTER(Graphics)]
error = C.create_string_buffer(256)
for fmt in (70, 80):
    for stage, tag, mask, push in (('vertex', 1, 7, 0), ('fragment', 2, 8, 68)):
        data = (a.fixtures / f'{fmt}-{stage}.spv').read_bytes()
        # Input deliberately unaligned; result remains owned after input mutation.
        source = C.create_string_buffer(b'x' + data)
        result = Graphics()
        assert compile_shader(C.byref(source, 1), len(data), tag, C.byref(result), error, len(error)) == 0, error.value
        original_pointer, size = result.code, result.size
        expected = (a.reference_code / f'{fmt}-{stage}.mali').read_bytes()
        C.memset(source, 0, len(source))
        assert C.string_at(result.code, size) == expected
        assert (result.stage, result.registers, result.tls, result.sysval_mask, result.push_bytes) == (tag, 32, 0, mask, push)
        assert result.fau_words == (9 if tag == 1 else 49)
        assert result.preload == (0x1800000000000000 if tag == 1 else 0x3000000000000000)
        # A nonempty output is refused intact, never leaked/replaced.
        assert compile_shader(data, len(data), tag, C.byref(result), error, len(error)) != 0
        assert error.value and result.code == original_pointer and result.size == size
        free(C.byref(result));free(C.byref(result))
        assert bytes(result) == bytes(C.sizeof(result))
        for payload, length, wrong_stage in ((None, 0, tag), (data, 3, tag), (data, len(data), 99), (b'\0'*20, 20, tag)):
            assert compile_shader(payload, length, wrong_stage, C.byref(result), error, len(error)) != 0
            assert error.value and not result.code and not result.size
free(None)
# A valid SPIR-V module with a push offset beyond the supported span is refused.
source = a.fixtures / '70-fragment.spv'
assembly = subprocess.check_output(['spirv-dis', str(source)], text=True)
assert 'Offset 64' in assembly
assembly = assembly.replace('Offset 64', 'Offset 128')
(a.work / 'outside-push.spvasm').write_text(assembly)
spv = a.work / 'outside-push.spv'
subprocess.run(['spirv-as', '--target-env', 'vulkan1.2', str(a.work / 'outside-push.spvasm'), '-o', str(spv)], check=True)
subprocess.run(['spirv-val', '--target-env', 'vulkan1.2', str(spv)], check=True)
data = spv.read_bytes();result = Graphics()
assert compile_shader(data, len(data), 2, C.byref(result), error, len(error)) != 0
assert b'push access' in error.value and not result.code and not result.size
# Error output of length1 must be NUL terminated without touching a guard byte.
guard = C.create_string_buffer(b'xy', 2)
assert compile_shader(None, 0, 1, C.byref(result), guard, 1) != 0
assert guard.raw == b'\0y'
print('PASS graphics C ABI: native-qualified bytes, owned outputs, metadata, unaligned inputs, bounded push refusal, cleanup')
