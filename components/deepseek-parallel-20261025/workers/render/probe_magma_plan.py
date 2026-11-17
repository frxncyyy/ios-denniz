#!/usr/bin/env python3
"""Probe the real installed magma VS/FS pair through the frozen baseline bridge.

No production edit, no GPU. Shows exactly what the generic render plan does for
a real vertex-stage texture before the render-side binding work.
"""
import ctypes as C
import json
import sys
from pathlib import Path

ROOT = Path('$NORD2_WORK/gpu')
BASE = ROOT / 'build/ui-corpus-20261025'
sys.path.insert(0, str(ROOT / 'metal/tegu'))
import check_fixed_ui_live_variants as live


class Resource(C.Structure):
    _fields_ = [
        ('kind', C.c_uint32), ('stage', C.c_uint32), ('index', C.c_uint32), ('format', C.c_uint32),
        ('texture_type', C.c_uint32), ('width', C.c_uint32), ('height', C.c_uint32),
        ('array_length', C.c_uint32), ('mipmap_levels', C.c_uint32), ('sample_count', C.c_uint32),
        ('usage', C.c_uint32), ('sparse', C.c_uint32), ('depth_twin', C.c_uint32),
        ('swizzle', C.c_uint32 * 4), ('parent_level', C.c_uint32), ('parent_width', C.c_uint32),
        ('parent_height', C.c_uint32), ('parent_depth', C.c_uint32), ('own_format_views', C.c_uint32),
        ('address_mode_identity', C.c_uint32), ('length', C.c_uint64), ('offset', C.c_uint64),
        ('gpu_address', C.c_uint64), ('resource_id', C.c_uint64), ('stride', C.c_uint32),
        ('reserved', C.c_uint32), ('min_filter', C.c_uint32), ('mag_filter', C.c_uint32),
        ('mip_filter', C.c_uint32), ('address_s', C.c_uint32), ('address_t', C.c_uint32),
        ('address_r', C.c_uint32), ('border_color', C.c_uint32),
        ('normalized_coordinates', C.c_uint32), ('lod_average', C.c_uint32),
        ('max_anisotropy', C.c_uint32), ('compare_function', C.c_uint32),
        ('reduction_mode', C.c_uint32), ('lod_min_clamp', C.c_float), ('lod_max_clamp', C.c_float),
    ]


class Draw(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'vertex_start', 'vertex_count', 'instance_count', 'base_instance')]


class Binding(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in
                ('binding', 'kind', 'descriptor_type', 'stage', 'index', 'sampler_stage', 'sampler_index', 'attachment',
                 'min_filter', 'mag_filter', 'mipmap_mode', 'address0', 'address1', 'address2', 'border_color',
                 'format', 'texture_type', 'width', 'height', 'reserved')] + [
                ('resource_id', C.c_uint64), ('bind_offset', C.c_uint64), ('resource_length', C.c_uint64),
                ('descriptor_offset', C.c_uint64), ('descriptor_length', C.c_uint64)]


class Push(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('stage', 'offset', 'length', 'bytes_offset')]


class GenericPlan(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in
                ('version', 'binding_count', 'push_count', 'push_bytes_length',
                 'constant_stage', 'constant_length', 'reads_framebuffer', 'reserved')] + [
                ('bindings', Binding * 16), ('pushes', Push * 8),
                ('push_bytes', C.c_uint8 * 256), ('constant_bytes', C.c_uint8 * 512)]


def texture(stage, index, rid, fmt=80, w=64, h=64, levels=1, usage=1, ttype=2):
    r = Resource()
    r.kind, r.stage, r.index = 2, stage, index
    r.format, r.texture_type, r.width, r.height = fmt, ttype, w, h
    r.array_length = r.mipmap_levels = r.sample_count = 1
    r.mipmap_levels = levels
    r.usage = usage
    r.swizzle[:] = (2, 3, 4, 5)
    r.address_mode_identity = 1
    r.resource_id = rid
    return r


def buffer(stage, index, length=256):
    r = Resource()
    r.kind, r.stage, r.index, r.length = 1, stage, index, length
    return r


def main():
    lib = live.configure_bridge(BASE / 'baseline/libtegu_air_bridge.so')
    profile = live.profile_bytes(BASE / 'baseline/tegu-air-profile.h')
    plain = live.configure_bridge(BASE / 'baseline/libtegu_air_bridge.so')
    lib.tegu_air_render_pipeline_plan_generic.restype = C.c_int32
    lib.tegu_air_render_pipeline_plan_generic.argtypes = [
        C.c_void_p, C.POINTER(Resource), C.c_size_t, C.POINTER(live.VertexLayout), C.c_size_t,
        C.POINTER(Draw), C.POINTER(GenericPlan), C.c_char_p, C.c_size_t]
    lib.tegu_air_blob_free.argtypes = [C.POINTER(C.c_void_p)]
    job = next(x for x in json.load(open(BASE / 'graphics-jobs.json'))['jobs'] if x['id'] == 'graphics-01523')
    err = C.create_string_buffer(4096)
    vh = lib.tegu_air_library_open((BASE / job['vertex']['air_saved_as']).read_bytes(),
                                   (BASE / job['vertex']['air_saved_as']).stat().st_size, err, len(err))
    fh = lib.tegu_air_library_open((BASE / job['fragment']['air_saved_as']).read_bytes(),
                                   (BASE / job['fragment']['air_saved_as']).stat().st_size, err, len(err))
    assert vh and fh, err.value.decode()
    out = [live.Blob(), live.Blob(), live.Blob()]
    pb = C.create_string_buffer(profile)
    vn, fn = b'magmaVertexShader', b'magmaForegroundFragmentShader'
    pipeline = lib.tegu_air_render_pipeline_create_with_vertex_descriptor(
        vh, vn, len(vn), fh, fn, len(fn), 80, None, 0, None, 0, pb, len(profile),
        *[C.byref(x) for x in out], err, len(err))
    assert pipeline, err.value.decode()
    meta = json.loads(C.string_at(out[2].bytes, out[2].length))
    print('stages', [(s['stage'], s['specialization']) for s in meta['stages']])
    print('deps', json.dumps(meta['dependencies'])[:2000])
    print('variant_bits', meta.get('variant_bits'))
    rows = (Resource * 5)()
    rows[0] = buffer(1, 0)
    rows[1] = texture(1, 2, 0x202, w=128, h=128, usage=5)
    for i, slot in enumerate((0, 1, 2)):
        rows[2 + i] = texture(2, slot, 0x300 + slot, w=64, h=64)
    draw = Draw(1, 0, 3, 1, 0)
    plan = GenericPlan()
    rc = lib.tegu_air_render_pipeline_plan_generic(pipeline, rows, len(rows), None, 0,
                                                   C.byref(draw), C.byref(plan), err, len(err))
    print('plan rc', rc, err.value.decode(errors='replace'))
    if rc == 0:
        print('bindings', plan.binding_count)
        for i in range(plan.binding_count):
            b = plan.bindings[i]
            print(' ', {n: getattr(b, n) for n, _ in Binding._fields_})
        print('pushes', plan.push_count, plan.push_bytes_length,
              [(plan.pushes[i].stage, plan.pushes[i].offset, plan.pushes[i].length,
                plan.pushes[i].bytes_offset) for i in range(plan.push_count)])
    lib.tegu_air_render_pipeline_close(pipeline)
    lib.tegu_air_library_close(vh)
    lib.tegu_air_library_close(fh)
    for x in out:
        lib.tegu_air_blob_free(C.byref(x))


if __name__ == '__main__':
    main()
