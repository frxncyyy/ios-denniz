#!/usr/bin/env python3
"""Reconstruct the six captured lock-screen fixed-UI shader variants.

The function-constant values below are the R100/R105 device captures for
fragment serials 4..9.  The shared vertex specialization is captured serial12.
This tool translates each pair through a supplied AIR bridge and optionally
reports both current fixed-UI Mali compiler policies.  Compiler refusals are
reported rather than treated as script failures: they are the regression
targets this fixture is intended to make reproducible.
"""
import argparse
import ctypes as C
import hashlib
import json
import re
from pathlib import Path


U32 = C.c_uint32


class Blob(C.Structure):
    _fields_ = [('bytes', C.POINTER(C.c_uint8)), ('length', C.c_size_t)]


class FunctionConstant(C.Structure):
    _fields_ = [('kind', U32), ('type', U32), ('index', U32), ('size', U32),
                ('name', C.POINTER(C.c_uint8)), ('name_len', C.c_size_t),
                ('data', C.c_uint8 * 64)]


class VertexAttribute(C.Structure):
    _fields_ = [('index', U32), ('format', U32), ('offset', U32), ('buffer_index', U32)]


class VertexLayout(C.Structure):
    _fields_ = [('index', U32), ('stride', U32), ('step_function', U32), ('step_rate', U32)]


class Blend(C.Structure):
    _fields_ = [(name,U32) for name in ('version','enabled','rgb_op','rgb_src','rgb_dst','alpha_op','alpha_src','alpha_dst','write_mask')]


class Graphics(C.Structure):
    _fields_ = [('code', C.c_void_p), ('size', C.c_size_t)] + [
        (name, U32) for name in
        ('stage', 'registers', 'tls', 'fau_words', 'sysval_mask', 'push_bytes')
    ] + [('preload', C.c_uint64)]


class Varying(C.Structure):
    _fields_ = [(name, U32) for name in ('location', 'offset', 'components', 'alu_type')]


class Pair(C.Structure):
    _fields_ = [(name, U32) for name in (
        'version', 'flags', 'position_offset', 'position_size', 'point_position_offset',
        'point_position_size', 'secondary_offset', 'secondary_size',
        'secondary_registers', 'generic_size_B')] + [('secondary_preload', C.c_uint64)] + [
        (name, U32) for name in
        ('vs_attribute_mask', 'vs_varying_mask', 'fs_varying_mask', 'fs_uses_ld_var',
         'noperspective_mask', 'needs_extended_fifo', 'point_size_section',
         'point_size_offset', 'point_size_alu_type', 'varying_count')
    ] + [('varyings', Varying * 4)]


class Ubo(C.Structure):
    _fields_ = [(name, U32) for name in
                ('stage', 'descriptor_set', 'binding', 'table', 'index', 'declared_bytes')]


class R80Plan(C.Structure):
    _fields_ = [(name, U32) for name in
                ('version', 'table', 'resource_count', 'texture_count',
                 'constant_bytes', 'constant_access_bytes')] + [('ubos', Ubo * 3)]


class R85Plan(C.Structure):
    _fields_ = [(name, U32) for name in
                ('version', 'table', 'resource_count', 'texture_index', 'sampler_index',
                 'normalized_coordinates', 'sample_count', 'implicit_lod', 'has_bias',
                 'constant_bytes', 'constant_access_bytes')] + [('buffers', Ubo * 5)]


# Metal DataType values captured from the device: UInt=49, UChar3=51, Bool=53.
BOOL_NAMES = {
    'fc_clamp_color', 'fc_debug_xr', 'fc_extended_range', 'fc_framebuffer_fetch',
    'fc_framebuffer_sample', 'fc_generic', 'fc_unbounded_range',
}
BOOL_NAMES.update({
    'fc_color_opaque0', 'fc_color_opaque1', 'fc_has_asg_77_weights',
    'fc_has_attachment_0', 'fc_has_attachment_1', 'fc_has_attachment_2',
    'fc_has_color_cube0', 'fc_has_color_cube1', 'fc_has_color_params',
    'fc_has_color_trc0', 'fc_has_color_trc1', 'fc_has_edr_factor',
    'fc_has_gamma_luts', 'fc_has_hdr_scale', 'fc_has_image_0A',
    'fc_has_image_0B', 'fc_has_image_0C', 'fc_has_image_1A',
    'fc_has_image_1B', 'fc_has_image_1C', 'fc_has_lod_bias', 'fc_has_lut_tex',
    'fc_has_noise_scale', 'fc_has_noise_tex', 'fc_has_premultiply',
    'fc_has_ycbcr_matrix',
})

