#!/usr/bin/env python3
"""Portable authored-LLVM checks for the additive compositor C ABI.

This uses repo-authored LLVM fixtures and generates a stage-local texture
fixture. Captured QuartzCore tests remain separate, using ignored research data.
"""
import argparse
import ctypes as C
import json
import pathlib
import subprocess


class Blob(C.Structure):
    _fields_ = [('bytes', C.POINTER(C.c_uint8)), ('length', C.c_size_t)]


class Attachment(C.Structure):
    _fields_ = [('format', C.c_uint32), ('write_mask', C.c_uint32)]


class Attr(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('index', 'format', 'offset', 'buffer_index')]


class Layout(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('index', 'stride', 'step_function', 'step_rate')]


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
    _fields_ = [(n, C.c_uint32) for n in
                ('version', 'vertex_start', 'vertex_count', 'instance_count', 'base_instance')]


class TexturePlan(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in
                ('version', 'stage', 'texture_index', 'sampler_index', 'descriptor_set',
                 'binding', 'normalized_coordinates')]


def empty():
    return Blob(None, 0)


def contents(lib, blob):
    return C.string_at(blob.bytes, blob.length) if blob.bytes else b''


def resources():
    rows = (Resource * 3)()
    b = rows[0]
    b.kind, b.stage, b.index, b.length = 1, 1, 1, 96
    t = rows[1]
    t.kind, t.stage, t.index, t.format, t.texture_type = 2, 2, 0, 70, 2
    t.width = t.height = 64
    t.array_length = t.mipmap_levels = t.sample_count = t.usage = 1
    t.swizzle[:] = (2, 3, 4, 5)
    t.address_mode_identity, t.resource_id = 1, 33
    s = rows[2]
    s.kind, s.stage, s.index = 3, 2, 0
    s.address_s = s.address_t = s.address_r = 0
    s.normalized_coordinates, s.max_anisotropy = 1, 1
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library', type=pathlib.Path, required=True)
    ap.add_argument('--fixture', type=pathlib.Path, required=True)
    ap.add_argument('--profile', type=pathlib.Path, required=True)
    ap.add_argument('--work', type=pathlib.Path, required=True)
    a = ap.parse_args()
    if a.work.exists():
        ap.error('--work must be a fresh directory')
    a.work.mkdir(parents=True)
    air_path = a.work / 'texture_probe.air'
    subprocess.run(['llvm-as', str(a.fixture), '-o', str(air_path)], check=True)
    air, profile = air_path.read_bytes(), a.profile.read_bytes()
    air_buf = C.create_string_buffer(air)
    profile_buf = C.create_string_buffer(profile)
    lib = C.CDLL(str(a.library.resolve()))
    P, S = C.c_void_p, C.c_size_t
    lib.tegu_air_library_open.argtypes = [P, S, P, S]
    lib.tegu_air_library_open.restype = P
    lib.tegu_air_render_pipeline_create_with_vertex_descriptor.argtypes = [
        P, P, S, P, P, S, C.c_uint32, C.POINTER(Attr), S, C.POINTER(Layout), S,
        P, S, C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob), P, S]
    lib.tegu_air_render_pipeline_create_with_vertex_descriptor.restype = P
    lib.tegu_air_render_pipeline_create_with_attachments.argtypes = [
        P, P, S, P, P, S, C.POINTER(Attachment), S, C.POINTER(Attr), S,
        C.POINTER(Layout), S, P, S, C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob), P, S]
    lib.tegu_air_render_pipeline_create_with_attachments.restype = P
    lib.tegu_air_render_pipeline_plan_draw.argtypes = [P, C.POINTER(Resource), S,
        C.POINTER(Draw), C.POINTER(Blob), P, S]
    lib.tegu_air_render_pipeline_plan_draw.restype = C.c_int32
    lib.tegu_air_render_pipeline_native_check.argtypes = [P, C.c_uint32, P, S]
    lib.tegu_air_render_pipeline_native_check.restype = C.c_int32
    lib.tegu_air_render_pipeline_native_check_with_texture.argtypes = [
        P, C.c_uint32, C.POINTER(TexturePlan), P, S]
    lib.tegu_air_render_pipeline_native_check_with_texture.restype = C.c_int32
    lib.tegu_air_library_close.argtypes = [P]
    lib.tegu_air_render_pipeline_close.argtypes = [P]
    lib.tegu_air_blob_free.argtypes = [C.POINTER(Blob)]
    err = C.create_string_buffer(1024)
    library = lib.tegu_air_library_open(air_buf, len(air), err, len(err))
    assert library, err.value.decode()
    vn, fn = b'texture_vertex', b'texture_fragment'
    attributes = (Attr * 2)(Attr(7, 31, 0, 1), Attr(8, 31, 16, 1))
    layouts = (Layout * 1)(Layout(1, 32, 1, 1))
    old_out = [empty(), empty(), empty()]
    old = lib.tegu_air_render_pipeline_create_with_vertex_descriptor(
        library, vn, len(vn), library, fn, len(fn), 80, attributes, 2, layouts, 1,
        profile_buf, len(profile), *map(C.byref, old_out), err, len(err))
    assert old, err.value.decode()
    single = (Attachment * 1)(Attachment(80, 15))
    new_out = [empty(), empty(), empty()]
    one = lib.tegu_air_render_pipeline_create_with_attachments(
        library, vn, len(vn), library, fn, len(fn), single, 1, attributes, 2, layouts, 1,
        profile_buf, len(profile), *map(C.byref, new_out), err, len(err))
    assert one, err.value.decode()
    # A one-target attachment request must preserve exact old SPIR-V bytes.
    assert contents(lib, old_out[0]) == contents(lib, new_out[0])
    assert contents(lib, old_out[1]) == contents(lib, new_out[1])
    mrt = (Attachment * 2)(Attachment(80, 15), Attachment(80, 0))
    mrt_out = [empty(), empty(), empty()]
    multi = lib.tegu_air_render_pipeline_create_with_attachments(
        library, vn, len(vn), library, fn, len(fn), mrt, 2, attributes, 2, layouts, 1,
        profile_buf, len(profile), *map(C.byref, mrt_out), err, len(err))
    assert multi, err.value.decode()
    metadata = json.loads(contents(lib, mrt_out[2]))
    assert metadata['requested_attachments'] == [{'format': 80, 'write_mask': 15},
                                                  {'format': 80, 'write_mask': 0}]
    assert len(metadata['graphics']['color_attachments']) == 2
    # Old APIs still refuse resources; the additive texture API returns the
    # same qualified plan for BGRA8 target acceptance from current 0129 base.
    assert lib.tegu_air_render_pipeline_native_check(one, 80, err, len(err)) == -1
    texture_plan = TexturePlan()
    assert lib.tegu_air_render_pipeline_native_check_with_texture(
        one, 80, C.byref(texture_plan), err, len(err)) == 0, err.value.decode()
    assert tuple(getattr(texture_plan, name) for name, _ in TexturePlan._fields_) == (1, 16, 0, 0, 0, 0, 1)
    untouched = TexturePlan.from_buffer_copy(bytes([0xA5]) * C.sizeof(TexturePlan))
    before_plan = bytes(untouched)
    assert lib.tegu_air_render_pipeline_native_check_with_texture(
        one, 70, C.byref(untouched), err, len(err)) == -1
    assert bytes(untouched) == before_plan
    plan_out = empty()
    draw = Draw(1, 0, 3, 1, 0)
    bindings = resources()
    rc = lib.tegu_air_render_pipeline_plan_draw(multi, bindings, len(bindings),
                                                       C.byref(draw), C.byref(plan_out), err, len(err))
    assert rc == 0, err.value.decode()
    plan_json = json.loads(contents(lib, plan_out))
    assert plan_json['scope'].startswith('metadata-only')
    assert [x['write_mask'] for x in plan_json['attachments']] == [15, 0]
    assert plan_json['bound_resources'][1]['row_stride_bytes'] is None
    assert plan_json['bound_resources'][1]['backing_span_bytes'] is None
    lib.tegu_air_blob_free(C.byref(plan_out))
    wrong = resources()
    wrong[1].index = 3
    neutral = empty()
    rc = lib.tegu_air_render_pipeline_plan_draw(multi, wrong, len(wrong),
                                                       C.byref(draw), C.byref(neutral), err, len(err))
    assert rc == 0, err.value.decode()
    neutral_plan = json.loads(contents(lib, neutral))
    assert 'Neutral' in json.dumps(neutral_plan['plan']['descriptors'])
    assert neutral_plan['auxiliary_requirements']['dependencies']
    lib.tegu_air_blob_free(C.byref(neutral))
    # Exercise the entire bounded key space without requiring donor shaders.
    all_resources = []
    for stage in (1, 2):
        for kind, limit in ((1, 31), (2, 31), (3, 16)):
            for index in range(limit):
                row = Resource.from_buffer_copy(bytes(resources()[kind - 1]))
                row.stage, row.index = stage, index
                if kind == 2:
                    row.resource_id = stage * 100 + index + 1
                all_resources.append(row)
    maximum = (Resource * len(all_resources))(*all_resources)
    assert len(maximum) == 156
    maximum_out = empty()
    assert lib.tegu_air_render_pipeline_plan_draw(multi, maximum, len(maximum),
        C.byref(draw), C.byref(maximum_out), err, len(err)) == 0, err.value.decode()
    assert len(json.loads(contents(lib, maximum_out))['bound_resources']) == 156
    lib.tegu_air_blob_free(C.byref(maximum_out))
    for bad_index in (16, 30):
        bad = resources()
        bad[2].index = bad_index
        refused = empty()
        assert lib.tegu_air_render_pipeline_plan_draw(multi, bad, len(bad),
            C.byref(draw), C.byref(refused), err, len(err)) == -1
        assert not refused.bytes and refused.length == 0

    # A no-resource shader must support the conventional NULL/count0 span.
    plain_ll = pathlib.Path(__file__).with_name('fixtures') / 'legacy-v1.ll'
    plain_air = a.work / 'plain.air'
    subprocess.run(['llvm-as', str(plain_ll), '-o', str(plain_air)], check=True)
    raw = plain_air.read_bytes()
    raw_buffer = C.create_string_buffer(raw)
    plain_library = lib.tegu_air_library_open(raw_buffer, len(raw), err, len(err))
    assert plain_library, err.value.decode()
    plain_outputs = [empty(), empty(), empty()]
    pv, pf = b'test_vertex', b'test_fragment'
    plain = lib.tegu_air_render_pipeline_create_with_attachments(
        plain_library, pv, len(pv), plain_library, pf, len(pf), single, 1,
        None, 0, None, 0, profile_buf, len(profile),
        *map(C.byref, plain_outputs), err, len(err))
    assert plain, err.value.decode()
    plain_plan = empty()
    assert lib.tegu_air_render_pipeline_plan_draw(plain, None, 0, C.byref(draw),
        C.byref(plain_plan), err, len(err)) == 0, err.value.decode()
    assert json.loads(contents(lib, plain_plan))['bound_resources'] == []
    lib.tegu_air_blob_free(C.byref(plain_plan))
    assert lib.tegu_air_render_pipeline_plan_draw(plain, None, 1, C.byref(draw),
        C.byref(plain_plan), err, len(err)) == -1
    assert not plain_plan.bytes and plain_plan.length == 0
    lib.tegu_air_render_pipeline_close(plain)
    lib.tegu_air_library_close(plain_library)
    for output in plain_outputs:
        lib.tegu_air_blob_free(C.byref(output))

    # Make the authored VS sample a texture too. Different stage-local texture
    # tokens ensure a VS binding accidentally routed to FS cannot pass as valid.
    vertex_source = a.fixture.read_text().replace(
        '@texture_vertex(<4 x float> %position, <4 x float> %color) {',
        '@texture_vertex(<4 x float> %position, <4 x float> %color, ptr addrspace(1) %t, ptr addrspace(2) %s) {')
    vertex_source = vertex_source.replace(
        '  %with_position = insertvalue',
        '  %uv = shufflevector <4 x float> %color, <4 x float> poison, <2 x i32> <i32 0, i32 1>\n'
        '  %sample = call { <4 x float>, i8 } @air.sample_texture_2d.v4f32(ptr addrspace(1) %t, ptr addrspace(2) %s, <2 x float> %uv, i1 true, <2 x i32> zeroinitializer, i1 true, float 0.0, float 0.0, i32 0)\n'
        '  %vs_color = extractvalue { <4 x float>, i8 } %sample, 0\n'
        '  %with_position = insertvalue')
    vertex_source = vertex_source.replace('%with_position, <4 x float> %color, 1',
        '%with_position, <4 x float> %vs_color, 1').replace('!4 = !{!5, !6}', '!4 = !{!5, !6, !20, !21}')
    vertex_source += '\n!20 = !{i32 2, !"air.texture", !"air.location_index", i32 0, i32 1, !"air.sample", !"air.arg_type_name", !"texture2d<float, sample>", !"air.arg_name", !"t"}\n!21 = !{i32 3, !"air.sampler", !"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"sampler", !"air.arg_name", !"s"}\n'
    vs_ll, vs_air = a.work / 'vertex-texture.ll', a.work / 'vertex-texture.air'
    vs_ll.write_text(vertex_source)
    subprocess.run(['llvm-as', str(vs_ll), '-o', str(vs_air)], check=True)
    raw = vs_air.read_bytes()
    raw_buffer = C.create_string_buffer(raw)
    vertex_library = lib.tegu_air_library_open(raw_buffer, len(raw), err, len(err))
    assert vertex_library, err.value.decode()
    vertex_outputs = [empty(), empty(), empty()]
    both = lib.tegu_air_render_pipeline_create_with_attachments(
        vertex_library, vn, len(vn), vertex_library, fn, len(fn), single, 1,
        attributes, 2, layouts, 1, profile_buf, len(profile),
        *map(C.byref, vertex_outputs), err, len(err))
    assert both, err.value.decode()
    stage_resources = list(resources())
    for row in resources()[1:]:
        row = Resource.from_buffer_copy(bytes(row))
        row.stage = 1
        if row.kind == 2:
            row.resource_id = 99
        stage_resources.append(row)
    stage_resources = (Resource * len(stage_resources))(*stage_resources)
    both_plan = empty()
    assert lib.tegu_air_render_pipeline_plan_draw(both, stage_resources,
        len(stage_resources), C.byref(draw), C.byref(both_plan), err, len(err)) == 0, err.value.decode()
    both_json = json.loads(contents(lib, both_plan))
    assert 'Neutral' not in json.dumps(both_json['plan']['descriptors'])
    (a.work / 'vertex-texture-plan.json').write_text(json.dumps(both_json, indent=2) + '\n')
    lib.tegu_air_blob_free(C.byref(both_plan))
    lib.tegu_air_render_pipeline_close(both)
    lib.tegu_air_library_close(vertex_library)
    for output in vertex_outputs:
        lib.tegu_air_blob_free(C.byref(output))

    report = {'fixture': str(a.fixture), 'air_sha256': __import__('hashlib').sha256(air).hexdigest(),
              'legacy_vs_spirv_identical': True, 'legacy_fs_spirv_identical': True,
              'explicit_requested_mrt_masks': [15, 0], 'authored_resource_plan_succeeded': True,
              'all_156_legal_keys_planned': True, 'null_empty_resource_span_planned': True,
              'vertex_and_fragment_textures_bound_separately': True,
              'missing_texture_reported_as_neutral_with_dependencies': True,
              'translator_graphics_state': metadata['graphics']['color_attachments'],
              'scope': 'authored host compiler regression; no native execution'}
    (a.work / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    for outputs in (old_out, new_out, mrt_out):
        for blob in outputs:
            lib.tegu_air_blob_free(C.byref(blob))
    for handle in (old, one, multi):
        lib.tegu_air_render_pipeline_close(handle)
    lib.tegu_air_library_close(library)
    print('PASS authored texture_probe.ll explicit MRT/plan, old-SPIR-V parity, neutral resources reported')


if __name__ == '__main__':
    main()
