#!/usr/bin/env python3
"""Fork-isolated replay of the installed iOS 16.5 shader corpus through the CURRENT
AIR translator (libtegu_air_bridge) and Mali arch9 compiler (libnord2_mali_runtime).

Host only.  Libraries are snapshotted (sha256) into <out>/libs before use; every case
runs in a forked child with RLIMIT_CPU / RLIMIT_AS and a wall timeout, so an abort,
a crash or a hang is one ledger row, not a dead batch.

compute : every unique kernel AIR module (functions.json, stage kernel), same order of
          calls as runtime_pipeline.inc: tegu_air_pipeline_create -> limits ->
          dispatchable -> tegu_mali_compile, then tegu_mali_compile_compute_v2.
graphics: every graphics-jobs.json pair (captured/inferred/synthetic provenance kept),
          same translation calls as the frozen baseline compile_graphics.py, then
          tegu_mali_compile_generic_pair.
No function-constant value is invented here: functions that declare required constants
are recorded as needing inputs (the corpus_inputs audit owns explicit variants).
"""
import argparse, collections, ctypes as C, hashlib, json, os, re, resource, shutil, signal, struct, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT_DEFAULT = HERE.parent
GPU = HERE.parents[2]
CORPUS = GPU / 'build/ui-corpus-20261025'
sys.path.insert(0, str(GPU / 'metal/tegu'))
import check_fixed_ui_live_variants as live  # noqa: E402

U32 = C.c_uint32
# corpus_inputs audit plan (explicit synthetic_diagnostic:canonical constant values, origin recorded per row)
FC_PLAN = GPU / 'build/compile-stall-20261026/censusall-base/variants.jsonl'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def process_class(src):
    s = src or ''
    if 'QuartzCore.framework' in s:
        return 'coreanimation'
    if re.search(r'/(UIKit|CoreImage|RenderBox|TextInputUI|ChatKit|PencilKit|SpriteKit|SiriUICore|MediaCoreUI|'
                 r'LocalAuthenticationPrivateUI|PassKitUIFoundation|PhotosUICore|ClockKitUI|MusicUI|CameraUI|'
                 r'BiometricKitUI|HealthExperienceUI|FitnessUI|ActivityRingsUI|ActivityAchievementsUI|SeymourUI|'
                 r'MagnifierSupport|AppleMetalGLRenderer|SharingViewService|PridePoster|NanoTimeKit)', s):
        return 'ui_frameworks'
    if 'MetalPerformanceShaders' in s or 'Espresso' in s or 'MLCompute' in s or 'MetalFX' in s:
        return 'mps_ml'
    if '/VideoProcessors/' in s or '/VideoDecoders/' in s or re.search(
            r'(CMCapture|CMImaging|HDRProcessing|CameraColorProcessing|AppleCV|AppleDepth|Portrait|FRC|'
            r'VideoToolbox|VideoProcessing|H10ISP|CinematicFraming|PhotoImaging|MOVStreamIO|CameraEffectsKit|'
            r'Photosensitivity|NeutrinoCore|MediaAnalysis|Vision)', s):
        return 'camera_video'
    if re.search(r'(SceneKit|AvatarKit|VectorKit|CoreRE|VFX|ARKit|RealityKit|RoomPlan|Recon3D|SceneIntelligence|'
                 r'MeasureFoundation|AssetViewer|WeatherMaps|Quagga|TSReading|NanoUniverse|NTK)', s):
        return '3d_maps_ar'
    return 'other'


# ----------------------------------------------------------------- children
class MBlob(C.Structure):
    _fields_ = [('data', C.c_void_p), ('size', C.c_size_t)]


class Binding(C.Structure):
    _fields_ = [(n, U32) for n in ['binding', 'kind', 'stage_mask', 'declared_bytes', 'input_attachment_index']] + [('reserved', U32 * 3)]


class Plan(C.Structure):
    _fields_ = [(n, U32) for n in ['version', 'count', 'reads_framebuffer', 'reserved']] + [('bindings', Binding * 16)]


class LinkedFn(C.Structure):
    _fields_ = [('library', C.c_void_p), ('name', C.c_void_p), ('name_len', C.c_size_t), ('link_name', C.c_void_p), ('link_name_len', C.c_size_t), ('list', U32), ('reserved', U32)]


class ComputeOptions(C.Structure):
    _fields_ = [('version', U32), ('max_call_stack_depth', U32), ('linked', C.c_void_p), ('linked_count', C.c_size_t),
                ('stage_attributes', C.c_void_p), ('stage_attribute_count', C.c_size_t),
                ('stage_layouts', C.c_void_p), ('stage_layout_count', C.c_size_t),
                ('stage_index_type', U32), ('stage_index_buffer_index', U32), ('has_stage_input', U32), ('reserved', U32)]


class Attachment(C.Structure):
    _fields_ = [('format', U32), ('write_mask', U32)]


