#!/usr/bin/env python3
"""QuartzCore compositor gate replay (host-only, workers=1, bounded isolated children).

Owner: metal-shader-corpus (Claude).  Does not touch production files, the corpus_inputs tool
or its evidence.  Nothing here executes on a GPU; "compiled" means: the AIR->SPIR-V bridge
translated the pair AND tegu_mali_compile_generic_pair_options (the entry render_pipeline.inc
tegu_render_compile_generic_options really calls, flags DRAW_PARAMETERS|EXTENDED_VARYINGS,
SELECT_RT rt0 for the dual-attachment MRT shape) returned 0 AND the vertex-fetch coverage check
of tegu_render_generic_prepare passes.  That is exactly the set of conditions whose failure makes
the NORD2_JM_TRANSPORT build return a nil MTLRenderPipelineState (QuartzCore then aborts).

Parent: python3 gate_replay.py --out DIR [--only SUBSTR]   (resumable: results.jsonl)
Child : python3 gate_replay.py --child SPEC.json            (run under prlimit AS=2GiB CPU=12s)
"""
import argparse, ctypes as C, hashlib, json, os, re, subprocess, sys, time
from pathlib import Path

HERE = Path("$NORD2_WORK/gpu/build/ui-corpus-20261025/compositor-gate-20261025")
CORPUS = HERE.parent
REPO = CORPUS.parents[2]            # .../work/gpu is REPO/gpu ; CORPUS = REPO/gpu/build/ui-corpus-20261025
GPU = CORPUS.parents[1]
sys.path.insert(0, str(GPU / "metal/tegu"))

QC_LIB_SHA = "11c74cc42ce5137b73d44d427f75764f886656d4ddb374422d56b609ed2762d9"
QC_METALLIB = CORPUS / "libraries" / (QC_LIB_SHA + ".metallib")
SNAP = GPU / "build/deepseek-parallel-20261025/workers/corpus_inputs/sources/742cdf3d-06a6c6ee"
COMPILER = Path(os.environ.get("TEGU_GATE_COMPILER", str(SNAP / "libnord2_mali_runtime.so")))
BRIDGE = SNAP / "libtegu_air_bridge.so"
PROFILE = CORPUS / "baseline/tegu-air-profile.h"
EXP_COMPILER = os.environ.get("TEGU_GATE_COMPILER_SHA", "742cdf3d8d10364402ff3f1c7896be43a884e93f95684edf891b206db1c7ffbb")
EXP_BRIDGE = "06a6c6ee4e45f3b5d2bb4c4a7462d2054f81db5f8f9df564acfb4f1eac158b06"

U32 = C.c_uint32
OPT_DRAW, OPT_RT, OPT_TILE, OPT_EXT = 1, 2, 4, 8
PREMULT_OVER = dict(enabled=1, rgb_op=0, rgb_src=1, rgb_dst=5, alpha_op=0, alpha_src=1, alpha_dst=5)
NO_BLEND = dict(enabled=0, rgb_op=0, rgb_src=1, rgb_dst=0, alpha_op=0, alpha_src=1, alpha_dst=0)


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