FRAGMENT_NAMES = [
    'fc_attachment_count', 'fc_blend_function', 'fc_clamp_color',
    'fc_color_inputs0', 'fc_color_inputs1', 'fc_color_opaque0', 'fc_color_opaque1',
    'fc_color_opcodes0', 'fc_color_opcodes1', 'fc_coord_function0',
    'fc_coord_function1', 'fc_debug_xr', 'fc_dest_count', 'fc_dest_index',
    'fc_destination_function', 'fc_extended_range', 'fc_filter0', 'fc_filter1',
    'fc_framebuffer_fetch', 'fc_framebuffer_sample', 'fc_generic',
    'fc_has_asg_77_weights', 'fc_has_attachment_0', 'fc_has_attachment_1',
    'fc_has_attachment_2', 'fc_has_color_cube0', 'fc_has_color_cube1',
    'fc_has_color_params', 'fc_has_color_trc0', 'fc_has_color_trc1',
    'fc_has_edr_factor', 'fc_has_gamma_luts', 'fc_has_hdr_scale',
    'fc_has_image_0A', 'fc_has_image_0B', 'fc_has_image_0C', 'fc_has_image_1A',
    'fc_has_image_1B', 'fc_has_image_1C', 'fc_has_lod_bias', 'fc_has_lut_tex',
    'fc_has_noise_scale', 'fc_has_noise_tex', 'fc_has_premultiply',
    'fc_has_ycbcr_matrix', 'fc_image_count', 'fc_image_function0',
    'fc_image_function1', 'fc_memoryless_index0', 'fc_memoryless_index1',
    'fc_noise_width', 'fc_plane_count0', 'fc_plane_count1',
    'fc_texcoord_count0', 'fc_texcoord_count1', 'fc_texture_function',
    'fc_unbounded_range', 'fc_vertex_layout',
]

# Changes from the captured serial0 fragment baseline.  Serial2 is the already
# qualified R85 logo and is deliberately not repeated here.
VARIANTS = {
    4: {'fc_image_count': 1, 'fc_image_function0': 0x0f, 'fc_texture_function': 3},
    5: {'fc_blend_function': 0x29, 'fc_dest_count': 1, 'fc_has_image_0A': 1,
        'fc_has_lod_bias': 1, 'fc_has_noise_scale': 1, 'fc_has_noise_tex': 1,
        'fc_image_count': 1, 'fc_noise_width': 0x20, 'fc_plane_count0': 1,
        'fc_texture_function': 1},
    6: {'fc_image_count': 2, 'fc_image_function0': 0x0f,
        'fc_image_function1': 0x0f, 'fc_texture_function': 5},
    7: {'fc_dest_count': 1, 'fc_image_count': 1, 'fc_image_function0': 0x0f,
        'fc_texture_function': 0x2d},
    8: {'fc_blend_function': 0x2e, 'fc_dest_count': 1, 'fc_image_count': 1,
        'fc_image_function0': 0x0a, 'fc_texcoord_count0': 1,
        'fc_texture_function': 3},
    9: {'fc_coord_function0': 5, 'fc_has_image_0A': 1, 'fc_has_lod_bias': 1,
        'fc_image_count': 1, 'fc_image_function0': 0x11, 'fc_plane_count0': 1,
        'fc_texcoord_count0': 1, 'fc_texture_function': 3},
}


def profile_bytes(path):
    data = path.read_bytes()
    if data.lstrip().startswith(b'{'):
        json.loads(data)
        return data
    text = data.decode()
    match = re.search(r'\{([^{}]*)\}', text, re.S)
    if not match:
        raise ValueError('profile must be Khronos JSON or a generated C byte-array header')
    values = [int(value) for value in match.group(1).split(',') if value.strip()]
    return bytes(values)


def baseline_fragment():
    values = {}
    for name in FRAGMENT_NAMES:
        ty = 51 if name.startswith('fc_color_opcodes') else 53 if name in BOOL_NAMES else 49
        values[name] = (ty, b'\0\0\0' if ty == 51 else b'\0')
    values['fc_attachment_count'] = (49, b'\1')
    values['fc_blend_function'] = (49, b'\x36')
    values['fc_framebuffer_fetch'] = (53, b'\1')
    return values


