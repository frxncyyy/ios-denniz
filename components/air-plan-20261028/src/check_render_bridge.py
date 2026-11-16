#!/usr/bin/env python3
"""Compile authored vertex/fragment AIR through the real C ABI; no GPU claim."""
import argparse
import ctypes as C
import json
from pathlib import Path
import subprocess


class Blob(C.Structure):
    _fields_ = [('bytes', C.c_void_p), ('length', C.c_size_t)]


class RenderPush(C.Structure):
    _fields_ = [('version', C.c_uint32), ('vertex_span', C.c_uint32),
                ('fragment_span', C.c_uint32), ('vertex', C.c_ubyte * 128),
                ('fragment', C.c_ubyte * 128)]

class VertexAttribute(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ('index', 'format', 'offset', 'buffer_index')]

class VertexLayout(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ('index', 'stride', 'step_function', 'step_rate')]

class FetchAttribute(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ('location', 'buffer_index', 'format', 'offset', 'stride', 'input_rate', 'divisor')]

class VertexFetch(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in ('version', 'mask', 'count', 'reserved')] + [('attributes', FetchAttribute * 31)]


p = argparse.ArgumentParser(description=__doc__)
for name in ('bridge', 'translator', 'work'):
    p.add_argument('--' + name, type=Path, required=True)
p.add_argument("--varying", action="store_true", help="Authored position/color attributes and smooth float4 varying")
p.add_argument("--triangle", action="store_true", help="Authored vertex-ID triangle fixture")
p.add_argument('--vertex-attributes', action='store_true', help='Authored attribute fetch; validates compiler metadata and native refusal')
p.add_argument('--attribute-location', type=int, default=0, help='Attribute test location0..30, including sparse slots')
a = p.parse_args()
if sum((a.triangle,a.vertex_attributes,a.varying))>1:
    p.error('Choose one authored render fixture')
if not 0 <= a.attribute_location <= 30 or (a.attribute_location and not a.vertex_attributes):
    p.error('Attribute location requires --vertex-attributes and a slot0..30')
a.work.mkdir(parents=True, exist_ok=False)
fixture = a.work / 'render.ll'
fixture.write_text('''; Authored minimal render-pipeline test; no vendor shader.
define <4 x float> @test_vertex() {
 ret <4 x float> <float 0.0, float 0.0, float 0.0, float 1.0>
}
define <4 x float> @test_fragment() {
 ret <4 x float> <float 0.25, float 0.5, float 0.75, float 1.0>
}
!air.vertex = !{!0}
!air.fragment = !{!4}
!0 = !{ptr @test_vertex, !1, !3}
!1 = !{!2}
!2 = !{!"air.position", !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!3 = !{}
!4 = !{ptr @test_fragment, !5, !3}
!5 = !{!6}
!6 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}
''')
if a.triangle:
    text = fixture.read_text().replace(
        'define <4 x float> @test_vertex() {\n ret <4 x float> <float 0.0, float 0.0, float 0.0, float 1.0>\n}',
        '''define <4 x float> @test_vertex(i32 %vid) {
 %is0 = icmp eq i32 %vid, 0
 %is1 = icmp eq i32 %vid, 1
 %other = select i1 %is1, <4 x float> <float 0.75, float -0.75, float 0.5, float 1.0>, <4 x float> <float -0.75, float 0.75, float 0.5, float 1.0>
 %position = select i1 %is0, <4 x float> <float -0.75, float -0.75, float 0.5, float 1.0>, <4 x float> %other
 ret <4 x float> %position
}''').replace('!0 = !{ptr @test_vertex, !1, !3}', '!0 = !{ptr @test_vertex, !1, !7}')
    text += '\n!7 = !{!8}\n!8 = !{i32 0, !"air.vertex_id", !"air.arg_type_name", !"uint", !"air.arg_name", !"vid"}\n'
    fixture.write_text(text)
if a.vertex_attributes:
    text = fixture.read_text().replace(
        'define <4 x float> @test_vertex() {\n ret <4 x float> <float 0.0, float 0.0, float 0.0, float 1.0>\n}',
        'define <4 x float> @test_vertex(<4 x float> %position) {\n ret <4 x float> %position\n}'
    ).replace('!0 = !{ptr @test_vertex, !1, !3}', '!0 = !{ptr @test_vertex, !1, !7}')
    text += '\n!7 = !{!8}\n!8 = !{i32 0, !"air.vertex_input", !"air.location_index", i32 '+str(a.attribute_location)+', i32 1, !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}\n'
    fixture.write_text(text)
if a.varying:
    fixture.write_text(Path(__file__).with_name('varying_probe.ll').read_text())