# ------------------------------------------------------------------ child
def child_main(spec_path):
    import check_fixed_ui_live_variants as live
    spec = json.loads(Path(spec_path).read_text())
    assert sha(COMPILER) == EXP_COMPILER and sha(BRIDGE) == EXP_BRIDGE, "library hash drift"

    Blob, VA, VL = live.Blob, live.VertexAttribute, live.VertexLayout

    class Att(C.Structure):
        _fields_ = [("format", U32), ("write_mask", U32)]

    Blend = live.Blend

    class FetchAttr(C.Structure):
        _fields_ = [(n, U32) for n in ("location", "buffer_index", "format", "offset", "stride",
                                       "input_rate", "divisor")]

    class Fetch(C.Structure):
        _fields_ = [("version", U32), ("mask", U32), ("count", U32), ("reserved", U32),
                    ("attributes", FetchAttr * 31)]

    class Binding(C.Structure):
        _fields_ = [(n, U32) for n in ("binding", "kind", "stage_mask", "declared_bytes",
                                       "input_attachment_index")] + [("reserved", U32 * 3)]

    class Plan(C.Structure):
        _fields_ = [(n, U32) for n in ("version", "count", "reads_framebuffer", "reserved")] + [
            ("bindings", Binding * 16)]

    class Options(C.Structure):
        _fields_ = [(n, U32) for n in ("version", "flags", "rt", "reserved")]

    class VSlot(C.Structure):
        _fields_ = [(n, U32) for n in ("location", "offset", "components", "alu_type",
                                       "interpolation", "reserved")]

    class VInfo(C.Structure):
        _fields_ = [(n, U32) for n in ("version", "count", "vs_mask", "fs_mask", "flat_mask",
                                       "noperspective_mask", "generic_size_B", "uses_flat_shading")] + [
            ("slots", VSlot * 16)]

    br = live.configure_bridge(BRIDGE)
    common = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p, C.c_size_t]
    f1 = br.tegu_air_render_pipeline_create_with_vertex_descriptor
    f1.restype = C.c_void_p
    f1.argtypes = common + [U32, C.POINTER(VA), C.c_size_t, C.POINTER(VL), C.c_size_t, C.c_void_p, C.c_size_t,
                            C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob), C.c_void_p, C.c_size_t]
    f2 = br.tegu_air_render_pipeline_create_with_blend
    f2.restype = C.c_void_p
    f2.argtypes = common + [U32, C.POINTER(VA), C.c_size_t, C.POINTER(VL), C.c_size_t, C.POINTER(Blend),
                            C.c_void_p, C.c_size_t, C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob),
                            C.c_void_p, C.c_size_t]
    f3 = br.tegu_air_render_pipeline_create_with_attachment_blends
    f3.restype = C.c_void_p
    f3.argtypes = common + [C.POINTER(Att), C.c_size_t, C.POINTER(Blend), C.POINTER(VA), C.c_size_t,
                            C.POINTER(VL), C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(Blob),
                            C.POINTER(Blob), C.POINTER(Blob), C.c_void_p, C.c_size_t]
    br.tegu_air_render_pipeline_vertex_fetch.restype = C.c_int32
    br.tegu_air_render_pipeline_vertex_fetch.argtypes = [C.c_void_p, C.POINTER(Fetch), C.c_void_p, C.c_size_t]

    mc = C.CDLL(str(COMPILER))
    pair_args = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
    mc.tegu_mali_compile_generic_pair_options.restype = C.c_int
    mc.tegu_mali_compile_generic_pair_options.argtypes = pair_args + [
        C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(Options), C.POINTER(live.Graphics),
        C.POINTER(live.Graphics), C.POINTER(live.Pair), C.POINTER(Plan), C.POINTER(VInfo),
        C.c_void_p, C.c_size_t]
    mc.tegu_mali_compile_generic_pair.restype = C.c_int
    mc.tegu_mali_compile_generic_pair.argtypes = pair_args + [
        C.POINTER(live.Graphics), C.POINTER(live.Graphics), C.POINTER(live.Pair), C.POINTER(Plan),
        C.c_void_p, C.c_size_t]
    mc.tegu_mali_graphics_free.argtypes = [C.POINTER(live.Graphics)]

    err = C.create_string_buffer(4096)
    profile = live.profile_bytes(PROFILE)
    pbuf = C.create_string_buffer(profile)
    raw = QC_METALLIB.read_bytes()
    base = br.tegu_air_library_open(raw, len(raw), err, len(err))
    if not base:
        print("RESULT -1 " + json.dumps({"stage": "library_open", "error": err.value.decode(errors="replace")}), flush=True)
        return 3

    def blend_struct(d, mask):
        return Blend(1, d["enabled"], d["rgb_op"], d["rgb_src"], d["rgb_dst"], d["alpha_op"],
                     d["alpha_src"], d["alpha_dst"], mask)

    for idx, c in enumerate(spec["combos"]):
        print("START %d" % idx, flush=True)
        res = {"combo": c["tag"]}
        t0 = time.monotonic()
        vn, fn = spec["vertex"].encode(), spec["fragment"].encode()
        attrs = (VA * len(c["attributes"]))(*[VA(a["index"], a["format"], a["offset"], a["buffer_index"]) for a in c["attributes"]])
        lays = (VL * len(c["layouts"]))(*[VL(l["index"], l["stride"], l["step_function"], l["step_rate"]) for l in c["layouts"]])
        out = (Blob * 3)()
        err.value = b""
        pipeline = None
        spv = None
        if c["kind"] == "fixed_serial":
            try:
                v, f, meta = live.translate(br, base, profile, c["serial"], c["format"],
                                            None if not c.get("blend") else blend_struct(c["blend"], 15))
                spv = (v, f)
                res["translate"] = "ok"
            except Exception as ex:   # noqa
                res["translate"] = "error"
                res["translate_error"] = str(ex)[:400]
        else:
            if c["shape"] in ("dual", "tri"):
                n = 2 if c["shape"] == "dual" else 3
                masks = c.get("masks") or ([15, 0] if n == 2 else [15, 0, 0])
                fm = c.get("formats") or [c["format"]] * n
                atts = (Att * n)(*[Att(fm[i], m) for i, m in enumerate(masks)])
                bl = (Blend * n)(*[blend_struct(c.get("blend") or NO_BLEND if i == 0 else NO_BLEND, masks[i]) for i in range(n)])
                pipeline = f3(base, vn, len(vn), base, fn, len(fn), atts, n, bl, attrs, len(attrs), lays, len(lays),
                              pbuf, len(profile), out[0], out[1], out[2], err, len(err))
            else:
                if c.get("blend") and c["blend"]["enabled"]:
                    bl = blend_struct(c["blend"], 15)
                    pipeline = f2(base, vn, len(vn), base, fn, len(fn), c["format"], attrs, len(attrs), lays, len(lays),
                                  C.byref(bl), pbuf, len(profile), out[0], out[1], out[2], err, len(err))
                else:
                    pipeline = f1(base, vn, len(vn), base, fn, len(fn), c["format"], attrs, len(attrs), lays, len(lays),
                                  pbuf, len(profile), out[0], out[1], out[2], err, len(err))
            if pipeline:
                spv = (C.string_at(out[0].bytes, out[0].length), C.string_at(out[1].bytes, out[1].length))
                res["translate"] = "ok"
            else:
                res["translate"] = "error"
                res["translate_error"] = err.value.decode(errors="replace")[:400]
        if spv:
            res["vertex_spirv_sha256"] = hashlib.sha256(spv[0]).hexdigest()
            res["fragment_spirv_sha256"] = hashlib.sha256(spv[1]).hexdigest()
            if os.environ.get("TEGU_GATE_DUMP"):
                dd=Path(os.environ["TEGU_GATE_DUMP"]);dd.mkdir(parents=True,exist_ok=True)
                (dd/(res["vertex_spirv_sha256"][:16]+".vert.spv")).write_bytes(spv[0]);(dd/(res["fragment_spirv_sha256"][:16]+".frag.spv")).write_bytes(spv[1])
            fetch = Fetch()
            ferr = C.create_string_buffer(512)
            frc = -1
            if pipeline:
                frc = br.tegu_air_render_pipeline_vertex_fetch(pipeline, C.byref(fetch), ferr, len(ferr))
                res["fetch_rc"] = frc
                if frc:
                    res["fetch_error"] = ferr.value.decode(errors="replace")[:200]
            vb, fb = C.create_string_buffer(spv[0]), C.create_string_buffer(spv[1])
            vs, fs, pair, plan, vinfo = live.Graphics(), live.Graphics(), live.Pair(), Plan(), VInfo()
            multi = c["shape"] in ("dual", "tri")
            flags = OPT_DRAW | OPT_EXT | (OPT_RT if multi else 0) | int(os.environ.get("TEGU_GATE_EXTRA_FLAGS","0"))
            opts = Options(1, flags, c.get("rt", 0) if multi else 0, 0)
            err.value = b""
            rc = mc.tegu_mali_compile_generic_pair_options(vb, len(spv[0]), fb, len(spv[1]), None, 0, None, 0,
                                                          C.byref(opts), C.byref(vs), C.byref(fs), C.byref(pair),
                                                          C.byref(plan), C.byref(vinfo), err, len(err))
            res["compile_rc"] = rc
            res["compile_error"] = err.value.decode(errors="replace")[:300]
            if rc == 0:
                res["vertex_code_bytes"], res["fragment_code_bytes"] = vs.size, fs.size
                res["vertex_code_sha256"] = hashlib.sha256(C.string_at(vs.code, vs.size)).hexdigest()
                res["fragment_code_sha256"] = hashlib.sha256(C.string_at(fs.code, fs.size)).hexdigest()
                res["vs_sysval_mask"], res["fs_sysval_mask"] = vs.sysval_mask, fs.sysval_mask
                res["vs_tls"], res["fs_tls"] = vs.tls, fs.tls
                res["pair_flags"] = pair.flags
                res["flat_shading"] = bool(pair.flags & 2)
                res["noperspective_mask"] = pair.noperspective_mask
                res["varying_count"] = vinfo.count
                res["generic_size_B"] = pair.generic_size_B
                res["vs_attribute_mask"] = pair.vs_attribute_mask
                res["bindings"] = [[plan.bindings[i].binding, plan.bindings[i].kind] for i in range(plan.count)]
                res["reads_framebuffer"] = plan.reads_framebuffer
                if frc == 0:
                    covered = 0
                    n = 0
                    for i in range(min(fetch.count, 31)):
                        a = fetch.attributes[i]
                        if a.location < 32 and (pair.vs_attribute_mask >> a.location) & 1:
                            covered |= 1 << a.location
                            n += 1
                    res["fetch_covers"] = bool((covered == pair.vs_attribute_mask) and (n or not pair.vs_attribute_mask))
                mc.tegu_mali_graphics_free(C.byref(vs))
                mc.tegu_mali_graphics_free(C.byref(fs))
            if c["shape"] == "dual" and c.get("masks") == [15, 15]:
                vs3, fs3, pair3, plan3, vinfo3 = live.Graphics(), live.Graphics(), live.Pair(), Plan(), VInfo()
                err.value = b""
                rc3 = mc.tegu_mali_compile_generic_pair_options(vb, len(spv[0]), fb, len(spv[1]), None, 0, None, 0,
                                                               C.byref(Options(1, OPT_DRAW | OPT_EXT | OPT_RT, 1, 0)),
                                                               C.byref(vs3), C.byref(fs3), C.byref(pair3),
                                                               C.byref(plan3), C.byref(vinfo3), err, len(err))
                res["compile_rt1_rc"] = rc3
                res["compile_rt1_error"] = err.value.decode(errors="replace")[:200]
                if rc3 == 0:
                    mc.tegu_mali_graphics_free(C.byref(vs3))
                    mc.tegu_mali_graphics_free(C.byref(fs3))
            # legacy (no options) entry for comparison: what the pre-options frontend would have done
            vs2, fs2, pair2, plan2 = live.Graphics(), live.Graphics(), live.Pair(), Plan()
            err.value = b""
            rc2 = mc.tegu_mali_compile_generic_pair(vb, len(spv[0]), fb, len(spv[1]), C.byref(vs2), C.byref(fs2),
                                                    C.byref(pair2), C.byref(plan2), err, len(err))
            res["legacy_rc"] = rc2
            if rc2:
                res["legacy_error"] = err.value.decode(errors="replace")[:200]
            else:
                mc.tegu_mali_graphics_free(C.byref(vs2))
                mc.tegu_mali_graphics_free(C.byref(fs2))
        if pipeline:
            br.tegu_air_render_pipeline_close(pipeline)
        for b in out:
            if b.bytes:
                br.tegu_air_blob_free(C.byref(b))
        res["seconds"] = round(time.monotonic() - t0, 3)
        print("RESULT %d %s" % (idx, json.dumps(res)), flush=True)
    return 0