def constants(values):
    array = (FunctionConstant * len(values))()
    names = []
    for i, (name, (ty, value)) in enumerate(values.items()):
        names.append((C.c_uint8 * len(name)).from_buffer_copy(name.encode()))
        array[i].kind = 1
        array[i].type = ty
        array[i].size = len(value)
        array[i].name = names[-1]
        array[i].name_len = len(name)
        array[i].data[:len(value)] = value
    return array, names


def configure_bridge(path):
    lib = C.CDLL(str(path.resolve()))
    lib.tegu_air_library_open.restype = C.c_void_p
    lib.tegu_air_library_open.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
    lib.tegu_air_library_specialize.restype = C.c_void_p
    lib.tegu_air_library_specialize.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t,
        C.POINTER(FunctionConstant), C.c_size_t, C.c_void_p, C.c_size_t]
    lib.tegu_air_library_close.argtypes = [C.c_void_p]
    lib.tegu_air_render_pipeline_create_with_vertex_descriptor.restype = C.c_void_p
    lib.tegu_air_render_pipeline_create_with_vertex_descriptor.argtypes = [
        C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p, C.c_size_t,
        U32, C.POINTER(VertexAttribute), C.c_size_t, C.POINTER(VertexLayout), C.c_size_t,
        C.c_void_p, C.c_size_t, C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob),
        C.c_void_p, C.c_size_t]
    fn = lib.tegu_air_render_pipeline_create_with_blend
    fn.restype = C.c_void_p
    args = lib.tegu_air_render_pipeline_create_with_vertex_descriptor.argtypes
    fn.argtypes = args[:11] + [C.POINTER(Blend)] + args[11:]
    lib.tegu_air_render_pipeline_close.argtypes = [C.c_void_p]
    lib.tegu_air_blob_free.argtypes = [C.POINTER(Blob)]
    return lib


def specialize(lib, library, function, values):
    rows, keepalive = constants(values)
    error = C.create_string_buffer(1024)
    handle = lib.tegu_air_library_specialize(library, function.encode(), len(function),
        rows, len(values), error, len(error))
    del keepalive
    if not handle:
        raise RuntimeError(f'{function} specialization failed: {error.value.decode(errors="replace")}')
    return handle


def translate(lib, base, profile, serial, target_format=80, blend=None):
    vertex = {
        'fc_generic': (53, b'\0'), 'fc_texcoord_count0': (49, b'\1'),
        'fc_texcoord_count1': (49, b'\1'), 'fc_vertex_layout': (49, b'\0'),
    }
    fragment = baseline_fragment()
    for name, value in VARIANTS[serial].items():
        ty, old = fragment[name]
        fragment[name] = (ty, value.to_bytes(len(old), 'little'))
    vs_lib = specialize(lib, base, 'fixed_vert_lph_spc', vertex)
    fs_lib = specialize(lib, base, 'fixed_frag_lph_cph', fragment)
    attributes = (VertexAttribute * 4)(
        VertexAttribute(0, 31, 0, 1), VertexAttribute(1, 29, 16, 1),
        VertexAttribute(2, 29, 24, 1), VertexAttribute(3, 27, 32, 1))
    layouts = (VertexLayout * 1)(VertexLayout(1, 48, 1, 1))
    output = (Blob(), Blob(), Blob())
    error = C.create_string_buffer(2048)
    profile_buffer = C.create_string_buffer(profile)
    create = lib.tegu_air_render_pipeline_create_with_blend if blend else lib.tegu_air_render_pipeline_create_with_vertex_descriptor
    extra = [C.byref(blend)] if blend else []
    pipeline = create(
        vs_lib, b'fixed_vert_lph_spc', len('fixed_vert_lph_spc'),
        fs_lib, b'fixed_frag_lph_cph', len('fixed_frag_lph_cph'), target_format,
        attributes, 4, layouts, 1, *extra, profile_buffer, len(profile),
        C.byref(output[0]), C.byref(output[1]), C.byref(output[2]), error, len(error))
    lib.tegu_air_library_close(vs_lib)
    lib.tegu_air_library_close(fs_lib)
    if not pipeline:
        message = error.value.decode(errors='replace')
        for value in output:
            lib.tegu_air_blob_free(C.byref(value))
        raise RuntimeError(f'serial{serial} pipeline failed: {message}')
    spirv = [C.string_at(value.bytes, value.length) for value in output[:2]]
    metadata = C.string_at(output[2].bytes, output[2].length)
    lib.tegu_air_render_pipeline_close(pipeline)
    for value in output:
        lib.tegu_air_blob_free(C.byref(value))
    return spirv[0], spirv[1], metadata


