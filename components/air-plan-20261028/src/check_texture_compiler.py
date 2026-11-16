#!/usr/bin/env python3
"""Check the bounded paired texture compiler with actual authored AIR and refusals.

Every case runs in its own child process, so a backend abort cannot mask a result and
no two Mesa-linked libraries (each with its own glsl type singleton and NIR globals)
are ever loaded into one process. Binaries, disassembly and metadata go under
results/. Exits nonzero on any failure. Compile-only evidence: nothing runs on a GPU.
Inputs: current and pre-change libraries, texture/varying bridge fixture directories,
Mesa disassembler and a fresh output directory. All generated artifacts stay there.
"""
import ctypes, hashlib, json, os, re, subprocess, sys

import argparse
from pathlib import Path
import shutil

SOURCE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
for option in ('library','baseline','air','varying','disassembler','work'):
    parser.add_argument('--'+option,type=Path,required=True)
parser.add_argument('--case',help=argparse.SUPPRESS)
args = parser.parse_args()
LIBS = {'proto':str(args.library.resolve()),'baseline':str(args.baseline.resolve())}
DISASM = str(args.disassembler.resolve())
FIX = str((args.work/'fixtures').resolve())
RES = str((args.work/'results').resolve())


def prepare_fixtures():
    args.work.mkdir(parents=True,exist_ok=False)
    fix = Path(FIX);fix.mkdir();(fix/'spv').mkdir()
    for prefix, directory in (('air',args.air),('legacy-air',args.varying)):
        (fix/prefix).mkdir()
        for fmt in (70,80):
            for stage in ('vertex','fragment'):
                name=f'{fmt}-{stage}.spv'
                shutil.copyfile(directory/name,fix/prefix/name)
    for source in (SOURCE/'texture_compiler_fixtures').glob('*.spvasm'):
        shutil.copyfile(source,fix/source.name)
    subprocess.run([sys.executable,str(SOURCE/'generate_texture_fixtures.py'),
                    str(args.air/'70-fragment.spvasm'),str(fix)],check=True)
    for assembly in sorted(fix.glob('*.spvasm')):
        air = assembly.stem.startswith('air_')
        target = 'vulkan1.2' if air else 'vulkan1.0'
        output = fix/'spv'/(assembly.stem+'.spv')
        subprocess.run(['spirv-as',*(['--preserve-numeric-ids'] if air else []),
                        '--target-env',target,str(assembly),'-o',str(output)],check=True)
        subprocess.run(['spirv-val','--target-env',target,str(output)],check=True)
    original=(args.air/'70-fragment.spv').read_bytes()
    reassembled=(fix/'spv/air_fs_reassembled.spv').read_bytes()
    assert original[:8]+original[12:]==reassembled[:8]+reassembled[12:]

VERTEX, FRAGMENT = 1, 2
PLAN_EXPECTED = {"version": 1, "table": 0, "texture_index": 1, "sampler_index": 0,
                 "normalized_coordinates": 1, "descriptor_set": 0, "binding": 0}
PACKED_HANDLE = 0x10000  # va_tex_handle: (0<<27)|(1<<16)|(0<<11)|0


class Graphics(ctypes.Structure):
    _fields_ = [("code", ctypes.POINTER(ctypes.c_ubyte)), ("size", ctypes.c_size_t),
                ("stage", ctypes.c_uint32), ("registers", ctypes.c_uint32), ("tls", ctypes.c_uint32),
                ("fau_words", ctypes.c_uint32), ("sysval_mask", ctypes.c_uint32),
                ("push_bytes", ctypes.c_uint32), ("preload", ctypes.c_uint64)]


class Slot(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint32) for n in ("location", "offset", "components", "alu_type")]


U32 = ctypes.c_uint32


class Pair(ctypes.Structure):
    _fields_ = ([(n, U32) for n in ("version", "position_size", "secondary_offset", "secondary_size",
                                    "secondary_registers", "generic_size_B")] +
                [("secondary_preload", ctypes.c_uint64)] +
                [(n, U32) for n in ("vs_attribute_mask", "vs_varying_mask", "fs_varying_mask",
                                    "fs_uses_ld_var", "noperspective_mask", "varying_count")] +
                [("varyings", Slot * 4)])


