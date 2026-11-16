#!/usr/bin/env python3
"""Check that pair compilation links the live subset of reordered varyings."""
import argparse
import ctypes as C
from pathlib import Path
import subprocess


class Graphics(C.Structure):
    _fields_ = [('code', C.c_void_p), ('size', C.c_size_t)] + [
        (name, C.c_uint32) for name in
        ('stage', 'registers', 'tls', 'fau_words', 'sysval_mask', 'push_bytes')
    ] + [('preload', C.c_uint64)]


class Slot(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ('location', 'offset', 'components', 'alu_type')]


class Pair2(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ('version', 'flags', 'position_offset', 'position_size',
                 'point_position_offset', 'point_position_size',
                 'secondary_offset', 'secondary_size', 'secondary_registers',
                 'generic_size_B')] + [('secondary_preload', C.c_uint64)] + [
        (name, C.c_uint32) for name in
        ('vs_attribute_mask', 'vs_varying_mask', 'fs_varying_mask',
         'fs_uses_ld_var', 'noperspective_mask', 'needs_extended_fifo',
         'point_size_section', 'point_size_offset', 'point_size_alu_type',
         'varying_count')
    ] + [('varyings', Slot * 4)]


VERTEX = r'''#version 450
layout(location=0) in vec2 position;
layout(location=0) out vec2 zz;
layout(location=1) out vec3 unused_middle;
layout(location=2) out vec2 aa;
void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    zz = position;
    unused_middle = vec3(position, 7.0);
    aa = position.yx;
}
'''

FRAGMENT = r'''#version 450
layout(location=2) in vec2 aa;
layout(location=0) in vec2 zz;
layout(location=0) out vec4 color;
void main() { color = vec4(zz, zz); }
'''


def compile_glsl(work: Path, stage: str, text: str) -> bytes:
    source = work / ('subset.' + stage)
    output = work / ('subset.' + stage + '.spv')
    source.write_text(text)
    subprocess.run(['glslangValidator', '-V', '--target-env', 'vulkan1.2',
                    '-S', stage, str(source), '-o', str(output)],
                   check=True, capture_output=True)
    subprocess.run(['spirv-val', '--target-env', 'vulkan1.2', str(output)],
                   check=True, capture_output=True)
    return output.read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=False)

    vertex = compile_glsl(args.work, 'vert', VERTEX)
    fragment = compile_glsl(args.work, 'frag', FRAGMENT)
    vb, fb = C.create_string_buffer(vertex), C.create_string_buffer(fragment)
    vs, fs, pair = Graphics(), Graphics(), Pair2()
    error = C.create_string_buffer(512)
    lib = C.CDLL(str(args.library.resolve()))
    compile_pair = lib.tegu_mali_compile_graphics_pair2
    compile_pair.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
                             C.POINTER(Graphics), C.POINTER(Graphics),
                             C.POINTER(Pair2), C.c_void_p, C.c_size_t]
    compile_pair.restype = C.c_int
    lib.tegu_mali_graphics_free.argtypes = [C.POINTER(Graphics)]

    status = compile_pair(vb, len(vertex), fb, len(fragment), C.byref(vs),
                          C.byref(fs), C.byref(pair), error, len(error))
    if status:
        raise AssertionError((status, error.value.decode(errors='replace')))
    # Locations 1 and 2 are authored and written by the vertex shader, and the
    # reordered fragment interface declares location 2, but only location 0 is
    # live after specialization/optimization.
    assert pair.version == 2
    assert pair.vs_varying_mask == pair.fs_varying_mask == 0b001
    assert pair.varying_count == 1
    assert pair.varyings[0].location == 0
    assert pair.varyings[0].components == 2
    assert pair.generic_size_B == 8
    assert vs.code and fs.code and not error.value
    lib.tegu_mali_graphics_free(C.byref(vs))
    lib.tegu_mali_graphics_free(C.byref(fs))
    print('PASS live subset location 0 linked; unused reordered locations 1/2 pruned')


if __name__ == '__main__':
    main()
