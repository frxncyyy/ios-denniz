#!/usr/bin/env python3
"""Build actual RGBA/BGRA blended AIR variants and validate their host gates.

Requires the pinned metal2vulkan checkout, an authored render AIR library, and
the real Mali graphics compiler. All generated files stay beneath --work.
This validates host metadata/codegen only; it does not execute on a GPU.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import sys


class Blob(C.Structure):
    _fields_ = [('bytes', C.c_void_p), ('length', C.c_size_t)]


class Attribute(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('index', 'format', 'offset', 'buffer_index')]


class Layout(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('index', 'stride', 'step_function', 'step_rate')]


class Blend(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'enabled', 'rgb_op', 'rgb_src', 'rgb_dst',
        'alpha_op', 'alpha_src', 'alpha_dst', 'write_mask')]


class BlendState(C.Structure):
    _fields_ = [('words', C.c_uint32 * 4), ('reads_destination', C.c_uint32), ('opaque', C.c_uint32)]


class TexturePlan(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'stage', 'texture_index',
        'sampler_index', 'descriptor_set', 'binding', 'normalized_coordinates')]


def bind_bridge(lib):
    blobp = C.POINTER(Blob)
    lib.tegu_air_library_open.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
    lib.tegu_air_library_open.restype = C.c_void_p
    lib.tegu_air_library_close.argtypes = [C.c_void_p]
    lib.tegu_air_render_pipeline_close.argtypes = [C.c_void_p]
    lib.tegu_air_blob_free.argtypes = [blobp]
    lib.tegu_air_render_pipeline_create_with_blend.argtypes = [C.c_void_p,
        C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p, C.c_size_t, C.c_uint32,
        C.POINTER(Attribute), C.c_size_t, C.POINTER(Layout), C.c_size_t,
        C.POINTER(Blend), C.c_void_p, C.c_size_t, blobp, blobp, blobp, C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_create_with_blend.restype = C.c_void_p
    lib.tegu_air_render_pipeline_native_check_with_blend.argtypes = [C.c_void_p,
        C.c_uint32, C.POINTER(Blend), C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_native_check_with_blend.restype = C.c_int32
    lib.tegu_air_render_pipeline_native_check.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_native_check.restype = C.c_int32
    lib.tegu_air_render_pipeline_native_check_with_vertex_fetch.argtypes = lib.tegu_air_render_pipeline_native_check.argtypes
    lib.tegu_air_render_pipeline_native_check_with_vertex_fetch.restype = C.c_int32
    lib.tegu_air_render_pipeline_native_check_with_texture.argtypes = [C.c_void_p,
        C.c_uint32, C.POINTER(TexturePlan), C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_native_check_with_texture.restype = C.c_int32


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translator', type=Path, required=True, help='pinned metal2vulkan checkout')
    parser.add_argument('--air', type=Path, required=True, help='AIR library with varying_vertex and varying_fragment')
    parser.add_argument('--mali-library', type=Path, required=True, help='real libtegu_mali_graphics.so')
    parser.add_argument('--work', type=Path, required=True, help='new output directory beneath repository research/')
    parser.add_argument('--baseline', type=Path, help='optional pre-change bridge for old unblended ABI byte comparison')
    parser.add_argument('--profile', type=Path, help='validation profile; defaults to pinned vk12-bare.json')
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[3]
    work = args.work.resolve()
    research = (repo / 'research').resolve()
    if not work.is_relative_to(research) or work == research or work.exists():
        parser.error('--work must be a fresh child directory of repository research/')
    translator, air, mali = (p.resolve() for p in (args.translator, args.air, args.mali_library))
    profile = (args.profile.resolve() if args.profile else
        translator / 'crates/metal2vulkan-validation/profiles/vk12-bare.json')
    for path in (translator, air, mali, profile):
        if not path.exists():
            parser.error(f'input does not exist: {path}')
    baseline = args.baseline.resolve() if args.baseline else None
    if baseline and not baseline.is_file():
        parser.error(f'baseline bridge not found: {baseline}')
    work.mkdir(parents=True)
    env = dict(os.environ)
    env['CARGO_NET_OFFLINE'] = 'true'
    metal = Path(__file__).resolve().parent
    subprocess.run([sys.executable, metal / 'build_air_bridge.py', '--translator', translator,
        '--work', work], check=True, env=env)
    bridge_path = work / 'target/release/libtegu_air_bridge.so'
    lib = C.CDLL(str(bridge_path))
    bind_bridge(lib)
    err = C.create_string_buffer(1024)
    air_bytes, profile_bytes = air.read_bytes(), profile.read_bytes()
    library = lib.tegu_air_library_open(air_bytes, len(air_bytes), err, len(err))
    assert library, err.value.decode(errors='replace')
    attrs = (Attribute * 2)(Attribute(7, 31, 0, 1), Attribute(8, 31, 16, 1))
    layouts = (Layout * 1)(Layout(1, 32, 1, 1))
    modes = {
        'source-alpha': Blend(1, 1, 0, 4, 5, 0, 4, 5, 15),
        'premultiplied': Blend(1, 1, 0, 1, 5, 0, 1, 5, 15),
        'separate-alpha': Blend(1, 1, 0, 4, 5, 0, 1, 5, 15),
    }
    factor_map = {0: 0, 1: 1, 2: 2, 3: 3, 4: 6, 5: 7, 6: 4, 7: 5, 8: 8, 9: 9}
    summaries = {}
    handles = []
    try:
        for mode, blend in modes.items():
            summaries[mode] = {}
            for label, fmt in (('rgba', 70), ('bgra', 80)):
                blobs = (Blob(), Blob(), Blob())
                handle = lib.tegu_air_render_pipeline_create_with_blend(library,
                    b'varying_vertex', 14, library, b'varying_fragment', 16, fmt,
                    attrs, 2, layouts, 1, C.byref(blend), profile_bytes, len(profile_bytes),
                    *(C.byref(blob) for blob in blobs), err, len(err))
                assert handle, f'{mode}/{label} create: {err.value.decode(errors="replace")}'
                handles.append(handle)
                assert lib.tegu_air_render_pipeline_native_check_with_blend(handle, fmt,
                    C.byref(blend), err, len(err)) == 0, (mode, label, err.value)
                assert lib.tegu_air_render_pipeline_native_check(handle, fmt, err, len(err)) == -1
                assert lib.tegu_air_render_pipeline_native_check_with_vertex_fetch(handle, fmt, err, len(err)) == -1
                assert lib.tegu_air_render_pipeline_native_check_with_texture(handle, fmt,
                    C.byref(TexturePlan()), err, len(err)) == -1
                data = []
                for suffix, blob in zip(('vertex', 'fragment', 'metadata'), blobs):
                    value = C.string_at(blob.bytes, blob.length)
                    ext = 'json' if suffix == 'metadata' else 'spv'
                    (work / f'{mode}-{label}-{suffix}.{ext}').write_bytes(value)
                    data.append(value)
                    lib.tegu_air_blob_free(C.byref(blob))
                metadata = json.loads(data[2])
                attachment = metadata['graphics']['color_attachments'][0]
                expected = dict(src_color=factor_map[blend.rgb_src], dst_color=factor_map[blend.rgb_dst],
                    color_op=blend.rgb_op, src_alpha=factor_map[blend.alpha_src],
                    dst_alpha=factor_map[blend.alpha_dst], alpha_op=blend.alpha_op)
                assert attachment['format'] == (37 if fmt == 70 else 44), attachment
                assert attachment['blend'] == expected and attachment['write_mask'] == 15, attachment
                summaries[mode][label] = {'format': attachment['format'], 'blend': expected}
            assert summaries[mode]['rgba']['blend'] == summaries[mode]['bgra']['blend']
        if baseline:
            base = C.CDLL(str(baseline))
            base.tegu_air_library_open.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
            base.tegu_air_library_open.restype = C.c_void_p
            base.tegu_air_library_close.argtypes = [C.c_void_p]
            base.tegu_air_blob_free.argtypes = [C.POINTER(Blob)]
            base.tegu_air_render_pipeline_close.argtypes = [C.c_void_p]
            create_args = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p,
                C.c_size_t, C.c_uint32, C.POINTER(Attribute), C.c_size_t, C.POINTER(Layout),
                C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(Blob), C.POINTER(Blob),
                C.POINTER(Blob), C.c_void_p, C.c_size_t]
            base_create = base.tegu_air_render_pipeline_create_with_vertex_descriptor
            base_create.argtypes = create_args
            base_create.restype = C.c_void_p
            new_create = lib.tegu_air_render_pipeline_create_with_vertex_descriptor
            new_create.argtypes = create_args
            new_create.restype = C.c_void_p
            old_err = C.create_string_buffer(1024)
            old_library = base.tegu_air_library_open(air_bytes, len(air_bytes), old_err, len(old_err))
            assert old_library, old_err.value
            try:
                for fmt in (70, 80):
                    old_blobs, new_blobs = (Blob(), Blob(), Blob()), (Blob(), Blob(), Blob())
                    args_old = (old_library, b'varying_vertex', 14, old_library,
                        b'varying_fragment', 16, fmt, attrs, 2, layouts, 1,
                        profile_bytes, len(profile_bytes))
                    old = base_create(*args_old, *(C.byref(blob) for blob in old_blobs), old_err, len(old_err))
                    assert old, old_err.value
                    new = new_create(library, b'varying_vertex', 14, library,
                        b'varying_fragment', 16, fmt, attrs, 2, layouts, 1,
                        profile_bytes, len(profile_bytes), *(C.byref(blob) for blob in new_blobs), err, len(err))
                    assert new, err.value
                    for left, right in zip(old_blobs, new_blobs):
                        assert C.string_at(left.bytes, left.length) == C.string_at(right.bytes, right.length)
                        base.tegu_air_blob_free(C.byref(left))
                        lib.tegu_air_blob_free(C.byref(right))
                    base.tegu_air_render_pipeline_close(old)
                    lib.tegu_air_render_pipeline_close(new)
                print('PASS old unblended RGBA/BGRA AIR/SPIR-V/metadata byte parity', flush=True)
            finally:
                base.tegu_air_library_close(old_library)
        subprocess.run(['clang', '-shared', '-fPIC', '-O2', metal / 'blend_state.c',
            '-o', work / 'blend_state.so'], check=True)
        converter = C.CDLL(str(work / 'blend_state.so')).tegu_blend_build_format
        converter.argtypes = [C.c_uint32, C.POINTER(Blend), C.POINTER(BlendState)]
        converter.restype = C.c_int
        for blend in modes.values():
            rgba, bgra = BlendState(), BlendState()
            assert converter(70, C.byref(blend), C.byref(rgba)) == 0
            assert converter(80, C.byref(blend), C.byref(bgra)) == 0
            assert bytes(rgba) == bytes(bgra) and rgba.words[3] == (237 << 12)
        compiler = metal / 'compile_graphics_pair.py'
        for mode in modes:
            for label in ('rgba', 'bgra'):
                vertex, fragment = work / f'{mode}-{label}-vertex.spv', work / f'{mode}-{label}-fragment.spv'
                subprocess.run(['spirv-val', '--target-env', 'vulkan1.2', vertex], check=True)
                subprocess.run(['spirv-val', '--target-env', 'vulkan1.2', fragment], check=True)
                subprocess.run([sys.executable, compiler, '--library', mali,
                    '--vertex', vertex, '--fragment', fragment, '--out', work / f'{mode}-{label}-mali'], check=True)
                pair = json.loads((work / f'{mode}-{label}-mali/pair_metadata.json').read_text())
                assert pair['status'] == 0 and pair['gpu_id'] == '0xb8a21030', pair
        result = {'host_only': True, 'native_gpu_executed': False, 'formats': {'rgba': 37, 'bgra': 44},
            'logical_blend_word3': hex(237 << 12), 'blend_facts_identical_across_formats': True,
            'mali_pair_compile': {mode: {'rgba': 'pass', 'bgra': 'pass'} for mode in modes}}
        (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    finally:
        lib.tegu_air_library_close(library)
        for handle in handles:
            lib.tegu_air_render_pipeline_close(handle)
    print(f'PASS actual RGBA/BGRA blend AIR gate, metadata, converter, SPIR-V validation and six Mali host pairs; GPU_EXECUTED=0; {work}')


if __name__ == '__main__':
    main()