class Pair2(ctypes.Structure):
    _fields_ = ([(n, U32) for n in ("version", "flags", "position_offset", "position_size",
                                    "point_position_offset", "point_position_size", "secondary_offset",
                                    "secondary_size", "secondary_registers", "generic_size_B")] +
                [("secondary_preload", ctypes.c_uint64)] +
                [(n, U32) for n in ("vs_attribute_mask", "vs_varying_mask", "fs_varying_mask",
                                    "fs_uses_ld_var", "noperspective_mask", "needs_extended_fifo",
                                    "point_size_section", "point_size_offset", "point_size_alu_type",
                                    "varying_count")] +
                [("varyings", Slot * 4)])


class Plan(ctypes.Structure):
    _fields_ = [(n, U32) for n in PLAN_EXPECTED]


assert ctypes.sizeof(Graphics) == 48 and ctypes.sizeof(Pair) == 120 and ctypes.sizeof(Pair2) == 152
assert ctypes.sizeof(Plan) == 28


def load(which):
    lib = ctypes.CDLL(LIBS[which])
    G, vp, sz = ctypes.POINTER(Graphics), ctypes.c_void_p, ctypes.c_size_t
    lib.tegu_mali_compile_graphics.argtypes = [vp, sz, U32, G, ctypes.c_char_p, sz]
    lib.tegu_mali_compile_graphics_with_vertex_inputs.argtypes = [vp, sz, U32, G, ctypes.POINTER(U32),
                                                                  ctypes.c_char_p, sz]
    lib.tegu_mali_graphics_free.argtypes = [G]
    for n in ("tegu_mali_compile_graphics_pair", "tegu_mali_compile_graphics_pair2"):
        getattr(lib, n).argtypes = [vp, sz, vp, sz, G, G, vp, ctypes.c_char_p, sz]
    if hasattr(lib, "tegu_mali_compile_graphics_pair_texture"):
        # Raw pointers so guard tests can pass deliberately overlapping objects.
        lib.tegu_mali_compile_graphics_pair_texture.argtypes = [vp, sz, vp, sz, vp, vp, vp, vp, vp, sz]
    return lib


def src(name):
    """'air:70-vertex' -> fixtures/air, 'lair:...' -> fixtures/legacy-air, else fixtures/spv."""
    for pfx, d in (("air:", "air"), ("lair:", "legacy-air")):
        if name.startswith(pfx):
            return open(os.path.join(FIX, d, name[len(pfx):] + ".spv"), "rb").read()
    return open(os.path.join(FIX, "spv", name + ".spv"), "rb").read()


def sha(b):
    return hashlib.sha256(b).hexdigest()


def code(g):
    return bytes(ctypes.string_at(g.code, g.size)) if g.size else b""


def gdict(g):
    return {"size": g.size, "stage": g.stage, "registers": g.registers, "tls": g.tls,
            "fau_words": g.fau_words, "sysval_mask": g.sysval_mask, "push_bytes": g.push_bytes,
            "preload": "0x%016x" % g.preload, "sha256": sha(code(g))}


def sdict(p):
    d = {}
    for n, _ in p._fields_:
        if n == "varyings":
            d[n] = [{k: getattr(p.varyings[i], k) for k, _ in Slot._fields_} for i in range(4)]
        elif n == "secondary_preload":
            d[n] = "0x%016x" % p.secondary_preload
        else:
            d[n] = getattr(p, n)
    return d


def empty(g):
    return bytes(g) == bytes(ctypes.sizeof(g))


def free(lib, *gs):
    for g in gs:
        lib.tegu_mali_graphics_free(ctypes.byref(g))
        assert empty(g)


def call_texture(lib, v, f, vs=None, fs=None, pr=None, pl=None, err=None, err_size=None):
    vs = vs if vs is not None else Graphics()
    fs = fs if fs is not None else Graphics()
    pr = pr if pr is not None else Pair2()
    pl = pl if pl is not None else Plan()
    err = err if err is not None else ctypes.create_string_buffer(256)
    a = lambda o: ctypes.addressof(o) if o is not False else None
    st = lib.tegu_mali_compile_graphics_pair_texture(
        v, len(v), f, len(f), a(vs), a(fs), a(pr), a(pl), a(err) if err is not False else None,
        (len(err) if err is not False else 0) if err_size is None else err_size)
    return st, vs, fs, pr, pl, (err.value.decode() if err is not False else None)