def configure_compiler(path):
    lib = C.CDLL(str(path.resolve()))
    common = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
              C.POINTER(Graphics), C.POINTER(Graphics), C.POINTER(Pair)]
    lib.tegu_mali_compile_fixed_ui_pair_r80.argtypes = common + [C.POINTER(R80Plan), C.c_void_p, C.c_size_t]
    lib.tegu_mali_compile_fixed_ui_pair_r80.restype = C.c_int
    lib.tegu_mali_compile_fixed_ui_texture_pair_r85.argtypes = common + [C.POINTER(R85Plan), C.c_void_p, C.c_size_t]
    lib.tegu_mali_compile_fixed_ui_texture_pair_r85.restype = C.c_int
    lib.tegu_mali_graphics_free.argtypes = [C.POINTER(Graphics)]
    return lib


def compile_policy(lib, entry, plan_type, vertex, fragment):
    vb, fb = C.create_string_buffer(vertex), C.create_string_buffer(fragment)
    vs, fs, pair, plan = Graphics(), Graphics(), Pair(), plan_type()
    error = C.create_string_buffer(1024)
    function = getattr(lib, entry)
    status = function(vb, len(vertex), fb, len(fragment), C.byref(vs), C.byref(fs),
                      C.byref(pair), C.byref(plan), error, len(error))
    result = {
        'status': status, 'message': error.value.decode(errors='replace'),
        'vertex_mali_bytes': vs.size, 'fragment_mali_bytes': fs.size,
        'pair_version': pair.version, 'vs_varying_mask': pair.vs_varying_mask,
        'fs_varying_mask': pair.fs_varying_mask, 'varying_count': pair.varying_count,
        'plan_version': plan.version,
    }
    if vs.code:
        result['vertex_mali_sha256'] = hashlib.sha256(C.string_at(vs.code, vs.size)).hexdigest()
    if fs.code:
        result['fragment_mali_sha256'] = hashlib.sha256(C.string_at(fs.code, fs.size)).hexdigest()
    lib.tegu_mali_graphics_free(C.byref(vs))
    lib.tegu_mali_graphics_free(C.byref(fs))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--metallib', type=Path, required=True)
    parser.add_argument('--profile', type=Path, required=True,
                        help='Khronos JSON or generated tegu-compositor-profile.h')
    parser.add_argument('--compiler', type=Path,
                        help='optional libtegu_mali_graphics.so')
    parser.add_argument('--output-dir', type=Path,
                        help='optionally write serialN-vs.spv, serialN-fs.spv and metadata JSON')
    args = parser.parse_args()
    bridge = configure_bridge(args.bridge)
    metallib = args.metallib.read_bytes()
    metallib_buffer = C.create_string_buffer(metallib)
    error = C.create_string_buffer(1024)
    base = bridge.tegu_air_library_open(metallib_buffer, len(metallib), error, len(error))
    if not base:
        raise SystemExit('AIR library open failed: ' + error.value.decode(errors='replace'))
    profile = profile_bytes(args.profile)
    compiler = configure_compiler(args.compiler) if args.compiler else None
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
    report = {'metallib_sha256': hashlib.sha256(metallib).hexdigest(),
              'profile_sha256': hashlib.sha256(profile).hexdigest(), 'variants': []}
    try:
        for serial in sorted(VARIANTS):
            vertex, fragment, metadata = translate(bridge, base, profile, serial)
            item = {
                'serial': serial,
                'constants': VARIANTS[serial],
                'vertex_spirv_bytes': len(vertex),
                'vertex_spirv_sha256': hashlib.sha256(vertex).hexdigest(),
                'fragment_spirv_bytes': len(fragment),
                'fragment_spirv_sha256': hashlib.sha256(fragment).hexdigest(),
                'metadata_bytes': len(metadata),
                'metadata_sha256': hashlib.sha256(metadata).hexdigest(),
            }
            if compiler:
                item['mali_r80'] = compile_policy(compiler,
                    'tegu_mali_compile_fixed_ui_pair_r80', R80Plan, vertex, fragment)
                item['mali_r85'] = compile_policy(compiler,
                    'tegu_mali_compile_fixed_ui_texture_pair_r85', R85Plan, vertex, fragment)
            if args.output_dir:
                prefix = args.output_dir / f'serial{serial}'
                prefix.with_name(prefix.name + '-vs.spv').write_bytes(vertex)
                prefix.with_name(prefix.name + '-fs.spv').write_bytes(fragment)
                prefix.with_name(prefix.name + '-metadata.json').write_bytes(metadata)
            report['variants'].append(item)
    finally:
        bridge.tegu_air_library_close(base)
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
