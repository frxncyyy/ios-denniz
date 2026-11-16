#!/usr/bin/env python3
"""Compile linked VS/FS SPIR-V with the pair2 ABI for explicit native tests.

Inputs must already pass AIR metadata validation before native use. Success
only generates code/metadata; it neither authorizes nor submits a GPU draw.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path


class Graphics(C.Structure):
    _fields_ = [('code', C.c_void_p), ('size', C.c_size_t)] + [(n, C.c_uint32) for n in
        ('stage', 'registers', 'tls', 'fau_words', 'sysval_mask', 'push_bytes')] + [('preload', C.c_uint64)]


class Slot(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('location', 'offset', 'components', 'alu_type')]


class Pair(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'flags', 'position_offset', 'position_size',
        'point_position_offset', 'point_position_size', 'secondary_offset', 'secondary_size',
        'secondary_registers', 'generic_size_B')] + [('secondary_preload', C.c_uint64)] + [
        (n, C.c_uint32) for n in ('vs_attribute_mask', 'vs_varying_mask', 'fs_varying_mask',
        'fs_uses_ld_var', 'noperspective_mask', 'needs_extended_fifo', 'point_size_section',
        'point_size_offset', 'point_size_alu_type', 'varying_count')] + [('varyings', Slot * 4)]


class TexturePlan(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'table', 'texture_index',
                'sampler_index', 'normalized_coordinates', 'descriptor_set', 'binding')]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('library', 'vertex', 'fragment', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--texture', action='store_true',
                        help='Use the additive bounded texture-pair compiler ABI')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    assert C.sizeof(Graphics) == 48 and C.sizeof(Pair) == 152
    lib = C.CDLL(str(args.library.resolve()))
    entry = lib.tegu_mali_compile_graphics_pair_texture if args.texture else lib.tegu_mali_compile_graphics_pair2
    entry.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
        C.POINTER(Graphics), C.POINTER(Graphics), C.POINTER(Pair)] + \
        ([C.POINTER(TexturePlan)] if args.texture else []) + [C.c_void_p, C.c_size_t]
    entry.restype = C.c_int
    lib.tegu_mali_graphics_free.argtypes = [C.POINTER(Graphics)]
    vertex, fragment = args.vertex.read_bytes(), args.fragment.read_bytes()
    assert all(20 <= len(b) <= 1024*1024 and len(b) % 4 == 0 for b in (vertex, fragment))
    vs, fs, pair, plan, error = Graphics(), Graphics(), Pair(), TexturePlan(), C.create_string_buffer(512)
    try:
        status = entry(vertex, len(vertex), fragment, len(fragment),
                       C.byref(vs), C.byref(fs), C.byref(pair),
                       *([C.byref(plan)] if args.texture else []), error, len(error))
        if status:
            raise SystemExit(f'Pair compile refused ({status}): {error.value.decode(errors="replace")}')
        assert not error.value and pair.version == 2 and pair.varying_count <= 4
        result = dict(status=0, gpu_id='0xb8a21030', gpu_executed=False,
            evidence='Linked SPIRV through canonical pair2 compiler; host-only code generation',
            inputs_sha256=dict(vertex=sha(vertex), fragment=sha(fragment)), sha256={})
        if args.texture:
            result['texture_plan'] = {n: getattr(plan, n) for n, _ in TexturePlan._fields_}
            assert result['texture_plan'] == dict(version=1, table=0, texture_index=1,
                sampler_index=0, normalized_coordinates=1, descriptor_set=0, binding=0)
            result['evidence'] = 'Linked SPIRV through bounded texture-pair compiler; host-only code generation'
        for name, stage, code in (('vs', 1, vs), ('fs', 2, fs)):
            assert code.code and code.stage == stage and 0 < code.size <= 4096 and code.size % 8 == 0
            binary = C.string_at(code.code, code.size)
            artifact = 'vs_full' if name == 'vs' else 'fs'
            (args.out / (artifact + '.bin')).write_bytes(binary)
            result[name] = {n: getattr(code, n) for n, _ in Graphics._fields_ if n not in ('code', 'preload')}
            result[name].update(preload=hex(code.preload), sha256=sha(binary))
            result['sha256'][artifact] = sha(binary)
        result['pair'] = {n: getattr(pair, n) for n, _ in Pair._fields_ if n not in ('varyings', 'secondary_preload')}
        result['pair']['secondary_preload'] = hex(pair.secondary_preload)
        result['pair']['varyings'] = [{n: getattr(slot, n) for n, _ in Slot._fields_}
                                     for slot in pair.varyings[:pair.varying_count]]
        (args.out / 'pair_metadata.json').write_text(json.dumps(result, indent=2) + '\n')
        print('Compiled paired VS/FS; GPU_EXECUTED=0')
    finally:
        lib.tegu_mali_graphics_free(C.byref(vs))
        lib.tegu_mali_graphics_free(C.byref(fs))


if __name__ == '__main__':
    main()