def disasm_bytes(data, tag):
    os.makedirs(os.path.join(RES, ".tmp"), exist_ok=True)
    p = os.path.join(RES, ".tmp", "%s.%d.bin" % (tag, os.getpid()))
    with open(p, "wb") as fh:
        fh.write(data)
    r = subprocess.run([DISASM, "--id=47266", "disasm", p], capture_output=True, text=True, timeout=30)
    os.unlink(p)
    if r.returncode:
        raise RuntimeError("disassembler failed: " + r.stderr)
    return r.stdout


def tex_evidence(fs_text):
    lines = fs_text.splitlines()
    tex = [l.strip() for l in lines if re.search(r"\bTEX_", l)]
    singles = [l for l in tex if l.startswith("TEX_SINGLE")]
    ev = {"tex_lines": tex, "tex_single": singles,
          "ld_var_buf": [l.strip() for l in lines if "LD_VAR_BUF" in l],
          "ld_var_plain": [l.strip() for l in lines if re.search(r"\bLD_VAR(?:_IMM)?[.\s]", l) and "BUF" not in l],
          "blend": [l.strip() for l in lines if re.search(r"\bBLEND\b", l)],
          "lea_or_query": [l.strip() for l in lines if re.search(r"\b(LEA_TEX|TEX_FETCH|TEX_GATHER|TEX_GRADIENT|TEX_DUAL|LD_TEX)", l)]}
    assert len(tex) == 1 and len(singles) == 1, tex
    m = re.search(r"\[(r\d+)\^?:(r\d+)\^?\]\s*$", singles[0])
    assert m, singles[0]
    lo, hi = m.group(1), m.group(2)
    idx = lines.index(next(l for l in lines if l.strip() == singles[0]))
    # The handle registers must be materialized immediately before the TEX from immediates.
    def last_def(reg):
        for l in reversed(lines[:idx]):
            mm = re.match(r"\s*(\S+)\s+%s\b,?\s*(.*)$" % reg, l)
            if mm:
                return l.strip()
        return None
    ev["handle_lo_def"], ev["handle_hi_def"] = last_def(lo), last_def(hi)
    lo_val = re.search(r"IADD_IMM\.i32(?:\.discard)? %s, 0x0, #(0x[0-9A-F]+)$" % lo, ev["handle_lo_def"] or "")
    hi_val = re.fullmatch(r"MOV\.i32 %s, 0x0" % hi, ev["handle_hi_def"] or "")
    assert lo_val and int(lo_val.group(1), 16) == PACKED_HANDLE and hi_val, (ev["handle_lo_def"], ev["handle_hi_def"])
    ev["packed_handle"] = "0x%x" % int(lo_val.group(1), 16)
    ev["tex_single_mods"] = singles[0].split()[0].split(".")
    assert ".2d." in singles[0] and ".zero" in singles[0] and ".32." in singles[0], singles[0]
    assert not ev["lea_or_query"] and not ev["ld_var_plain"] and len(ev["ld_var_buf"]) == 1 and len(ev["blend"]) == 1
    return ev


def save_case(tag, progs, meta):
    d = os.path.join(RES, tag)
    os.makedirs(d, exist_ok=True)
    for k, b in progs.items():
        with open(os.path.join(d, k + ".bin"), "wb") as fh:
            fh.write(b)
        with open(os.path.join(d, k + ".disasm.txt"), "w") as fh:
            fh.write(disasm_bytes(b, k))
    with open(os.path.join(d, "metadata.json"), "w") as fh:
        fh.write(json.dumps(meta, indent=1) + "\n")


