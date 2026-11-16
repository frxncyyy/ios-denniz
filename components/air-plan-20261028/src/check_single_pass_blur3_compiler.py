#!/usr/bin/env python3
"""Focused host checks for the exact single_pass_blur_3 compiler entry."""
import argparse
import ctypes as C
import hashlib
from pathlib import Path


class Graphics(C.Structure):
    _fields_ = [('code', C.c_void_p), ('size', C.c_size_t)] + [
        (name, C.c_uint32) for name in
        ('stage', 'registers', 'tls', 'fau_words', 'sysval_mask', 'push_bytes')
    ] + [('preload', C.c_uint64)]


class Slot(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ('location', 'offset', 'components', 'alu_type')]


class Pair(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        'version', 'flags', 'position_offset', 'position_size', 'point_position_offset',
        'point_position_size', 'secondary_offset', 'secondary_size', 'secondary_registers',
        'generic_size_B')] + [('secondary_preload', C.c_uint64)] + [
        (name, C.c_uint32) for name in (
            'vs_attribute_mask', 'vs_varying_mask', 'fs_varying_mask', 'fs_uses_ld_var',
            'noperspective_mask', 'needs_extended_fifo', 'point_size_section',
            'point_size_offset', 'point_size_alu_type', 'varying_count')
    ] + [('varyings', Slot * 4)]


class Ubo(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ('stage', 'descriptor_set', 'binding', 'table', 'index', 'declared_bytes')]


class Plan(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ('version', 'table', 'texture_index', 'sampler_index',
                 'normalized_coordinates', 'sample_count', 'implicit_lod')] + [('ubos', Ubo * 4)]


class ResourcePlan(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ('version', 'table', 'texture_index', 'sampler_index',
                 'normalized_coordinates', 'sample_count')] + [('ubos', Ubo * 4)]


def raw(value):
    return C.string_at(C.addressof(value), C.sizeof(value))


parser = argparse.ArgumentParser()
parser.add_argument('--library', type=Path, required=True)
parser.add_argument('--vertex', type=Path, required=True)
parser.add_argument('--fragment', type=Path, required=True)
args = parser.parse_args()

lib = C.CDLL(str(args.library.resolve()))
exact = lib.tegu_mali_compile_single_pass_blur3_pair
generic = lib.tegu_mali_compile_graphics_pair_resources
signature = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
             C.POINTER(Graphics), C.POINTER(Graphics), C.POINTER(Pair),
             C.c_void_p, C.c_void_p, C.c_size_t]
exact.argtypes = generic.argtypes = signature

vertex_bytes, fragment_bytes = args.vertex.read_bytes(), args.fragment.read_bytes()
vertex = C.create_string_buffer(vertex_bytes)
fragment = C.create_string_buffer(fragment_bytes)
vs, fs, pair, plan = Graphics(), Graphics(), Pair(), Plan()
error = C.create_string_buffer(512)

status = exact(vertex, len(vertex_bytes), fragment, len(fragment_bytes), C.byref(vs), C.byref(fs),
               C.byref(pair), C.byref(plan), error, len(error))
assert status == 0, (status, error.value)
assert (plan.version, plan.table, plan.texture_index, plan.sampler_index,
        plan.normalized_coordinates, plan.sample_count, plan.implicit_lod) == (1, 1, 4, 5, 1, 4, 1)
assert [ubo.declared_bytes for ubo in plan.ubos] == [80, 32, 112, 32]
assert (vs.size, vs.registers, vs.tls, vs.fau_words, vs.sysval_mask, vs.push_bytes,
        vs.preload) == (1152, 32, 0, 34, 3, 8, 0x3800000000000000)
assert (fs.size, fs.registers, fs.tls, fs.fau_words, fs.sysval_mask, fs.push_bytes,
        fs.preload) == (8704, 64, 0, 51, 8, 76, 0x3000000000000000)
assert hashlib.sha256(C.string_at(vs.code, vs.size)).hexdigest() == \
       '40e8dca1a260e5527cb8979161b6e15493d85d0a1b993da207e87c89859a7e52'
assert hashlib.sha256(C.string_at(fs.code, fs.size)).hexdigest() == \
       '7c100704ba60f35a69f469d71e2c59b00b2d7ec7da0488869b4caa0ced8c0fd8'
lib.tegu_mali_graphics_free(C.byref(vs)); lib.tegu_mali_graphics_free(C.byref(fs))

# The generic resource entry deliberately retains its explicit-LOD contract.
pair = Pair()
resource_plan = ResourcePlan()
status = generic(vertex, len(vertex_bytes), fragment, len(fragment_bytes), C.byref(vs), C.byref(fs),
                 C.byref(pair), C.byref(resource_plan), error, len(error))
assert status == 5 and b'Only explicit-LOD texture sample is supported' in error.value
assert not vs.code and not fs.code and pair.version == 0

# Exact input identity is checked before translation; a changed module cannot
# accidentally broaden the accepted shader class.
changed_bytes = bytearray(fragment_bytes); changed_bytes[-1] ^= 1
changed = C.create_string_buffer(bytes(changed_bytes))
sentinel = Plan(); C.memset(C.byref(sentinel), 0xA5, C.sizeof(sentinel)); sentinel.version = 0
before = raw(sentinel)
status = exact(vertex, len(vertex_bytes), changed, len(changed_bytes), C.byref(vs), C.byref(fs),
               C.byref(pair), C.byref(sentinel), error, len(error))
assert status == 5 and b'differ from exact single_pass_blur_3 class' in error.value
assert raw(sentinel) == before and not vs.code and not fs.code and pair.version == 0

# Overlapping outputs are rejected before any caller memory is modified.
C.memset(C.byref(pair), 0x5A, C.sizeof(pair)); before = raw(pair)
status = exact(vertex, len(vertex_bytes), fragment, len(fragment_bytes), C.byref(vs), C.byref(fs),
               C.byref(pair), C.byref(pair), error, len(error))
assert status == 1 and raw(pair) == before

assert C.string_at(vertex, len(vertex_bytes)) == vertex_bytes
assert C.string_at(fragment, len(fragment_bytes)) == fragment_bytes
print('single_pass_blur_3 exact compiler: 1 positive, 3 bounded refusals passed')