# ------------------------------------------------------------------ parent
def run_pair(spec, work):
    """Run all combos of one pair in a child; recover from a crash by skipping the crashing combo."""
    results = {}
    remaining = list(range(len(spec["combos"])))
    log = []
    while remaining:
        sub = dict(spec)
        sub["combos"] = [spec["combos"][i] for i in remaining]
        sp = work / "spec.json"
        sp.write_text(json.dumps(sub))
        cmd = ["prlimit", "--as=2147483648", "--cpu=12", "--core=0", sys.executable, str(Path(__file__).resolve()),
               "--child", str(sp)]
        t0 = time.monotonic()
        try:
            r = subprocess.run(cmd, capture_output=True, timeout=90)
            code, out, errb = r.returncode, r.stdout.decode(errors="replace"), r.stderr.decode(errors="replace")
        except subprocess.TimeoutExpired as ex:
            code, out, errb = "timeout", (ex.stdout or b"").decode(errors="replace"), (ex.stderr or b"").decode(errors="replace")
        started = None
        done = set()
        for line in out.splitlines():
            if line.startswith("START "):
                started = int(line.split()[1])
            elif line.startswith("RESULT "):
                _, i, js = line.split(" ", 2)
                i = int(i)
                if i >= 0:
                    results[remaining[i]] = json.loads(js)
                    done.add(i)
                else:
                    for k in remaining:
                        results[k] = json.loads(js)
                    return results, log
        log.append({"exit": code, "seconds": round(time.monotonic() - t0, 2), "stderr_tail": errb[-400:]})
        if code == 0 and len(done) == len(remaining):
            break
        if started is not None and started not in done:
            gi = remaining[started]
            tail = (errb + out)[-600:]
            cls = "abort.base_instance" if "load_base_instance" in tail else \
                  "abort.texture_source_steal_tex_srcs" if "steal_tex_srcs" in tail else \
                  "cpu_limit_unknown" if code in ("timeout", -9) else "abort.other"
            results[gi] = {"combo": spec["combos"][gi]["tag"], "child_abort": cls, "exit": code, "tail": tail}
            remaining = [k for k in remaining if k not in results]
        else:
            for k in remaining:
                if k not in results:
                    results[k] = {"combo": spec["combos"][k]["tag"], "child_abort": "abort.other", "exit": code,
                                  "tail": (errb + out)[-400:]}
            break
        remaining = [k for k in remaining if k not in results]
    return results, log