def accept_case(vs_name, fs_name, tag):
    lib = load("proto")
    v, f = src(vs_name), src(fs_name)
    st, vs, fs, pr, pl, err = call_texture(lib, v, f)
    assert st == 0 and err == "", (st, err)
    plan = sdict(pl)
    assert plan == PLAN_EXPECTED, plan
    assert pr.version == 2 and pr.fs_uses_ld_var == 0 and pr.noperspective_mask == 0 and pr.needs_extended_fifo == 0
    vc, fc = code(vs), code(fs)
    sec = pr.secondary_offset
    progs = {"vs_full": vc, "fs": fc, "vs_position": vc[pr.position_offset:sec], "vs_varying": vc[sec:]}
    if pr.flags & 1:
        progs["vs_point_position"] = vc[:pr.point_position_size]
    fs_text = disasm_bytes(fc, "fs")
    ev = tex_evidence(fs_text)
    for k in ("vs_full", "vs_position", "vs_varying"):
        t = disasm_bytes(progs[k], k)
        assert not re.search(r"\bTEX_", t), k
    meta = {"inputs": {"vs": vs_name, "vs_sha256": sha(v), "fs": fs_name, "fs_sha256": sha(f)},
            "status": st, "vs": gdict(vs), "fs": gdict(fs), "pair2": sdict(pr), "texture_plan": plan,
            "sha256": {k: sha(b) for k, b in progs.items()}, "fs_texture_evidence": ev,
            "note": "compile-only; no GPU execution; no GPU addresses in any output"}
    save_case(tag, progs, meta)
    free(lib, vs, fs)
    return {"plan": plan, "pair2": meta["pair2"], "vs": meta["vs"], "fs": meta["fs"], "sha256": meta["sha256"],
            "tex_single": ev["tex_single"], "packed_handle": ev["packed_handle"],
            "handle_defs": [ev["handle_lo_def"], ev["handle_hi_def"]]}


def case_air(fmt):
    return accept_case("air:%d-vertex" % fmt, "air:%d-fragment" % fmt, "air-%d" % fmt)


def case_air_80_identical():
    a = json.load(open(os.path.join(RES, "air-70", "metadata.json")))
    b = json.load(open(os.path.join(RES, "air-80", "metadata.json")))
    same_in = a["inputs"]["vs_sha256"] == b["inputs"]["vs_sha256"] and a["inputs"]["fs_sha256"] == b["inputs"]["fs_sha256"]
    assert same_in and a["sha256"] == b["sha256"] and a["texture_plan"] == b["texture_plan"]
    return {"inputs_identical": same_in, "outputs_identical": True}


def case_vs_matches_pair2():
    """The textured pair's VS must be byte-identical to pair2's VS for the same AIR VS
    (linked against the qualified non-textured varying FS)."""
    lib = load("proto")
    v, f = src("air:70-vertex"), src("air:70-fragment")
    st, vs, fs, pr, pl, err = call_texture(lib, v, f)
    assert st == 0, err
    lf = src("lair:70-fragment")
    vs2, fs2, p2, e2 = Graphics(), Graphics(), Pair2(), ctypes.create_string_buffer(256)
    st2 = lib.tegu_mali_compile_graphics_pair2(v, len(v), lf, len(lf), ctypes.byref(vs2), ctypes.byref(fs2),
                                               ctypes.byref(p2), e2, 256)
    assert st2 == 0, e2.value
    assert code(vs) == code(vs2) and gdict(vs) == gdict(vs2) and bytes(pr) == bytes(p2)
    out = {"vs_sha256": sha(code(vs)), "pair2_struct_identical": True,
           "legacy_air_vs_identical_input": src("lair:70-vertex") == v}
    free(lib, vs, fs, vs2, fs2)
    return out


def refuse(vs_name, fs_name, expect):
    lib = load("proto")
    st, vs, fs, pr, pl, err = call_texture(lib, src(vs_name), src(fs_name))
    assert st != 0 and expect in err, (st, err)
    assert empty(vs) and empty(fs) and empty(pr) and empty(pl), "refusal must leave outputs zeroed"
    return {"status": st, "error": err}