class Libs:
    def __init__(self, bridge, compiler):
        self.b = live.configure_bridge(bridge)
        b = self.b
        b.tegu_air_pipeline_create.restype = C.c_void_p
        b.tegu_air_pipeline_create.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
                                               C.POINTER(live.Blob), C.POINTER(live.Blob), C.c_void_p, C.c_size_t]
        b.tegu_air_pipeline_limits.argtypes = [C.c_void_p, C.POINTER(U32)]
        self.has_tile = hasattr(b, 'tegu_air_tile_pipeline_create')
        if self.has_tile:
            b.tegu_air_tile_pipeline_create.restype = C.c_void_p
            b.tegu_air_tile_pipeline_create.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(U32), U32, U32, C.c_void_p, C.c_size_t,
                                                        C.POINTER(live.Blob), C.POINTER(live.Blob), C.c_void_p, C.c_size_t]
        self.has_ex = hasattr(b, 'tegu_air_pipeline_create_ex')
        if self.has_ex:
            b.tegu_air_pipeline_create_ex.restype = C.c_void_p
            b.tegu_air_pipeline_create_ex.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(ComputeOptions), C.c_void_p, C.c_size_t,
                                                      C.POINTER(live.Blob), C.POINTER(live.Blob), C.c_void_p, C.c_size_t]
        b.tegu_air_pipeline_dispatchable.argtypes = [C.c_void_p]
        b.tegu_air_pipeline_close.argtypes = [C.c_void_p]
        create = b.tegu_air_render_pipeline_create_with_attachments
        create.restype = C.c_void_p
        base = b.tegu_air_render_pipeline_create_with_vertex_descriptor.argtypes
        create.argtypes = base[:6] + [C.POINTER(Attachment), C.c_size_t] + base[7:]
        self.has_tess = hasattr(b, 'tegu_air_render_pipeline_create_tessellated')
        if self.has_tess:
            ct = b.tegu_air_render_pipeline_create_tessellated
            ct.restype = C.c_void_p
            ct.argtypes = base[:6] + [C.POINTER(Attachment), C.c_size_t] + base[7:]
        cb = b.tegu_air_render_pipeline_create_with_attachment_blends
        cb.restype = C.c_void_p
        cb.argtypes = base[:6] + [C.POINTER(Attachment), C.c_size_t, C.POINTER(live.Blend)] + base[7:]
        self.m = live.configure_compiler(compiler)
        m = self.m
        m.tegu_mali_compile.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(MBlob), C.c_void_p, C.c_size_t]
        m.tegu_mali_compile.restype = C.c_int
        m.tegu_mali_compile_compute_v2.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(MBlob), C.c_void_p, C.c_size_t]
        m.tegu_mali_compile_compute_v2.restype = C.c_int
        m.tegu_mali_compile_generic_pair.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(live.Graphics),
                                                     C.POINTER(live.Graphics), C.POINTER(live.Pair), C.POINTER(Plan), C.c_void_p, C.c_size_t]
        m.tegu_mali_compile_generic_pair.restype = C.c_int
        m.tegu_mali_compile_generic_pair_options.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.c_void_p,
                                                             C.POINTER(live.Graphics), C.POINTER(live.Graphics), C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_size_t]
        m.tegu_mali_compile_generic_pair_options.restype = C.c_int


MTL_TYPES = {'float': 3, 'float2': 4, 'float3': 5, 'float4': 6, 'half': 16, 'half2': 17, 'half3': 18, 'half4': 19,
             'int': 29, 'int2': 30, 'int3': 31, 'int4': 32, 'uint': 33, 'uint2': 34, 'uint3': 35, 'uint4': 36,
             'short': 37, 'short2': 38, 'short3': 39, 'short4': 40, 'ushort': 41, 'ushort2': 42, 'ushort3': 43, 'ushort4': 44,
             'char': 45, 'char2': 46, 'char3': 47, 'char4': 48, 'uchar': 49, 'uchar2': 50, 'uchar3': 51, 'uchar4': 52,
             'bool': 53, 'bool2': 54, 'bool3': 55, 'bool4': 56, 'long': 81, 'ulong': 85}


def specialize(L, handle, fname, consts, err):
    """consts: audit-plan initial_constants rows (explicit origin kept by the caller)."""
    if not consts:
        return handle
    arr = (live.FunctionConstant * len(consts))()
    keep = []
    for i, c in enumerate(consts):
        raw = bytes.fromhex(c['value_hex'])
        nm = c['name'].encode()
        keep.append((C.c_uint8 * max(1, len(nm))).from_buffer_copy(nm or b'\0'))
        arr[i].kind = c['kind'] if 'kind' in c else (1 if c.get('selector', 'name') == 'name' else 0)
        arr[i].type = c['type_raw'] if 'type_raw' in c else MTL_TYPES[c['type']]
        arr[i].index = c['index']
        arr[i].size = len(raw)
        arr[i].name = keep[-1]
        arr[i].name_len = len(nm)
        arr[i].data[:len(raw)] = raw
    fn = fname.encode()
    err.value = b''
    h = L.b.tegu_air_library_specialize(handle, fn, len(fn), arr, len(consts), err, len(err))
    if not h:
        raise RuntimeError('specialize: ' + err.value.decode(errors='replace'))
    return h


LIBCACHE = collections.OrderedDict()


def whole_lib(L, path, err):
    """Whole metallib handle; opened once in the parent (see prefetch) and inherited by forked children."""
    key = str(path)
    if key in LIBCACHE:
        return LIBCACHE[key]
    return open_lib(L, path, err)


def prefetch(L, case):
    paths = set()
    if case['kind'] == 'compute' and (case['entry'].get('constants') or case.get('fc')):
        paths.add(str(CORPUS / case['entry']['library_saved_as']))
    elif case['kind'] == 'graphics' and case['job']['pairing_evidence'] != 'captured_fixed_ui':
        for st in ('vertex', 'fragment'):
            if case['job'][st].get('constants') or case.get('fc'):
                paths.add(str(CORPUS / case['job'][st]['library_saved_as']))
    err = C.create_string_buffer(4096)
    for p in paths:
        if p in LIBCACHE:
            LIBCACHE.move_to_end(p)
            continue
        try:
            LIBCACHE[p] = open_lib(L, p, err)
        except RuntimeError:
            continue
        while len(LIBCACHE) > 3:
            _, h = LIBCACHE.popitem(last=False)
            L.b.tegu_air_library_close(h)


def open_lib(L, path, err):
    raw = Path(path).read_bytes()
    h = L.b.tegu_air_library_open(raw, len(raw), err, len(err))
    if not h:
        raise RuntimeError('library_open: ' + err.value.decode(errors='replace'))
    return h