def vertex_descriptor(entry, rs):
    """Same synthetic tightly-packed buffer0 descriptor as the ledger's prepare_graphics.py."""
    formats = {'float': (28, 4), 'float2': (29, 8), 'float3': (30, 12), 'float4': (31, 16), 'half': (25, 2),
               'half2': (25, 4), 'half3': (26, 6), 'half4': (27, 8), 'uint': (36, 4), 'uint2': (37, 8),
               'uint3': (38, 12), 'uint4': (39, 16), 'int': (32, 4), 'int2': (33, 8), 'int3': (34, 12), 'int4': (35, 16)}
    attrs, offset = [], 0
    for line in rs[entry['air_sha256']].get('interfaces', []):
        if '"air.vertex_input"' not in line:
            continue
        m = re.search(r'!"air.location_index", i32 (\d+)', line)
        t = re.search('!"air.arg_type_name", !"([^"]+)"', line)
        if not m or not t or t[1] not in formats:
            continue
        fmt, size = formats[t[1]]
        offset = (offset + 3) // 4 * 4
        attrs.append({'index': int(m[1]), 'format': fmt, 'offset': offset, 'buffer_index': 0})
        offset += size
    layouts = [{'index': 0, 'stride': (offset + 15) // 16 * 16, 'step_function': 1, 'step_rate': 1}] if attrs else []
    return attrs, layouts


def build_pairs():
    fx = json.load(open(CORPUS / 'functions.json'))
    rs = {}
    for l in open(CORPUS / 'resources.jsonl'):
        r = json.loads(l)
        rs[r['air_sha256']] = r
    qc = [e for e in fx['entries'] if 'QuartzCore.framework/' in e['library_source']]
    byname = {e['name']: e for e in qc}

    def sig(e, role):
        out = set()
        for line in rs[e['air_sha256']].get('interfaces', []):
            m = re.search('!"air.' + role + '", !"([^"]+)"', line)
            t = re.search('!"air.arg_type_name", !"([^"]+)"', line)
            if m and t:
                out.add((m[1], t[1]))
        return out

    vs = [e for e in qc if e['stage'] == 'vertex']
    fs = [e for e in qc if e['stage'] == 'fragment']
    sv = {e['name']: sig(e, 'vertex_output') for e in vs}
    sf = {e['name']: sig(e, 'fragment_input') for e in fs}
    pairs = {}   # (v,f) -> origin tags

    def add(v, f, tag):
        pairs.setdefault((v, f), set()).add(tag)

    recipes = json.load(open(GPU / 'metal/tegu/quartzcore_catalog_recipes.json'))
    for r in recipes['entries']:
        add(r['vertex'], r['fragment'], 'captured_donor_recipe')
    add('fixed_vert_lph_spc', 'fixed_frag_lph_cph', 'captured_fixed_ui')
    for f in fs:
        n = f['name']
        sfx = 'lpf' if n.endswith('lpf') else 'lph' if n.endswith('lph') else None
        compat = [v['name'] for v in vs if sf[n] <= sv[v['name']]]
        if not sf[n]:
            if n == 'read_surf_frag':
                partners = ['read_surf_vert']
            else:
                partners = ['path_blit_vert_' + sfx]
        else:
            partners = [v for v in compat if sfx and v.endswith(sfx)]
            if not partners:
                partners = compat[:2]
        for v in partners:
            add(v, n, 'inferred_same_suffix_signature_compatible')
        for v in compat:
            add(v, n, 'matrix_signature_compatible')
    # ledger abi_candidate jobs for QuartzCore (pairing the earlier ledger actually tried)
    jobs = json.load(open(CORPUS / 'graphics-jobs.json'))['jobs']
    for j in jobs:
        if 'QuartzCore.framework/' in j['vertex']['library_source'] and j['pairing_evidence'] == 'abi_candidate':
            add(j['vertex']['name'], j['fragment']['name'], 'ledger_abi_candidate')
    return pairs, byname, rs, recipes


def make_combos(v, f, tags, byname, rs, recipes):
    combos = []
    attrs, lays = vertex_descriptor(byname[v], rs)
    if 'captured_donor_recipe' in tags:
        for r in recipes['entries']:
            if r['vertex'] == v and r['fragment'] == f:
                de = recipes['descriptor_classes'][r['descriptor_class']]
                at = [x for x in de['attachments'] if x['format']]
                combos.append(dict(tag='captured:' + r['descriptor_class'], origin='captured', kind='air', shape='dual', masks=[15, 0], rt=0,
                                   format=at[0]['format'], attributes=de['attributes'], layouts=de['layouts'],
                                   blend=None))
    if 'captured_fixed_ui' in tags:
        for serial in range(4, 10):
            for fmt in (80, 10, 70, 115):
                combos.append(dict(tag='captured_fixed_ui:serial%d:fmt%d' % (serial, fmt),
                                   origin='captured' if fmt == 80 else 'inferred_format_expansion',
                                   kind='fixed_serial', shape='single', serial=serial, format=fmt,
                                   attributes=[], layouts=[], blend=None))
    if tags == {'matrix_signature_compatible'}:
        combos.append(dict(tag='matrix:single:fmt80', origin='inferred', kind='air', shape='single', format=80,
                           masks=None, rt=0, attributes=attrs, layouts=lays, blend=None))
    elif tags - {'captured_donor_recipe', 'captured_fixed_ui'}:
        if f.startswith('fixed_frag') or v.startswith('fixed_vert'):
            # function-constant dependent: values not captured except serial recipe above
            combos.append(dict(tag='inferred:single:fmt80:nofc', origin='inferred', kind='air', shape='single',
                               format=80, attributes=attrs, layouts=lays, blend=None))
        else:
            def A(tag, shape, fmt, masks=None, rt=0, blend=None):
                combos.append(dict(tag=tag, origin='inferred', kind='air', shape=shape, format=fmt, masks=masks, rt=rt,
                                   attributes=attrs, layouts=lays, blend=blend))
            for fmt in (80, 115, 70, 10):
                A('inferred:single:fmt%d' % fmt, 'single', fmt)
            for fmt in (80, 115, 70):
                A('inferred:dual_rt0:fmt%d' % fmt, 'dual', fmt, [15, 0], 0)
                A('inferred:dual_rt1:fmt%d' % fmt, 'dual', fmt, [0, 15], 1)
            A('inferred:dual_both:fmt80', 'dual', 80, [15, 15], 0)
            A('inferred:dual_rt0:fmt80:over', 'dual', 80, [15, 0], 0, PREMULT_OVER)
            A('inferred:single:fmt80:over', 'single', 80, None, 0, PREMULT_OVER)
            A('inferred:tri_rt0:fmt80', 'tri', 80, [15, 0, 0], 0)
            combos.append(dict(tag='inferred:tri_cov_only:rt2:fmt80', origin='inferred', kind='air', shape='tri', format=80,
                               formats=[80, 80, 10], masks=[0, 0, 15], rt=2, attributes=attrs, layouts=lays, blend=None))
            for rt in (0, 1, 2):
                combos.append(dict(tag='inferred:tri_r8:rt%d:fmt80' % rt, origin='inferred', kind='air', shape='tri', format=80,
                                   formats=[80, 80, 10], masks=[15, 15, 15], rt=rt, attributes=attrs, layouts=lays, blend=None))
    return combos


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--child')
    ap.add_argument('--out', type=Path)
    ap.add_argument('--only')
    a = ap.parse_args()
    if a.child:
        sys.exit(child_main(a.child))
    assert sha(COMPILER) == EXP_COMPILER and sha(BRIDGE) == EXP_BRIDGE, "compiler/bridge hash drift"
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    pairs, byname, rs, recipes = build_pairs()
    done_path = out / 'results.jsonl'
    done = set()
    if done_path.exists():
        for l in open(done_path):
            done.add(json.loads(l)['pair'])
    todo = sorted(pairs)
    if a.only:
        todo = [p for p in todo if a.only in p[0] or a.only in p[1]]
    meta = {"compiler": str(COMPILER), "compiler_sha256": EXP_COMPILER, "bridge": str(BRIDGE), "bridge_sha256": EXP_BRIDGE,
            "workers": 1, "rlimit_as": 2147483648, "rlimit_cpu_s": 12, "hardware_executed": False,
            "entry": "tegu_mali_compile_generic_pair_options flags=DRAW_PARAMETERS|EXTENDED_VARYINGS(+SELECT_RT rt0 for dual)",
            "started_unix": time.time()}
    (out / 'meta.json').write_text(json.dumps(meta, indent=1))
    t0 = time.monotonic()
    n = 0
    with open(done_path, 'a') as fh:
        for (v, f) in todo:
            key = v + '|' + f
            if key in done:
                continue
            tags = pairs[(v, f)]
            combos = make_combos(v, f, tags, byname, rs, recipes)
            if not combos:
                continue
            work = out / 'work' / (v + '__' + f)
            work.mkdir(parents=True, exist_ok=True)
            spec = {'vertex': v, 'fragment': f, 'combos': combos}
            res, log = run_pair(spec, work)
            row = {'pair': key, 'vertex': v, 'fragment': f, 'tags': sorted(tags),
                   'combos': [dict(combos[i], result=res.get(i)) for i in range(len(combos))], 'child_log': log}
            fh.write(json.dumps(row) + '\n')
            fh.flush()
            n += 1
            print('%d %s %.0fs' % (n, key, time.monotonic() - t0), flush=True)
    print('pairs_done', n)


if __name__ == '__main__':
    main()