REFUSALS = {
    "set1": ("vs_pos_color", "tex_set1", "set0 binding0"),
    "binding1": ("vs_pos_color", "tex_binding1", "set0 binding0"),
    "implicit_lod": ("vs_pos_color", "tex_implicit", "Only explicit-LOD"),
    "lod_one": ("vs_pos_color", "tex_lod1", "LOD must be constant 0.0"),
    "lod_dynamic": ("vs_pos_color", "tex_lod_dynamic", "LOD must be constant 0.0"),
    "offset": ("vs_pos_color", "tex_offset", "Unsupported texture source"),
    "grad_zero_derivatives": ("vs_pos_color", "tex_grad", "Only explicit-LOD"),
    "bias": ("vs_pos_color", "tex_bias", "Only explicit-LOD"),
    "offset_zero": ("vs_pos_color", "tex_offset_zero", "Unsupported texture source"),
    "min_lod": ("vs_pos_color", "tex_minlod", "Only explicit-LOD"),
    "gather": ("vs_pos_color", "tex_gather", "Only explicit-LOD"),
    "fetch_read": ("vs_pos_color", "tex_fetch", "Only explicit-LOD"),
    "query_size_live": ("vs_pos_color", "tex_query_live", "Texture query survived"),
    "query_levels": ("vs_pos_color", "tex_query_levels", "Texture query survived"),
    "compare_depth": ("vs_pos_color", "tex_compare", "Unsupported texture source"),
    "array": ("vs_pos_color", "tex_array", "float 2D non-array non-depth"),
    "3d": ("vs_pos_color", "tex_3d", "float 2D non-array non-depth"),
    "cube": ("vs_pos_color", "tex_cube", "float 2D non-array non-depth"),
    "msaa": ("vs_pos_color", "tex_msaa", "Only explicit-LOD"),
    "uint": ("vs_pos_color", "tex_uint", "float 2D non-array non-depth"),
    "two_samples": ("vs_pos_color", "tex_two_samples", "exactly one texture"),
    "unused_texture": ("vs_pos_color", "tex_unused", "exactly one texture"),
    "two_bindings": ("vs_pos_color", "tex_two_bindings", "exactly one combined image sampler"),
    "plus_ubo": ("vs_pos_color", "tex_plus_ubo", "exactly one combined image sampler"),
    "plus_storage_image": ("vs_pos_color", "tex_plus_storage", "exactly one combined image sampler"),
    "separate_texture_sampler": ("vs_pos_color", "tex_separate", "exactly one combined image sampler"),
    "vs_texture": ("tex_vs_sampling", "tex_ok", "Shader resources are unsupported"),
    "air_spec_default7_unnormalized": ("air:70-vertex", "air_spec7_unnormalized", "Texture query survived"),
    "no_texture_fs": ("vs_pos_color", "fs_color", "exactly one combined image sampler"),
    # pair2 varying/PSIZ validation is preserved on the textured entry
    "varying_mismatch": ("vs_color_loc1", "tex_ok", "interface mismatch"),
    "psiz_plus_clip": ("vs_psiz_clip", "tex_ok", "Unsupported graphics output"),
    "fs_reads_unwritten": ("vs_pos_only", "tex_ok", "interface mismatch"),
}