vertex_name, fragment_name = (b'varying_vertex',b'varying_fragment') if a.varying else (b'test_vertex',b'test_fragment')
air = a.work / 'render.air'
subprocess.run(['llvm-as', str(fixture), '-o', str(air)], check=True)
data = air.read_bytes()
profile = (a.translator / 'crates/metal2vulkan-validation/profiles/vk12-bare.json').read_bytes()
lib = C.CDLL(str(a.bridge.resolve()))
lib.tegu_air_library_open.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
lib.tegu_air_library_open.restype = C.c_void_p
lib.tegu_air_library_close.argtypes = [C.c_void_p]
lib.tegu_air_render_pipeline_close.argtypes = [C.c_void_p]
lib.tegu_air_blob_free.argtypes = [C.POINTER(Blob)]
create = lib.tegu_air_render_pipeline_create
create.restype = C.c_void_p
create.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p,
                  C.c_size_t, C.c_uint32, C.c_void_p, C.c_size_t,
                  C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob), C.c_void_p, C.c_size_t]
extended = lib.tegu_air_render_pipeline_create_with_vertex_descriptor
extended.restype = C.c_void_p
extended.argtypes = create.argtypes[:7] + [C.POINTER(VertexAttribute), C.c_size_t,
    C.POINTER(VertexLayout), C.c_size_t] + create.argtypes[7:]
attributes = (VertexAttribute * 1)(VertexAttribute(a.attribute_location, 31, 16, 1))
layouts = (VertexLayout * 1)(VertexLayout(1, 32, 1, 1))
if a.varying:
    attributes = (VertexAttribute * 2)(VertexAttribute(7,31,0,1),VertexAttribute(8,31,16,1))
if a.vertex_attributes or a.varying:
    def create(*args):
        return extended(*args[:7], attributes, len(attributes), layouts, 1, *args[7:])