def run_compute(L, case, profile, out, keep):
    e = case['entry']
    err = C.create_string_buffer(4096)
    row = {'translation': 'not_attempted', 'compile': 'not_attempted'}
    try:
        h = open_lib(L, CORPUS / e['air_saved_as'], err)
    except RuntimeError as x:
        row.update(translation='library_refused', error=str(x))
        return row
    spv, meta = live.Blob(), live.Blob()
    name = e['name'].encode()
    err.value = b''
    p = L.b.tegu_air_pipeline_create(h, name, len(name), profile, len(profile), C.byref(spv), C.byref(meta), err, len(err))
    fc = list(case.get('fc') or [])
    for attempt in range(16):
        if p:
            break
        msg = err.value.decode(errors='replace')
        m = FC_RE.search(msg)
        if not m:
            break
        row.setdefault('first_error', msg)
        if attempt and not complete_constant(e, fc, int(m[1])):
            break
        if not attempt and not fc and not complete_constant(e, fc, int(m[1])):
            break
        row['constants_origin'] = sorted({c['origin'] for c in fc})
        row['constants_supplied'] = len(fc)
        try:
            # production opens the whole metallib (its MTLB function-constant table drives specialization)
            h = specialize(L, whole_lib(L, CORPUS / e['library_saved_as'], err), e['name'], fc, err)
        except RuntimeError as x:
            row.update(translation='refused', error=str(x))
            return row
        spv, meta = live.Blob(), live.Blob()
        err.value = b''
        p = L.b.tegu_air_pipeline_create(h, name, len(name), profile, len(profile), C.byref(spv), C.byref(meta), err, len(err))
    if not p and 'StageInputDescriptor' in err.value.decode(errors='replace') and L.has_ex and case.get('stage_in'):
        # synthetic MTLStageInputOutputDescriptor: declared [[stage_in]] attributes packed in buffer 30,
        # stepping per thread position in grid x (MTLStepFunctionThreadPositionInGridX = 5)
        row.setdefault('first_error', err.value.decode(errors='replace'))
        fmt = {'float': (28, 4), 'float2': (29, 8), 'float3': (30, 12), 'float4': (31, 16), 'half2': (25, 4), 'half4': (27, 8),
               'int': (32, 4), 'int2': (33, 8), 'int3': (34, 12), 'int4': (35, 16), 'uint': (36, 4), 'uint2': (37, 8), 'uint3': (38, 12), 'uint4': (39, 16)}
        attrs, off = [], 0
        for loc, ty in case['stage_in']:
            if ty not in fmt:
                break
            f, size = fmt[ty]
            attrs.append(live.VertexAttribute(loc, f, off, 30))
            off += size
        if len(attrs) == len(case['stage_in']):
            at = (live.VertexAttribute * len(attrs))(*attrs)
            lay = (live.VertexLayout * 1)(live.VertexLayout(30, (off + 3) // 4 * 4, 5, 1))
            opt = ComputeOptions(1, 0, None, 0, C.cast(at, C.c_void_p), len(attrs), C.cast(lay, C.c_void_p), 1, 1, 0, 1, 0)
            spv, meta = live.Blob(), live.Blob()
            err.value = b''
            p = L.b.tegu_air_pipeline_create_ex(h, name, len(name), C.byref(opt), profile, len(profile), C.byref(spv), C.byref(meta), err, len(err))
            row['stage_input_origin'] = 'synthetic_stage_input_descriptor(buffer30,packed,ThreadPositionInGridX)'
    linked, keep = [], []
    for attempt in range(8):
        msg = err.value.decode(errors='replace') if not p else ''
        m = re.search(r'LinkedFunction \{ symbol: "([^"]+)" \}', msg)
        if p or not m or not L.has_ex:
            break
        sym = m[1]
        cands = sorted(n for n in VISIBLE if n.startswith(sym + '_to_'))
        pref = [n for n in cands if n == sym + '_to_' + sym.split('_', 1)[1]] + cands
        if not pref or any(a == sym for a, _ in linked):
            break
        row.setdefault('first_error', msg)
        linked.append((sym, pref[0]))
        arr = (LinkedFn * len(linked))()
        for i, (alias, real) in enumerate(linked):
            lib = whole_lib(L, CORPUS / VISIBLE[real], err)
            a, r = alias.encode(), real.encode()
            keep.extend([a, r])
            arr[i] = LinkedFn(lib, C.cast(C.c_char_p(r), C.c_void_p), len(r), C.cast(C.c_char_p(a), C.c_void_p), len(a), 0, 0)
        opt = ComputeOptions(1, 0, C.cast(arr, C.c_void_p), len(linked), None, 0, None, 0, 0, 0, 0, 0)
        keep.append(arr)
        spv, meta = live.Blob(), live.Blob()
        err.value = b''
        p = L.b.tegu_air_pipeline_create_ex(h, name, len(name), C.byref(opt), profile, len(profile), C.byref(spv), C.byref(meta), err, len(err))
        row['linked_origin'] = 'synthetic_link(specializedName alias -> ' + ','.join(f'{a}={r}' for a, r in linked) + ')'
    if not p and 'TileAttachments' in err.value.decode(errors='replace') and L.has_tile:
        # tile function: synthetic colour attachment 0 = BGRA8Unorm, single sample
        row.setdefault('first_error', err.value.decode(errors='replace'))
        fm = (U32 * 8)(80, 0, 0, 0, 0, 0, 0, 0)
        spv, meta = live.Blob(), live.Blob()
        err.value = b''
        p = L.b.tegu_air_tile_pipeline_create(h, name, len(name), fm, 1, 0, profile, len(profile), C.byref(spv), C.byref(meta), err, len(err))
        row['tile_origin'] = 'synthetic_tile_descriptor(color0=BGRA8Unorm,1 sample)'
    if not p:
        row.update(translation='refused', error=err.value.decode(errors='replace'))
        return row
    s = C.string_at(spv.bytes, spv.length)
    row.update(translation='translated', spirv_sha256=hashlib.sha256(s).hexdigest(), spirv_bytes=len(s))
    if keep:
        (out / 'compute.spv').write_bytes(s)
    (out / 'phase').write_text('mali')
    lim = (U32 * 3)()
    if L.b.tegu_air_pipeline_limits(p, lim):
        row.update(compile='limits_refused')
        return row
    row['limits'] = list(lim)
    d = L.b.tegu_air_pipeline_dispatchable(p)
    row['dispatchable'] = d
    # creation-only storage-texture stand-in: production defers compile to dispatch (real formats);
    # replay still compiles the stand-in SPIR-V with the v2 entry to expose lowering gaps.
    pkg = MBlob()
    err.value = b''
    rc = 1
    if d:
        rc = L.m.tegu_mali_compile(s, len(s), C.byref(pkg), err, len(err))
        row['v1_rc'] = rc
        if rc:
            row['v1_error'] = err.value.decode(errors='replace')
    if rc:
        pkg = MBlob()
        err.value = b''
        rc = L.m.tegu_mali_compile_compute_v2(s, len(s), None, 0, C.byref(pkg), err, len(err))
        row['entry'] = 'v2'
    else:
        row['entry'] = 'v1'
    row['compile_rc'] = rc
    if rc:
        row.update(compile='refused', error=err.value.decode(errors='replace'))
    else:
        code = C.string_at(pkg.data, pkg.size)
        row.update(compile='compiled', package_sha256=hashlib.sha256(code).hexdigest(), package_bytes=pkg.size)
    return row


def run_captured(L, case, profile, out, keep):
    c = case['cap']
    err = C.create_string_buffer(4096)
    row = {'translation': 'not_attempted', 'compile': 'not_attempted'}
    try:
        base = open_lib(L, CORPUS / c['library'], err)
        vh = specialize(L, base, c['vertex'], c['vc'], err)
        fh = specialize(L, base, c['fragment'], c['fc'], err)
        n = max(1, len(c['targets']))
        at = (Attachment * n)(*[Attachment(t[0], t[1]) for t in c['targets']])
        bl = (live.Blend * n)(*[live.Blend(*b) for b in c['blends']])
        attrs = (live.VertexAttribute * max(1, len(c['attrs'])))(*[live.VertexAttribute(*a) for a in c['attrs']])
        lays = (live.VertexLayout * max(1, len(c['layouts'])))(*[live.VertexLayout(*a) for a in c['layouts']])
        blobs = [live.Blob(), live.Blob(), live.Blob()]
        vn, fn = c['vertex'].encode(), c['fragment'].encode()
        err.value = b''
        if c['targets']:
            p = L.b.tegu_air_render_pipeline_create_with_attachment_blends(vh, vn, len(vn), fh, fn, len(fn), at, len(c['targets']), bl,
                attrs, len(c['attrs']), lays, len(c['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
        else:
            p = L.b.tegu_air_render_pipeline_create_with_vertex_descriptor(vh, vn, len(vn), fh, fn, len(fn), c['format'],
                attrs, len(c['attrs']), lays, len(c['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
        if not p:
            raise RuntimeError(err.value.decode(errors='replace'))
        vs, fs = [C.string_at(b.bytes, b.length) for b in blobs[:2]]
    except RuntimeError as x:
        row.update(translation='refused', error=str(x))
        return row
    row.update(translation='translated', vertex_spirv_sha256=hashlib.sha256(vs).hexdigest(), fragment_spirv_sha256=hashlib.sha256(fs).hexdigest())
    job = {'attachments': [{'format': t[0], 'write_mask': t[1]} for t in c['targets']]}
    return compile_pair(L, job, vs, fs, row, err, out)


RAW_TYPES = {}
for _base, _fam, _sz in ((3, 'float', 4), (16, 'float', 2), (29, 'sint', 4), (33, 'uint', 4), (37, 'sint', 2), (41, 'uint', 2),
                         (45, 'sint', 1), (49, 'uint', 1), (53, 'bool', 1), (81, 'sint', 8), (85, 'uint', 8)):
    for _n in range(4):
        RAW_TYPES[_base + _n] = (_fam, _sz, _n + 1)


def complete_constant(entry, rows, index):
    """Add declared constant `index` (MTLB CNST table) with the audit's canonical synthetic value
    (bool/int 1, float 0.5); origin recorded.  Returns False when the entry does not declare it."""
    if any(r['index'] == index for r in rows):
        return False
    for c in entry.get('constants') or []:
        if c['index'] != index or c['type'] not in RAW_TYPES:
            continue
        fam, sz, n = RAW_TYPES[c['type']]
        alt = POLICY[0] == 'alternate'
        if fam == 'float':
            v = 1.0 if alt else 0.5
            one = struct.pack('<e', v) if sz == 2 else struct.pack('<f', v)
        else:
            v = (0 if fam == 'bool' else (-1 if fam == 'sint' else 2)) if alt else 1
            one = v.to_bytes(sz, 'little', signed=(fam == 'sint'))
        rows.append({'index': index, 'name': c['name'], 'type_raw': c['type'], 'kind': 1, 'value_hex': (one * n).hex(),
                     'origin': 'synthetic_diagnostic:%s(completion)' % POLICY[0]})
        return True
    return False


POLICY = ['canonical']


def alternate_rows(rows):
    """The audit tool's 'alternate' policy for the same constants: bool 0, uint 2, sint -1, float 1.0."""
    out = []
    for r in rows or []:
        t = r.get('type', '')
        raw = bytes.fromhex(r['value_hex'])
        if t.startswith('bool') or r.get('type_raw') in (53, 54, 55, 56):
            nv = bytes(len(raw))
        elif t.startswith(('float', 'half')):
            nv = (struct.pack('<e', 1.0) if 'half' in t else struct.pack('<f', 1.0)) * max(1, len(raw) // (2 if 'half' in t else 4))
        elif t.startswith(('int', 'short', 'char', 'long')):
            nv = b'\xff' * len(raw)
        else:
            w = r.get('size', len(raw))
            nv = (2).to_bytes(len(raw), 'little')
        out.append(dict(r, value_hex=nv.hex(), origin='synthetic_diagnostic:alternate'))
    return out


FC_RE = re.compile(r'needs pipeline input: FunctionConstant \{ index: (\d+) \}')


def run_graphics(L, case, profile, out, keep):
    job = case['job']
    err = C.create_string_buffer(4096)
    row = {'translation': 'not_attempted', 'compile': 'not_attempted'}
    fc = {k: list(v or []) for k, v in (case.get('fc') or {}).items()}
    try:
        if job['pairing_evidence'] == 'captured_fixed_ui':
            vs, fs, _ = live.translate(L.b, open_lib(L, CORPUS / job['vertex']['library_saved_as'], err), profile, job['serial'], job['format'])
        else:
            attrs = (live.VertexAttribute * max(1, len(job['attributes'])))(*[live.VertexAttribute(**a) for a in job['attributes']])
            lays = (live.VertexLayout * max(1, len(job['layouts'])))(*[live.VertexLayout(**a) for a in job['layouts']])
            vn, fn = job['vertex']['name'].encode(), job['fragment']['name'].encode()
            for attempt in range(16):
                if fc:
                    # production opens the whole metallib (its MTLB function-constant table drives specialization)
                    try:
                        vh = specialize(L, whole_lib(L, CORPUS / job['vertex']['library_saved_as'], err), job['vertex']['name'], fc.get('vertex'), err)
                        fh = specialize(L, whole_lib(L, CORPUS / job['fragment']['library_saved_as'], err), job['fragment']['name'], fc.get('fragment'), err)
                    except RuntimeError as x:
                        m = re.search(r'missing required constant: (\S+)', str(x))
                        if not m:
                            raise
                        row.setdefault('first_error', str(x))
                        added = False
                        for st in ('vertex', 'fragment'):
                            for c in job[st].get('constants') or []:
                                if c['name'] == m[1]:
                                    fc.setdefault(st, [])
                                    added |= complete_constant(job[st], fc[st], c['index'])
                        if not added:
                            raise
                        continue
                    row['constants_origin'] = sorted({c['origin'] for st in fc.values() for c in st})
                    row['constants_supplied'] = sum(len(v) for v in fc.values())
                else:
                    vh, fh = open_lib(L, CORPUS / job['vertex']['air_saved_as'], err), open_lib(L, CORPUS / job['fragment']['air_saved_as'], err)
                blobs = [live.Blob(), live.Blob(), live.Blob()]
                err.value = b''
                if case.get('tess') and L.has_tess:
                    ta, tl = case['tess']
                    tattrs = (live.VertexAttribute * max(1, len(ta)))(*[live.VertexAttribute(**a) for a in ta])
                    tlays = (live.VertexLayout * max(1, len(tl)))(*[live.VertexLayout(**a) for a in tl])
                    atts = job.get('attachments') or [{'format': job['format'], 'write_mask': 15}]
                    at = (Attachment * len(atts))(*[Attachment(x['format'], x['write_mask']) for x in atts])
                    row['tessellation'] = 'evaluation_stage_as_vertex(synthetic control-point/patch descriptor buffers 30/29)'
                    p = L.b.tegu_air_render_pipeline_create_tessellated(vh, vn, len(vn), fh, fn, len(fn), at, len(atts),
                        tattrs, len(ta), tlays, len(tl), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
                elif 'attachments' in job:
                    at = (Attachment * len(job['attachments']))(*[Attachment(x['format'], x['write_mask']) for x in job['attachments']])
                    p = L.b.tegu_air_render_pipeline_create_with_attachments(vh, vn, len(vn), fh, fn, len(fn), at, len(job['attachments']),
                        attrs, len(job['attributes']), lays, len(job['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
                else:
                    p = L.b.tegu_air_render_pipeline_create_with_vertex_descriptor(vh, vn, len(vn), fh, fn, len(fn), job['format'],
                        attrs, len(job['attributes']), lays, len(job['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
                if p:
                    break
                msg = err.value.decode(errors='replace')
                m = FC_RE.search(msg)
                if not m:
                    raise RuntimeError(msg)
                row.setdefault('first_error', msg)
                idx = int(m[1])
                added = False
                for st in ('vertex', 'fragment'):
                    fc.setdefault(st, [])
                    added |= complete_constant(job[st], fc[st], idx)
                if not added:
                    raise RuntimeError(msg)
            if not p:
                raise RuntimeError(err.value.decode(errors='replace'))
            vs, fs = [C.string_at(b.bytes, b.length) for b in blobs[:2]]
    except RuntimeError as x:
        row.update(translation='refused', error=str(x))
        return row
    row.update(translation='translated', vertex_spirv_sha256=hashlib.sha256(vs).hexdigest(), fragment_spirv_sha256=hashlib.sha256(fs).hexdigest())
    if keep:
        (out / 'vertex.spv').write_bytes(vs)
        (out / 'fragment.spv').write_bytes(fs)
    return compile_pair(L, job, vs, fs, row, err, out)


def compile_pair(L, job, vs, fs, row, err, out=None):
    if out is not None:
        (out / 'phase').write_text('mali')
    v, f = live.Graphics(), live.Graphics()
    pair, plan, varying = C.create_string_buffer(4096), C.create_string_buffer(8192), C.create_string_buffer(8192)
    # production render path (render_pipeline.inc tegu_render_compile_generic_options):
    # DRAW_PARAMETERS|EXTENDED_VARYINGS|THREAD_STORAGE, MULTI_TARGET (options2) for >1 target
    flags = 1 | 8 | 16
    att = job.get('attachments', [])
    active = [i for i, x in enumerate(att) if x.get('write_mask', 15)]
    targets = [x['format'] for x in att]
    if len(active) > 1 and len(att) <= 4 and all(targets):
        # render_pipeline.inc genericTrueMrt: >1 active, all bound, <=4
        opt = (U32 * 12)(2, flags | 32, 0, 0, len(targets), *(targets + [0] * (4 - len(targets))), 0, 0, 0)
    elif len(att) > 1:
        # genericMrt && !genericTrueMrt: SELECT_RT on the main (first active) target
        opt = (U32 * 4)(1, flags | 2, active[0] if active else 0, 0)
    else:
        opt = (U32 * 4)(1, flags, 0, 0)
    row['options_flags'] = opt[1]
    err.value = b''
    rc = L.m.tegu_mali_compile_generic_pair_options(vs, len(vs), fs, len(fs), None, 0, None, 0, opt, C.byref(v), C.byref(f),
                                                     pair, plan, varying, err, len(err))
    row['compile_rc'] = rc
    if rc:
        row.update(compile='refused', error=err.value.decode(errors='replace'))
    else:
        row.update(compile='compiled', vertex_code_sha256=hashlib.sha256(C.string_at(v.code, v.size)).hexdigest(),
                   fragment_code_sha256=hashlib.sha256(C.string_at(f.code, f.size)).hexdigest(), vertex_bytes=v.size, fragment_bytes=f.size,
                   vertex_tls=v.tls, fragment_tls=f.tls)
    return row


def child(L, case, profile, cdir, keep, cpu, mem):
    resource.setrlimit(resource.RLIMIT_CPU, (cpu, cpu + 1))
    resource.setrlimit(resource.RLIMIT_AS, (mem, mem))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    (cdir / 'phase').write_text('translate')
    fd = os.open(str(cdir / 'stderr.log'), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
    os.dup2(fd, 2)
    os.dup2(fd, 1)
    try:
        row = {'compute': run_compute, 'graphics': run_graphics, 'captured': run_captured}[case['kind']](L, case, profile, cdir, keep)
        if case['kind'] == 'compute' and 'emitted no code' in (row.get('error') or '') and row.get('constants_origin'):
            # the canonical synthetic values made the kernel empty: one retry with the alternate policy
            POLICY[0] = 'alternate'
            alt = dict(case, fc=alternate_rows(case.get('fc')))
            r2 = run_compute(L, alt, profile, cdir, keep)
            r2['canonical_result'] = 'emitted no code'
            row = r2
    except BaseException as x:  # noqa
        row = {'translation': 'harness_error', 'compile': 'not_attempted', 'error': repr(x)}
    (cdir / 'row.json').write_text(json.dumps(row))
    os._exit(0)


# ----------------------------------------------------------------- driver
# synthetic diagnostic formats: float class -> blendable n2r formats by component count; integer -> integer formats
FMT = {('float', 1): 25, ('float', 2): 65, ('float', 3): 115, ('float', 4): 80, ('half', 1): 25, ('half', 2): 65, ('half', 3): 115, ('half', 4): 115,
       ('uint', 1): 53, ('uint', 2): 103, ('uint', 4): 123, ('int', 1): 54, ('int', 2): 104, ('int', 4): 124,
       ('ushort', 1): 23, ('ushort', 2): 63, ('ushort', 4): 113, ('short', 1): 24, ('short', 2): 64, ('short', 4): 114,
       ('uchar', 1): 13, ('uchar', 2): 33, ('uchar', 4): 73, ('char', 1): 14, ('char', 2): 34, ('char', 4): 74}


def stage_in_attrs(resources_row):
    out = []
    for line in resources_row.get('interfaces', []) + resources_row.get('metadata', []):
        m = re.search(r'!"air.stage_in", !"air.location_index", i32 (\d+), i32 \d+, !"air.arg_type_name", !"([a-z0-9]+)"', line)
        if m:
            out.append((int(m[1]), m[2]))
    return sorted(set(out))


VFMT = {'float': (28, 4), 'float2': (29, 8), 'float3': (30, 12), 'float4': (31, 16), 'half': (53, 2), 'half2': (25, 4), 'half4': (27, 8),
        'int': (32, 4), 'int2': (33, 8), 'int3': (34, 12), 'int4': (35, 16), 'uint': (36, 4), 'uint2': (37, 8), 'uint3': (38, 12), 'uint4': (39, 16),
        'packed_float3': (30, 12), 'uchar4': (3, 4), 'ushort2': (13, 4)}


def tess_descriptor(resources_row):
    """Synthetic vertex descriptor of a post-tessellation vertex function: control-point attributes
    packed in buffer 30 (MTLVertexStepFunctionPerPatchControlPoint = 4), patch attributes packed in
    buffer 29 (PerPatch = 3). None when the function is not post-tessellation."""
    md = resources_row.get('metadata', [])
    nodes = {}
    for line in md:
        m = re.match(r'^!(\d+) = (.*)$', line)
        if m:
            nodes[m[1]] = m[2]
    if not any('"air.patch"' in l for l in md):
        return None
    cps, patch = [], []
    for line in md:
        if '"air.patch_control_point_input"' in line:
            for ref in re.findall(r'!(\d+)', line.split('"air.patch_control_point_input"', 1)[1]):
                f = re.search(r'"air.location_index", i32 (\d+), i32 \d+, !"air.arg_type_name", !"([a-z_0-9]+)"', nodes.get(ref, ''))
                if f:
                    cps.append((int(f[1]), f[2]))
        f = re.search(r'"air.patch_input", !"air.location_index", i32 (\d+), i32 \d+, !"air.arg_type_name", !"([a-z_0-9]+)"', line)
        if f:
            patch.append((int(f[1]), f[2]))
    attrs, layouts = [], []
    for buf, step, group in ((30, 4, cps), (29, 3, patch)):
        off = 0
        for loc, ty in sorted(set(group)):
            if ty not in VFMT:
                return None
            fmt, size = VFMT[ty]
            attrs.append({'index': loc, 'format': fmt, 'offset': off, 'buffer_index': buf})
            off += (size + 3) // 4 * 4
        if group:
            layouts.append({'index': buf, 'stride': max(4, off), 'step_function': step, 'step_rate': 1})
    return attrs, layouts


def declared_targets(resources_row):
    out = {}
    for line in resources_row.get('interfaces', []):
        m = re.search(r'!"air.render_target", i32 (\d+), i32 \d+[^\n]*?air.arg_type_name", !"([a-z]+)(\d?)"', line)
        if m:
            out[int(m[1])] = (m[2], int(m[3] or 1))
            continue
        # framebuffer read (fragment argument): the attachment must be bound too
        m = re.search(r'^!\d+ = !\{i32 \d+, !"air.render_target", i32 (\d+), !"air.arg_type_name", !"([a-z]+)(\d?)"', line)
        if m and int(m[1]) not in out:
            out[int(m[1])] = (m[2], int(m[3] or 1))
    return out


def parse_spec(spec):
    """vspec/fspec: name|count|kind:type:index:name:hex;... -> constant rows (captured on device)."""
    name, _, rest = spec.split('|', 2)
    rows = []
    for item in rest.split(';'):
        if not item:
            continue
        k, t, i, n, h = item.split(':')
        rows.append({'kind': int(k), 'type_raw': int(t), 'index': int(i), 'name': n, 'value_hex': h, 'origin': 'captured_device'})
    return name, rows


def captured_cases():
    out = []
    path = GPU / 'build/pipeline-corpus/corpus.txt'
    if not path.exists():
        return out
    libs = {}
    for e in json.load(open(CORPUS / 'functions.json'))['entries']:
        libs[e['library_sha256']] = e
    for n, line in enumerate(open(path)):
        f = dict(x.split('=', 1) for x in line.split() if '=' in x)
        vname, vrows = parse_spec(f['vspec'])
        fname, frows = parse_spec(f['fspec'])
        lib = libs[f['vlib']]
        tg = [tuple(map(int, t.split('/'))) for t in f.get('targets', '').rstrip(',').split(',') if t]
        bl = [tuple(map(int, t.split('/'))) for t in f.get('blends', '').rstrip(',').split(',') if t]
        at = [tuple(map(int, t.split('/'))) for t in f.get('attrs', '').rstrip(',').split(',') if t]
        la = [tuple(map(int, t.split('/'))) for t in f.get('layouts', '').rstrip(',').split(',') if t]
        out.append({'id': 'p-%03d' % n, 'kind': 'captured', 'name': f['vertex'] + ' + ' + f['fragment'], 'library_source': lib['library_source'],
                    'provenance': 'captured_device_pipeline(corpus.txt TPC_MISS)', 'constants_declared': len(vrows) + len(frows),
                    'cap': {'library': lib['library_saved_as'], 'vertex': f['vertex'], 'fragment': f['fragment'], 'vc': vrows, 'fc': frows,
                            'targets': tg, 'blends': bl, 'attrs': at, 'layouts': la, 'format': int(f.get('format', 0))}})
    return out


VISIBLE = {}


def build_cases(kind, filt):
    cases = []
    resources = {}
    for line in open(CORPUS / 'resources.jsonl'):
        r = json.loads(line)
        resources[r['air_sha256']] = r
    funcs = json.load(open(CORPUS / 'functions.json'))['entries']
    for e in funcs:
        if e['stage'] == 'visible':
            VISIBLE.setdefault(e['name'], e['library_saved_as'])
    fcc, fcg = {}, {}
    if FC_PLAN.exists():
        for line in open(FC_PLAN):
            v = json.loads(line)
            if v['kind'] == 'compute_fc':
                fcc[v['kernel']['sha256']] = v['initial_constants'].get('kernel') or []
            elif v['kind'] == 'graphics_fc':
                fcg[v['case']] = v['initial_constants']
    # captured donor constants: values a device actually used for the same QuartzCore constant names
    donor = {}
    cpath = GPU / 'build/pipeline-corpus/corpus.txt'
    if cpath.exists():
        for line in open(cpath):
            f = dict(x.split('=', 1) for x in line.split() if '=' in x)
            for key in ('vspec', 'fspec'):
                for r in parse_spec(f[key])[1]:
                    donor.setdefault(r['name'], r)
    if kind in ('compute', 'all'):
        seen = {}
        for e in funcs:
            if e['stage'] == 'kernel' and e['air_sha256'] not in seen:
                seen[e['air_sha256']] = e
        for e in seen.values():
            cases.append({'id': 'c-' + e['air_sha256'][:24], 'kind': 'compute', 'entry': e, 'name': e['name'], 'library_source': e['library_source'],
                          'provenance': 'installed_air_default_constants_none_supplied', 'constants_declared': len(e.get('constants') or []),
                          'fc': fcc.get(e['air_sha256']), 'stage_in': stage_in_attrs(resources.get(e['air_sha256'], {}))})
    if kind in ('graphics', 'all'):
        for j in json.load(open(CORPUS / 'graphics-jobs.json'))['jobs']:
            prov = {'captured_fixed_ui': 'captured_constants_layout+synthetic_format', 'captured_donor_recipe': 'captured_donor_descriptor',
                    'abi_candidate': 'inferred_pairing+synthetic_descriptor'}[j['pairing_evidence']]
            if j['pairing_evidence'] == 'abi_candidate' and 'attachments' not in j:
                t = declared_targets(resources.get(j['fragment']['air_sha256'], {}))
                if t and t != {0: ('float', 4)} and t != {0: ('half', 4)}:
                    j = dict(j)
                    # undeclared slots below the highest target: bound but masked, as render_pipeline.inc does (fmt?fmt:80, mask 0)
                    j['attachments'] = [{'format': FMT.get(t[i], 0), 'write_mask': 15} if i in t else {'format': 80, 'write_mask': 0}
                                        for i in range(max(t) + 1)]
                    prov += '+declared_target_synthetic_formats'

            cases.append({'id': 'g-' + j['id'], 'kind': 'graphics', 'job': j, 'name': j['vertex']['name'] + ' + ' + j['fragment']['name'],
                          'library_source': j['vertex']['library_source'], 'provenance': prov,
                          'constants_declared': len(j['vertex'].get('constants') or []) + len(j['fragment'].get('constants') or []),
                          'fc': fcg.get(j['id']), 'tess': tess_descriptor(resources.get(j['vertex']['air_sha256'], {}))})
            if 'QuartzCore.framework' in j['vertex']['library_source'] and j['pairing_evidence'] == 'abi_candidate':
                cap = {}
                for st in ('vertex', 'fragment'):
                    rows = [dict(donor[c['name']], index=c['index'], origin='captured_donor(corpus.txt)')
                            for c in j[st].get('constants') or [] if c['name'] in donor and donor[c['name']]['type_raw'] == c['type']]
                    if rows:
                        cap[st] = rows
                if cap:
                    cases[-1]['fc'] = cap
                    cases[-1]['provenance'] += '+captured_donor_constants'
    if kind in ('captured', 'all'):
        cases += captured_cases()
    for c in cases:
        c['class'] = process_class(c['library_source'])
    order = ['coreanimation', 'ui_frameworks', 'camera_video', 'mps_ml', '3d_maps_ar', 'other']
    cases.sort(key=lambda c: (order.index(c['class']), c['kind'], c['library_source'], c['id']))
    if filt:
        ids = set(x.strip() for x in open(filt) if x.strip())
        cases = [c for c in cases if c['id'] in ids]
    return cases


def classify_crash(log):
    m = re.search(r'([A-Za-z0-9_./-]+\.(?:c|cpp|h|rs)):(\d+)[^\n]*?(?:Assertion|assert|panicked)[^\n]*', log)
    m2 = re.search(r"Assertion `([^']+)' failed", log)
    m3 = re.search(r'([a-z_0-9]+\.c):(\d+): ([a-z_0-9]+): Assertion', log)
    if m3:
        return f'{m3[1]}:{m3[2]} {m3[3]}: ' + (m2[1] if m2 else '')
    if m:
        return m[0][:200]
    lines = [x for x in log.strip().splitlines() if x.strip()]
    return lines[-1][:200] if lines else ''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bridge', type=Path, default=GPU / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so')
    ap.add_argument('--compiler', type=Path, default=GPU / 'build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so')
    ap.add_argument('--profile', type=Path, default=GPU / 'build/tegu-frontend/tegu-air-profile.h')
    ap.add_argument('--run', required=True, help='run name under <out>/runs')
    ap.add_argument('--out', type=Path, default=OUT_DEFAULT)
    ap.add_argument('--kind', choices=['compute', 'graphics', 'captured', 'all'], default='all')
    ap.add_argument('--filter', help='file of case ids')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--cpu', type=int, default=30)
    ap.add_argument('--wall', type=int, default=45)
    ap.add_argument('--mem', type=int, default=4 << 30)
    ap.add_argument('--keep-spirv', action='store_true')
    ap.add_argument('--limit', type=int)
    a = ap.parse_args()
    run = a.out / 'runs' / a.run
    (run / 'cases').mkdir(parents=True, exist_ok=True)
    libs = run / 'libs'
    libs.mkdir(exist_ok=True)
    ident = {}
    for k, p in (('bridge', a.bridge), ('compiler', a.compiler), ('profile', a.profile)):
        h = sha(p)
        dst = libs / p.name
        if dst.exists() and sha(dst) != h:
            sys.exit(f'{k} differs from snapshot in {run}; use a new --run')
        if not dst.exists():
            shutil.copyfile(p, dst)
        ident[k] = {'source': str(p), 'sha256': h}
    (run / 'identity.json').write_text(json.dumps(ident, indent=2))
    profile = live.profile_bytes(libs / a.profile.name)
    L = Libs(libs / a.bridge.name, libs / a.compiler.name)
    cases = build_cases(a.kind, a.filter)
    if a.limit:
        cases = cases[:a.limit]
    done = {}
    res = run / 'results.jsonl'
    if res.exists():
        for line in open(res):
            r = json.loads(line)
            done[r['id']] = r
    todo = [c for c in cases if c['id'] not in done]
    print(f'{len(cases)} cases, {len(done)} done, {len(todo)} to run', flush=True)
    stream = open(res, 'a')
    running = {}
    it = iter(todo)
    finished = 0
    t0 = time.time()

    def launch(c):
        prefetch(L, c)
        cdir = run / 'cases' / c['id']
        cdir.mkdir(exist_ok=True)
        for f in ('row.json',):
            if (cdir / f).exists():
                (cdir / f).unlink()
        pid = os.fork()
        if pid == 0:
            try:
                os.nice(0)
                child(L, c, profile, cdir, a.keep_spirv, a.cpu, a.mem)
            finally:
                os._exit(3)
        running[pid] = (c, time.monotonic(), cdir)

    def finish(pid, status):
        nonlocal finished
        c, start, cdir = running.pop(pid)
        row = {k: v for k, v in c.items() if k not in ('entry', 'job', 'fc', 'cap', 'stage_in', 'tess')}
        if c['kind'] == 'compute':
            row['air_sha256'] = c['entry']['air_sha256']
        elif c['kind'] == 'graphics':
            row.update(vertex_air_sha256=c['job']['vertex']['air_sha256'], fragment_air_sha256=c['job']['fragment']['air_sha256'],
                       pairing_evidence=c['job']['pairing_evidence'])
        log = (cdir / 'stderr.log').read_text(errors='replace') if (cdir / 'stderr.log').exists() else ''
        if status == 'timeout':
            row.update(translation=row.get('translation', 'unknown'), outcome='timeout')
        if (cdir / 'row.json').exists():
            row.update(json.load(open(cdir / 'row.json')))
        if (cdir / 'phase').exists():
            row['phase'] = (cdir / 'phase').read_text()
        if status == 'timeout':
            row['outcome'] = 'timeout'
        elif os.WIFSIGNALED(status):
            sig = os.WTERMSIG(status)
            row['signal'] = sig
            row['outcome'] = 'timeout' if sig in (signal.SIGXCPU, signal.SIGKILL) else 'crash'
            row['crash'] = classify_crash(log)
            row.setdefault('translation', 'unknown')
        elif not (cdir / 'row.json').exists():
            row['outcome'] = 'crash'
            row['crash'] = 'exit %d: ' % os.WEXITSTATUS(status) + classify_crash(log)
        else:
            row['outcome'] = 'ok' if row.get('compile') == 'compiled' else 'refused'
        row['seconds'] = round(time.monotonic() - start, 3)
        if row['outcome'] in ('crash', 'timeout'):
            row['log_tail'] = log[-1500:]
        stream.write(json.dumps(row) + '\n')
        stream.flush()
        finished += 1
        if finished % 200 == 0:
            print(finished, round(time.time() - t0), flush=True)

    def package_temp():
        try:
            for h in Path('/sys/class/hwmon').iterdir():
                if (h / 'name').read_text().strip() == 'coretemp':
                    return int((h / 'temp1_input').read_text()) / 1000
        except OSError:
            pass
        return 0

    pending = True
    last_t, hot = 0, False
    while running or pending:
        if time.monotonic() - last_t > 2:
            last_t = time.monotonic()
            t = package_temp()
            if t >= 92 and not hot:
                print(f'thermal pause at {t} C', flush=True)
            hot = t >= 92 or (hot and t > 85)
        while pending and not hot and len(running) < a.jobs:
            c = next(it, None)
            if c is None:
                pending = False
                break
            launch(c)
        try:
            pid, status = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            pid = 0
        if pid:
            finish(pid, status)
            continue
        now = time.monotonic()
        for p, (c, start, _) in list(running.items()):
            if now - start > a.wall:
                os.kill(p, signal.SIGKILL)
                os.waitpid(p, 0)
                finish(p, 'timeout')
        time.sleep(0.01)
    stream.close()
    print('done', finished, round(time.time() - t0), flush=True)


if __name__ == '__main__':
    main()