def legacy_calls(which):
    """Every legacy entry on legacy and textured inputs; returns comparable results."""
    lib = load(which)
    out = {}
    singles = [("vs_pos_only", VERTEX, 1), ("fs_const", FRAGMENT, 0), ("vs_pos_color", VERTEX, 1),
               ("fs_color", FRAGMENT, 0), ("vs_pos_color_psiz", VERTEX, 1), ("vs_pos_psiz_only", VERTEX, 0),
               ("lair:70-vertex", VERTEX, 1), ("lair:70-fragment", FRAGMENT, 0),
               ("air:70-vertex", VERTEX, 1), ("air:70-fragment", FRAGMENT, 0), ("tex_ok", FRAGMENT, 0),
               ("tex_vs_sampling", VERTEX, 1)]
    for n, s, m in singles:
        d, g, mask, err = src(n), Graphics(), U32(0xdead), ctypes.create_string_buffer(256)
        if m:
            st = lib.tegu_mali_compile_graphics_with_vertex_inputs(d, len(d), s, ctypes.byref(g), ctypes.byref(mask), err, 256)
        else:
            st = lib.tegu_mali_compile_graphics(d, len(d), s, ctypes.byref(g), err, 256)
        out["single/%s/%d/%d" % (n, s, m)] = {"status": st, "error": err.value.decode(), "result": gdict(g),
                                               "mask": mask.value if m else None}
        lib.tegu_mali_graphics_free(ctypes.byref(g))
    pairs = [("vs_pos_color", "fs_color"), ("vs_pos_color_psiz", "fs_color"), ("vs_psiz_dynamic", "fs_color"),
             ("lair:70-vertex", "lair:70-fragment"), ("lair:80-vertex", "lair:80-fragment"),
             ("air:70-vertex", "air:70-fragment"), ("vs_pos_color", "tex_ok"), ("vs_pos_color", "fs_ubo"),
             ("vs_pos_color", "fs_flat"), ("vs_color_loc1", "fs_color"), ("vs_psiz_clip", "fs_color")]
    for fn, P in (("tegu_mali_compile_graphics_pair", Pair), ("tegu_mali_compile_graphics_pair2", Pair2)):
        for a, b in pairs:
            v, f = src(a), src(b)
            vs, fs, pr, err = Graphics(), Graphics(), P(), ctypes.create_string_buffer(256)
            st = getattr(lib, fn)(v, len(v), f, len(f), ctypes.byref(vs), ctypes.byref(fs), ctypes.byref(pr), err, 256)
            out["%s/%s/%s" % (fn[len("tegu_mali_compile_graphics_"):], a, b)] = {
                "status": st, "error": err.value.decode(), "vs": gdict(vs), "fs": gdict(fs), "pair": sdict(pr)}
            free(lib, vs, fs)
    with open(os.path.join(RES, "legacy-%s.json" % which), "w") as fh:
        fh.write(json.dumps(out, indent=1, sort_keys=True) + "\n")
    return out


