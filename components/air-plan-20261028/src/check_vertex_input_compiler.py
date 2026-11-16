#!/usr/bin/env python3
"""Validate real Mali vertex-fetch codegen and sparse slots, without a GPU."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


class Graphics(C.Structure):
    _fields_ = [('code', C.c_void_p), ('size', C.c_size_t)] + [
        (name, C.c_uint32) for name in
        ('stage', 'registers', 'tls', 'fau_words', 'sysval_mask', 'push_bytes')
    ] + [('preload', C.c_uint64)]


class Guarded(C.Structure):
    _fields_ = [('before', C.c_uint64 * 2), ('result', Graphics), ('after', C.c_uint64 * 2)]


def at_location(data, location):
    """Move authored SPIR-V inputs, independently of AIR's dense renumbering."""
    words = list(struct.unpack('<' + 'I' * (len(data) // 4), data))
    instructions = []
    cursor = 5
    while cursor < len(words):
        size, opcode = words[cursor] >> 16, words[cursor] & 0xffff
        assert size and cursor + size <= len(words)
        instructions.append((cursor, size, opcode))
        cursor += size
    inputs = {words[i + 2] for i, size, op in instructions
              if op == 59 and size >= 4 and words[i + 3] == 1}  # OpVariable Input.
    count = 0
    for i, size, op in instructions:
        if op == 71 and size == 4 and words[i + 1] in inputs and words[i + 2] == 30:
            words[i + 3] = location  # OpDecorate Location.
            count += 1
    assert count == 1
    return struct.pack('<' + 'I' * len(words), *words)


p = argparse.ArgumentParser(description=__doc__)
for key in ('library', 'fixture', 'disassembler', 'work'):
    p.add_argument('--' + key, type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=False)
lib = C.CDLL(str(a.library.resolve()))
legacy = lib.tegu_mali_compile_graphics
legacy.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32, C.POINTER(Graphics), C.c_void_p, C.c_size_t]
legacy.restype = C.c_int
compile_fetch = lib.tegu_mali_compile_graphics_with_vertex_inputs
compile_fetch.argtypes = legacy.argtypes[:4] + [C.POINTER(C.c_uint32)] + legacy.argtypes[4:]
compile_fetch.restype = C.c_int
lib.tegu_mali_graphics_free.argtypes = [C.POINTER(Graphics)]
fixture = a.fixture.read_bytes()
results = []
for location in (0, 7, 15, 30, 31):
    data = at_location(fixture, location)
    source = a.work / f'location{location}.spv'
    source.write_bytes(data)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.2', str(source)], check=True)
    guard = Guarded()
    for i in range(2):
        guard.before[i] = guard.after[i] = 0xdeadbeefcafef00d
    mask = C.c_uint32(0xffffffff)
    error = C.create_string_buffer(512)
    assert legacy(data, len(data), 1, C.byref(guard.result), error, len(error)) != 0
    assert error.value and not guard.result.code and not guard.result.size
    status = compile_fetch(data, len(data), 1, C.byref(guard.result), C.byref(mask), error, len(error))
    if location == 31:
        assert status and error.value and mask.value == 0
        assert not guard.result.code and not guard.result.size
    else:
        assert not status and not error.value
        assert mask.value == 1 << location, (location, hex(mask.value))
        r = guard.result
        assert r.stage == 1 and 0 < r.size <= 4096 and r.registers and not r.tls
        binary = a.work / f'location{location}.mali'
        code = C.string_at(r.code, r.size)
        binary.write_bytes(code)
        # The Mesa CLI's optional long argument requires '=', otherwise atoi
        # receives NULL. Keep disassembly independent of our returned mask.
        decoded = subprocess.check_output([str(a.disassembler.resolve()), '--id=47266', 'disasm', str(binary)], text=True)
        (a.work / f'location{location}.disass.txt').write_text(decoded)
        assert 'LD_ATTR' in decoded, decoded[:1000]
        if location < 16:
            assert 'table:0x0' in decoded, decoded[:1000]
            assert re.search(r'LD_ATTR_IMM[^\n]*index:0x' + format(location, 'x') + r'\b', decoded, re.I), decoded[:1000]
        else:
            # The indexed form takes a whole resource handle in a register;
            # the immediate30 has table bits zero and descriptor index30.
            assert re.search(r'#0x' + format(location, 'x') + r'\b', decoded, re.I), decoded[:1000]
        results.append({'location': location, 'mask': mask.value, 'bytes': r.size,
                        'sha256': hashlib.sha256(code).hexdigest()})
        lib.tegu_mali_graphics_free(C.byref(guard.result))
    assert all(guard.before[i] == guard.after[i] == 0xdeadbeefcafef00d for i in range(2))

for data, stage in ((bytes(20), 1), (fixture, 999)):
    result = Graphics()
    mask = C.c_uint32(0xffffffff)
    assert compile_fetch(data, len(data), stage, C.byref(result), C.byref(mask), error, len(error))
    assert not result.code and not result.size and mask.value == 0
report = {'gpu_executed': False, 'vertex_fetch_codegen': True,
          'legacy_abi_refuses_vertex_fetch': True, 'output_guards_passed': True,
          'out_of_range_slot_refused': True, 'fixtures': results}
(a.work / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS real Mali attribute fetch instructions, sparse slots0/7/15/30, bounds and ABI guards; GPU_EXECUTED=0')
