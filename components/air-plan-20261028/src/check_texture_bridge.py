#!/usr/bin/env python3
"""Translate the authored textured render AIR through the real C ABI bridge.

Host-only. Records linked VS/FS SPIR-V, disassembly, metadata, static push and
vertex-fetch plan, and asserts that the existing native render gates still
REFUSE texture resources. No GPU claim. Writes only into --work (must not exist).
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import re
import subprocess


class Blob(C.Structure):
    _fields_ = [('bytes', C.c_void_p), ('length', C.c_size_t)]


class RenderPush(C.Structure):
    _fields_ = [('version', C.c_uint32), ('vertex_span', C.c_uint32),
                ('fragment_span', C.c_uint32), ('vertex', C.c_ubyte * 128),
                ('fragment', C.c_ubyte * 128)]


class VertexAttribute(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('index', 'format', 'offset', 'buffer_index')]


class VertexLayout(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('index', 'stride', 'step_function', 'step_rate')]


class FetchAttribute(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in
                ('location', 'buffer_index', 'format', 'offset', 'stride', 'input_rate', 'divisor')]


class VertexFetch(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('version', 'mask', 'count', 'reserved')] + \
               [('attributes', FetchAttribute * 31)]


here = Path(__file__).resolve().parent
repo = here.parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bridge', type=Path,
               default=repo / 'research/tegu-gpu/air-bridge/target/release/libtegu_air_bridge.so')
p.add_argument('--translator', type=Path, default=repo / 'research/tegu-gpu/metal2vulkan')
p.add_argument('--fixture', type=Path, default=here / 'texture_probe.ll')
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=False)

air = a.work / 'render.air'
subprocess.run(['llvm-as', str(a.fixture), '-o', str(air)], check=True)
(a.work / 'render.ll').write_text(a.fixture.read_text())
data = air.read_bytes()
profile = (a.translator / 'crates/metal2vulkan-validation/profiles/vk12-bare.json').read_bytes()
VS, FS = b'texture_vertex', b'texture_fragment'

lib = C.CDLL(str(a.bridge.resolve()))
lib.tegu_air_library_open.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
lib.tegu_air_library_open.restype = C.c_void_p
lib.tegu_air_library_close.argtypes = [C.c_void_p]
lib.tegu_air_render_pipeline_close.argtypes = [C.c_void_p]
lib.tegu_air_blob_free.argtypes = [C.POINTER(Blob)]
create = lib.tegu_air_render_pipeline_create_with_vertex_descriptor
create.restype = C.c_void_p
create.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p, C.c_size_t,
                   C.c_uint32, C.POINTER(VertexAttribute), C.c_size_t,
                   C.POINTER(VertexLayout), C.c_size_t, C.c_void_p, C.c_size_t,
                   C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob), C.c_void_p, C.c_size_t]
checks = {}
for name in ('native_check', 'native_check_with_vertex_fetch'):
    fn = getattr(lib, 'tegu_air_render_pipeline_' + name)
    fn.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_size_t]
    fn.restype = C.c_int32
    checks[name] = fn
push = lib.tegu_air_render_pipeline_push
push.argtypes = [C.c_void_p, C.POINTER(RenderPush), C.c_void_p, C.c_size_t]
push.restype = C.c_int32
fetch = lib.tegu_air_render_pipeline_vertex_fetch
fetch.argtypes = [C.c_void_p, C.POINTER(VertexFetch), C.c_void_p, C.c_size_t]
fetch.restype = C.c_int32

# Metal buffer1, stride32: attribute7 float4 @0, attribute8 float4 @16 (MTLVertexFormatFloat4=31).
attributes = (VertexAttribute * 2)(VertexAttribute(7, 31, 0, 1), VertexAttribute(8, 31, 16, 1))
layouts = (VertexLayout * 1)(VertexLayout(1, 32, 1, 1))
error = C.create_string_buffer(4096)
library = lib.tegu_air_library_open(data, len(data), error, len(error))
assert library, error.value


def disassemble(path):
    out = subprocess.run(['spirv-dis', '--raw-id', str(path)], check=True,
                         capture_output=True, text=True).stdout
    path.with_suffix('.spvasm').write_text(out)
    return out


summary = {'fixture': str(a.fixture), 'formats': {}}
handles = []
try:
    for fmt in (70, 80):
        outputs = [Blob(), Blob(), Blob()]
        handle = create(library, VS, len(VS), library, FS, len(FS), fmt, attributes, 2,
                        layouts, 1, profile, len(profile), *map(C.byref, outputs), error, len(error))
        assert handle and not error.value, error.value
        handles.append(handle)
        meta = json.loads(C.string_at(outputs[2].bytes, outputs[2].length))
        (a.work / f'{fmt}-metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
        result = {}
        text = {}
        for stage, output in zip(('vertex', 'fragment'), outputs[:2]):
            binary = C.string_at(output.bytes, output.length)
            assert binary[:4] == bytes.fromhex('03022307')
            path = a.work / f'{fmt}-{stage}.spv'
            path.write_bytes(binary)
            subprocess.run(['spirv-val', '--target-env', 'vulkan1.2', str(path)], check=True)
            text[stage] = disassemble(path)
        for output in outputs:
            lib.tegu_air_blob_free(C.byref(output))

        # Resource contract as actually emitted.
        sets = meta['layout']['sets']
        assert len(sets) == 1 and sets[0]['set'] == 0 and len(sets[0]['bindings']) == 1, sets
        b = sets[0]['bindings'][0]
        src = b['source']['CombinedTextureSampler']
        assert (b['binding'], b['ty'], b['count'], b['stages'], b['flags'], b['immutable_sampler']) == \
               (0, 1, 1, 16, 0, None), b  # COMBINED_IMAGE_SAMPLER, FRAGMENT only
        assert src['texture'] == {'class': 'Texture', 'index': 0, 'stage': 'Fragment'}, src
        assert src['sampler']['sampler'] == {'Slot': 0} and not src['sampler']['compare'], src
        fs = text['fragment']
        samples = re.findall(r'= (OpImage\w*Sample\w*) .*', fs)
        assert samples, 'no OpImageSample* in fragment'
        sampled = re.findall(r'%(\d+) = OpTypeImage .*', fs)
        decorations = [l.strip() for l in fs.splitlines() if 'OpDecorate' in l and
                       ('DescriptorSet' in l or 'Binding' in l)]
        assert 'OpTypeSampledImage' not in text['vertex'] and 'OpImageSample' not in text['vertex']
        caps = sorted(set(re.findall(r'OpCapability (\w+)', fs)) | set(re.findall(r'OpCapability (\w+)', text['vertex'])))
        result.update({
            'binding': b, 'fragment_sample_ops': [l.strip() for l in fs.splitlines() if 'OpImage' in l and 'Sample' in l],
            'fragment_image_types': [l.strip() for l in fs.splitlines() if 'OpTypeImage' in l or 'OpTypeSampledImage' in l],
            'fragment_resource_decorations': decorations,
            'spirv_capabilities_text': caps,
            'requirements': meta['requirements'], 'variant_bits': meta['variant_bits'],
            'dependencies': meta['dependencies'], 'stages': meta['stages'],
        })

        fetched = VertexFetch()
        assert fetch(handle, C.byref(fetched), error, len(error)) == 0, error.value
        rows = [{n: getattr(r, n) for n, _ in FetchAttribute._fields_} for r in fetched.attributes[:fetched.count]]
        assert (fetched.version, fetched.mask, fetched.count) == (1, 3, 2), (fetched.mask, fetched.count)
        for i, r in enumerate(rows):
            assert tuple(r.values()) == (i, 1, 109, 16 * i, 32, 0, 1), rows
        (a.work / f'{fmt}-vertex-fetch.json').write_text(json.dumps(
            {'version': fetched.version, 'mask': fetched.mask, 'count': fetched.count, 'attributes': rows}, indent=2) + '\n')

        resolved = RenderPush()
        rc = push(handle, C.byref(resolved), error, len(error))
        pushed = {'rc': rc, 'error': error.value.decode(), 'version': resolved.version,
                  'vertex_span': resolved.vertex_span, 'fragment_span': resolved.fragment_span,
                  'vertex': bytes(resolved.vertex).hex(), 'fragment': bytes(resolved.fragment).hex()}
        (a.work / f'{fmt}-push.json').write_text(json.dumps(pushed, indent=2) + '\n')
        result['push'] = pushed

        # The native gates must keep refusing texture resources.
        gates = {}
        for name, fn in checks.items():
            rc = fn(handle, fmt, error, len(error))
            gates[name] = {'rc': rc, 'error': error.value.decode()}
            expected = (b'native render static vertex input unsupported' if name == 'native_check'
                        else b'native render unbound resources unsupported')
            assert rc == -1 and error.value == expected, (name, error.value)
        result['native_gates'] = gates
        summary['formats'][fmt] = result
finally:
    lib.tegu_air_library_close(library)
    for handle in handles:
        lib.tegu_air_render_pipeline_close(handle)

(a.work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print('PASS textured AIR -> linked SPIR-V (spirv-val vulkan1.2), combined set0/binding0 = '
      'fragment texture0+sampler0, native gates refuse; no GPU rendering')