def case_guards():
    lib = load("proto")
    v, f = src("air:70-vertex"), src("air:70-fragment")
    out = {}
    buf = (ctypes.c_ubyte * 4)()
    # Non-empty VS / FS outputs are refused and left untouched.
    for which in ("vs", "fs"):
        g = Graphics(); g.code, g.size = ctypes.cast(buf, ctypes.POINTER(ctypes.c_ubyte)), 4
        kw = {which: g}
        st, vs, fs, pr, pl, err = call_texture(lib, v, f, **kw)
        assert st == 1 and err == "Output must be empty" and g.size == 4 and ctypes.addressof(g.code.contents) == ctypes.addressof(buf)
        assert empty(pr) and empty(pl) and empty(fs if which == "vs" else vs)
        out["nonempty_" + which] = st
    for ver in (1, 2, 7):
        pr = Pair2(); pr.version = ver
        st, vs, fs, _, pl, err = call_texture(lib, v, f, pr=pr)
        assert st == 1 and pr.version == ver and empty(vs) and empty(fs) and empty(pl)
        pl = Plan(); pl.version = ver
        st, vs, fs, pr, _, err = call_texture(lib, v, f, pl=pl)
        assert st == 1 and pl.version == ver and empty(vs) and empty(fs) and empty(pr)
        out["nonzero_versions_%d" % ver] = st
    for k in ("pr", "pl"):
        st, vs, fs, pr, pl, err = call_texture(lib, v, f, **{k: False})
        assert st == 1 and empty(vs) and empty(fs)
        out["null_" + k] = st
    st, vs, fs, pr, pl, err = call_texture(lib, v, f, vs=False)
    assert st == 1 and empty(fs)
    out["null_vs"] = st
    vs = Graphics()
    st, _, _, pr, pl, err = call_texture(lib, v, f, vs=vs, fs=vs)
    assert st == 1 and empty(vs) and empty(pr) and empty(pl)
    out["aliased_vs_fs"] = st

    # Overlapping objects: one blob carved into pair/plan regions that overlap.
    class Blob(ctypes.Structure):
        _fields_ = [("b", ctypes.c_ubyte * 512)]
    blob = Blob()
    base = ctypes.addressof(blob)
    lib_fn = lib.tegu_mali_compile_graphics_pair_texture
    vs, fs, err = Graphics(), Graphics(), ctypes.create_string_buffer(b"keep", 64)
    st = lib_fn(v, len(v), f, len(f), ctypes.addressof(vs), ctypes.addressof(fs), base, base + 140,
                ctypes.addressof(err), 64)
    assert st == 1 and err.value == b"keep" and empty(vs) and empty(fs) and bytes(blob) == bytes(512)
    out["plan_overlaps_pair"] = st
    pr, pl = Pair2(), Plan()
    st = lib_fn(v, len(v), f, len(f), ctypes.addressof(vs), ctypes.addressof(fs), ctypes.addressof(pr),
                ctypes.addressof(pl), ctypes.addressof(pr) + 8, 32)
    assert st == 1 and empty(vs) and empty(fs) and empty(pr) and empty(pl)
    out["error_overlaps_pair"] = st
    # Input SPIR-V overlapping an output: outputs are cleared before translation.
    # The VS output is an all-zero (otherwise valid) region inside the FS input range,
    # so only the alias check can refuse it (status1, not a translation status2).
    inp = ctypes.create_string_buffer(f + bytes(64), len(f) + 64)
    st = lib_fn(v, len(v), ctypes.addressof(inp), len(f) + 64, ctypes.addressof(inp) + len(f), ctypes.addressof(fs),
                ctypes.addressof(pr), ctypes.addressof(pl), ctypes.addressof(err), 64)
    assert st == 1 and err.value == b"keep" and inp.raw == f + bytes(64) and empty(fs) and empty(pr) and empty(pl)
    out["input_overlaps_output"] = st

    # Translation refusals zero every output.
    for tag, vv, ff, want in (("truncated_vs", v[:16], f, 2), ("bad_magic_fs", v, b"\0\0\0\0" + f[4:], 2),
                              ("misaligned_fs", v, f[:-2], 2)):
        st, vs, fs, pr, pl, err = call_texture(lib, vv, ff)
        assert st == want and empty(vs) and empty(fs) and empty(pr) and empty(pl), (tag, st, err)
        out[tag] = [st, err]
    # Error capacity: NULL buffer, size0, size1 and a short truncating buffer.
    st, vs, fs, pr, pl, _ = call_texture(lib, v, f, err=False)
    assert st == 0 and sdict(pl) == PLAN_EXPECTED
    free(lib, vs, fs)
    out["null_error_success"] = st
    neg = src("tex_lod1")
    vpc = src("vs_pos_color")
    st, vs, fs, pr, pl, _ = call_texture(lib, vpc, neg, err=False)
    assert st == 5 and empty(vs) and empty(fs) and empty(pr) and empty(pl)
    for cap in (0, 1, 8):
        e = ctypes.create_string_buffer(b"\x7f" * 32, 32)
        st, vs, fs, pr, pl, _ = call_texture(lib, vpc, neg, err=e, err_size=cap)
        raw = e.raw
        assert st == 5
        if cap == 0:
            assert raw == b"\x7f" * 32
        else:
            assert raw[cap - 1] == 0 and raw[cap:] == b"\x7f" * (32 - cap)
            assert raw[:cap - 1] == b"Texture LOD must be constant 0.0"[:cap - 1]
        out["error_capacity_%d" % cap] = raw[:cap].decode("latin1")
    # Input lifetime: input buffers are copied; results survive overwriting the input.
    vb, fb = ctypes.create_string_buffer(v, len(v)), ctypes.create_string_buffer(f, len(f))
    vs, fs, pr, pl, err = Graphics(), Graphics(), Pair2(), Plan(), ctypes.create_string_buffer(64)
    st = lib_fn(ctypes.addressof(vb), len(v), ctypes.addressof(fb), len(f), ctypes.addressof(vs), ctypes.addressof(fs),
                ctypes.addressof(pr), ctypes.addressof(pl), ctypes.addressof(err), 64)
    assert st == 0
    snap_v, snap_f = code(vs), code(fs)
    ctypes.memset(vb, 0xAB, len(v)); ctypes.memset(fb, 0xCD, len(f))
    del vb, fb
    st2, vs2, fs2, pr2, pl2, _ = call_texture(lib, v, f)
    assert st2 == 0 and code(vs) == snap_v == code(vs2) and code(fs) == snap_f == code(fs2)
    assert bytes(pr) == bytes(pr2) and bytes(pl) == bytes(pl2)
    assert ctypes.addressof(vs.code.contents) != ctypes.addressof(vs2.code.contents)
    free(lib, vs, fs)
    assert code(vs2) == snap_v and code(fs2) == snap_f, "freeing one result must not affect another"
    free(lib, vs2, fs2)
    out["input_lifetime_independent_results"] = True
    # Failure after VS success (FS texture refusal happens before VS compile; use a late
    # FS-stage refusal too): VS code freed and zeroed, plan zero.
    st, vs, fs, pr, pl, err = call_texture(lib, src("vs_pos_color"), src("tex_query_live"))
    assert st == 5 and empty(vs) and empty(fs) and empty(pr) and empty(pl)
    out["failure_cleanup"] = [st, err]
    return out