native_check = lib.tegu_air_render_pipeline_native_check
native_check.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_size_t]
native_check.restype = C.c_int32
fetch_check = lib.tegu_air_render_pipeline_native_check_with_vertex_fetch
fetch_check.argtypes = native_check.argtypes
fetch_check.restype = C.c_int32
push = lib.tegu_air_render_pipeline_push
push.argtypes = [C.c_void_p, C.POINTER(RenderPush), C.c_void_p, C.c_size_t]
push.restype = C.c_int32
fetch = lib.tegu_air_render_pipeline_vertex_fetch
fetch.argtypes = [C.c_void_p, C.POINTER(VertexFetch), C.c_void_p, C.c_size_t]
fetch.restype = C.c_int32
error = C.create_string_buffer(2048)
library = lib.tegu_air_library_open(data, len(data), error, len(error))
assert library, error.value
handles = []
try:
    for fmt, vertex, fragment, success in ((10, vertex_name, fragment_name, True),
                                         (70, vertex_name, fragment_name, True),
                                         (80, vertex_name, fragment_name, True),
                                         (0, vertex_name, fragment_name, False),
                                         (70, b'missing', fragment_name, False),
                                         (70, fragment_name, vertex_name, False)):
        outputs = [Blob(), Blob(), Blob()]
        handle = create(library, vertex, len(vertex), library, fragment, len(fragment),
                        fmt, profile, len(profile), *map(C.byref, outputs), error, len(error))
        if not success:
            assert not handle and error.value
            assert all(not b.bytes and not b.length for b in outputs)
            continue
        assert handle and not error.value, error.value
        handles.append(handle)
        meta = json.loads(C.string_at(outputs[2].bytes, outputs[2].length))
        assert meta['color_format'] == fmt and len(meta['stages']) == 2
        assert meta['scope'] == 'render_compilation_not_native_draw_plan'
        fetched = VertexFetch()
        assert fetch(handle, C.byref(fetched), error, len(error)) == 0, error.value
        assert fetched.version == 1 and not fetched.reserved
        if a.vertex_attributes:
            assert fetched.mask == 1 and fetched.count == 1
            actual = fetched.attributes[0]
            assert (actual.location, actual.buffer_index, actual.format, actual.offset,
                    actual.stride, actual.input_rate, actual.divisor) == (0, 1, 109, 16, 32, 0, 1)
        elif a.varying:
            assert fetched.mask == 3 and fetched.count == 2
            for i, actual in enumerate(fetched.attributes[:2]):
                assert (actual.location,actual.buffer_index,actual.format,actual.offset,
                        actual.stride,actual.input_rate,actual.divisor) == (i,1,109,16*i,32,0,1)
        else:
            assert not fetched.mask and not fetched.count
        (a.work / f'{fmt}-vertex-fetch.json').write_text(json.dumps({
            'version': fetched.version, 'mask': fetched.mask, 'count': fetched.count,
            'attributes': [{name: getattr(row, name) for name, _ in FetchAttribute._fields_}
                           for row in fetched.attributes[:fetched.count]]}, indent=2) + '\n')
        assert native_check(handle, fmt, error, len(error)) == (-1 if a.vertex_attributes or a.varying else 0), error.value
        if a.vertex_attributes:
            assert error.value == b'native render static vertex input unsupported', error.value
            vertex_input = meta['vertex_input']
            assert len(vertex_input['attributes']) == len(vertex_input['bindings']) == 1, vertex_input
            assert vertex_input['attributes'][0]['offset'] == 16, vertex_input
            # The linker may densely renumber Metal attribute indices. The
            # backend binds the returned Vulkan location, not the Metal index.
            assert vertex_input['attributes'][0]['location'] == 0, vertex_input
            assert vertex_input['bindings'][0]['buffer_index'] == 1, vertex_input
            assert vertex_input['bindings'][0]['stride'] == {'Static': 32}, vertex_input
        assert fetch_check(handle, fmt, error, len(error)) == 0, error.value
        assert fetch_check(handle, 80 if fmt == 70 else 70, error, len(error)) == -1
        assert native_check(handle, 80 if fmt == 70 else 70, error, len(error)) == -1
        assert error.value
        resolved = RenderPush()
        assert push(handle, C.byref(resolved), error, len(error)) == 0, error.value
        assert resolved.version == 1 and resolved.vertex_span == 0
        assert resolved.fragment_span == 68 and bytes(resolved.vertex) == bytes(128)
        expected = bytearray(128)
        expected[64:68] = (2 if fmt == 10 else 0x2222).to_bytes(4, 'little')
        assert bytes(resolved.fragment) == expected
        (a.work / f'{fmt}-push.json').write_text(json.dumps({
            'version': resolved.version, 'vertex_span': resolved.vertex_span,
            'fragment_span': resolved.fragment_span,
            'vertex': bytes(resolved.vertex).hex(), 'fragment': bytes(resolved.fragment).hex()
        }, indent=2) + '\n')
        for name, output in zip(('vertex', 'fragment'), outputs[:2]):
            binary = C.string_at(output.bytes, output.length)
            assert binary[:4] == bytes.fromhex('03022307')
            path = a.work / f'{fmt}-{name}.spv'
            path.write_bytes(binary)
            subprocess.run(['spirv-val', '--target-env', 'vulkan1.2', str(path)], check=True)
        (a.work / f'{fmt}-metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
        for output in outputs:
            lib.tegu_air_blob_free(C.byref(output))
    output = Blob()
    assert not create(library, vertex_name, len(vertex_name), library, fragment_name, len(fragment_name),
                      70, profile, len(profile), C.byref(output), C.byref(output),
                      C.byref(output), error, len(error))
    assert error.value
    if a.vertex_attributes:
        def refuse(attrs, attr_count, binds, bind_count):
            outputs = [Blob(), Blob(), Blob()]
            handle = extended(library, vertex_name, len(vertex_name), library, fragment_name, len(fragment_name),
                70, attrs, attr_count, binds, bind_count, profile, len(profile),
                *map(C.byref, outputs), error, len(error))
            assert not handle and error.value
            assert all(not output.bytes and not output.length for output in outputs)
        refuse(None, 1, layouts, 1)
        refuse(attributes, 32, layouts, 1)
        refuse(attributes, 1, None, 1)
        refuse(attributes, 1, layouts, 32)
        refuse(attributes, 1, None, 0)  # Declared buffer without its layout.
        refuse((VertexAttribute * 2)(attributes[0], attributes[0]), 2, layouts, 1)
        refuse(attributes, 1, (VertexLayout * 2)(layouts[0], layouts[0]), 2)
        for attribute in (VertexAttribute(31, 31, 16, 1), VertexAttribute(0, 0, 16, 1),
                          VertexAttribute(0, 999, 16, 1), VertexAttribute(0, 31, 16, 31)):
            refuse((VertexAttribute * 1)(attribute), 1, layouts, 1)
        for layout in (VertexLayout(31, 32, 1, 1), VertexLayout(1, 32, 999, 1)):
            refuse(attributes, 1, (VertexLayout * 1)(layout), 1)
finally:
    # Pipelines retain their artifacts independently of the input library.
    lib.tegu_air_library_close(library)
    for handle in handles:
        resolved = RenderPush()
        assert push(handle, C.byref(resolved), error, len(error)) == 0, error.value
        assert resolved.fragment_span == 68
        lib.tegu_air_render_pipeline_close(handle)
refused = RenderPush()
C.memset(C.byref(refused), 0xa5, C.sizeof(refused))
assert push(None, C.byref(refused), error, len(error)) == -1 and error.value
assert bytes(refused) == bytes(C.sizeof(refused))
assert push(None, None, error, len(error)) == -1 and error.value
guard = C.create_string_buffer(b'XY', 3)
assert push(None, C.byref(refused), guard, 1) == -1
assert guard.raw == b'\0Y\0'
assert push(None, C.byref(refused), None, 0) == -1
lib.tegu_air_render_pipeline_close(None)
bad_fetch = VertexFetch()
C.memset(C.byref(bad_fetch), 0xa5, C.sizeof(bad_fetch))
assert fetch(None, C.byref(bad_fetch), error, len(error)) == -1 and error.value
assert bytes(bad_fetch) == bytes(C.sizeof(bad_fetch))
assert fetch(None, None, error, len(error)) == -1 and error.value
print('PASS linked vertex/fragment AIR->SPIR-V, R8/RGBA/BGRA, validation, static render push, vertex descriptors, refusal, ownership; no GPU rendering')