def case_handwritten_disasm_reference():
    """Pinned-assembler encoding of the hardware-qualified handwritten texture-r1 TEX_SINGLE."""
    word = 0x0128403417c00080
    t = disasm_bytes(word.to_bytes(8, "little") + bytes(8 * 15), "texture_r1_handwritten")
    first = t.splitlines()[0].strip()
    assert first.startswith("TEX_SINGLE")
    return {"texture_r1_word": "0x%016x" % word, "disasm": first}


CASES = {
    "air_actual_70": lambda: case_air(70),
    "air_actual_80": lambda: case_air(80),
    "air_70_80_identical": case_air_80_identical,
    "handwritten_tex_ok": lambda: accept_case("vs_pos_color", "tex_ok", "handwritten-tex-ok"),
    "vs_matches_pair2": case_vs_matches_pair2,
    "guards": case_guards,
    "handwritten_r1_reference": case_handwritten_disasm_reference,
    "legacy_proto": lambda: legacy_calls("proto"),
    "legacy_baseline": lambda: legacy_calls("baseline"),
    }
for _n, (_v, _f, _e) in REFUSALS.items():
    CASES["refuse_" + _n] = (lambda v=_v, f=_f, e=_e: refuse(v, f, e))
ORDER_LAST = ("air_70_80_identical",)


def run_child(name):
    r = subprocess.run([sys.executable, "-I", os.path.abspath(__file__), *sys.argv[1:], "--case", name],
                       capture_output=True, text=True, timeout=300)
    if r.returncode:
        return {"ok": False, "returncode": r.returncode, "stderr": r.stderr[-2000:]}
    return {"ok": True, "result": json.loads(r.stdout.strip().splitlines()[-1])}


def main():
    if args.case:
        print(json.dumps(CASES[args.case]()))
        return 0
    prepare_fixtures()
    os.makedirs(RES, exist_ok=True)
    names = [n for n in CASES if n not in ORDER_LAST] + list(ORDER_LAST)
    report = {n: run_child(n) for n in names}
    lp, lb = (report["legacy_" + w] for w in ("proto", "baseline"))
    if lp["ok"] and lb["ok"]:
        diff = sorted(k for k in lp["result"] if lp["result"][k] != lb["result"].get(k))
        report["legacy_proto_vs_baseline_identical"] = {"ok": not diff and lp["result"].keys() == lb["result"].keys(),
                                                        "result": {"differing": diff, "calls": len(lp["result"])}}
        # Must refuse textures on every legacy entry.
        r = lp["result"]
        bad = [k for k in r if ("/air:70-fragment" in k or "tex_" in k) and r[k]["status"] == 0]
        report["legacy_entries_refuse_textures"] = {"ok": not bad, "result": {"accepted": bad}}
    if os.path.isdir(os.path.join(RES, ".tmp")):
        os.rmdir(os.path.join(RES, ".tmp"))
    with open(os.path.join(RES, "check_report.json"), "w") as fh:
        fh.write(json.dumps(report, indent=1) + "\n")
    bad = [n for n, r in report.items() if not r["ok"]]
    for n, r in report.items():
        print("%-44s %s" % (n, "PASS" if r["ok"] else "FAIL " + r.get("stderr", "")[-700:]))
    print("%d/%d passed; report %s" % (len(report) - len(bad), len(report), os.path.join(RES, "check_report.json")))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
