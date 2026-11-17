#!/usr/bin/env python3
"""Real Rust compute v2 planner probe against real AIR fixtures.

Loads the actual cdylib (env TEGU_AIR_BRIDGE_SO or the newest worker build)
and calls tegu_air_pipeline_native_dispatch_v2 with real AIR + profile files.
ctypes structures are generated from the Rust source declarations, never from
handwritten sizes.  Runs in a child process from the main integration test so
an ABI-stale library cannot crash the whole suite.
"""
from __future__ import annotations

import ctypes
import hashlib
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GPU = HERE.parents[3]
WORK = HERE.parents[4]
sys.path.insert(0, str(HERE))
import abi_oracle  # noqa: E402

READ = 0x100
WRITE = 0x200
SAMPLE = 0x400
STORAGE = 0x800


def _ctype(kind: str):
    return {"u8": ctypes.c_uint8, "u16": ctypes.c_uint16, "u32": ctypes.c_uint32,
            "i32": ctypes.c_int32, "f32": ctypes.c_float, "u64": ctypes.c_uint64,
            "usize": ctypes.c_size_t}[kind]


def build_ctypes(structs: dict[str, abi_oracle.Struct]) -> dict[str, type]:
    built: dict[str, type] = {}

    def make(name: str):
        if name in built:
            return built[name]
        struct = structs[name]
        fields = []
        for f in struct.fields:
            base = make(f.nested.name) if f.nested else _ctype(f.type)
            fields.append((f.name, base * f.count if f.count != 1 else base))
        cls = type(name, (ctypes.Structure,), {"_fields_": fields})
        built[name] = cls
        return cls

    # Resolve in dependency order; NativeConstant has no nested fields.
    for name in ("NativeConstant", "ComputeDescriptor", "ComputeRegion",
                 "NativeDispatchV2", "ComputeResource"):
        make(name)
    return built


class Blob(ctypes.Structure):
    _fields_ = [("bytes", ctypes.POINTER(ctypes.c_uint8)), ("length", ctypes.c_size_t)]


def library_path() -> Path:
    env = os.environ.get("TEGU_AIR_BRIDGE_SO")
    candidates = []
    if env:
        candidates.append(Path(env))
    candidates += [
        WORK / "gpu/build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so",
        WORK / "gpu/build/ui-corpus-20261025/baseline/libtegu_air_bridge.so",
    ]
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError("libtegu_air_bridge.so not found")


def fixture_dir() -> Path:
    env = os.environ.get("TEGU_COMPUTE_FIXTURES")
    if env:
        return Path(env)
    return WORK / "gpu/build/deepseek-parallel-20261025/workers/compute/air-work/fixtures"


def resource(cls, kind: int, index: int, access: int = 0, **kw):
    r = cls()
    r.version = 1
    r.kind = kind
    r.index = index
    r.access = access
    for key, value in kw.items():
        setattr(r, key, value)
    return r


def texture(cls, index: int, access: int, **kw):
    defaults = dict(format=80, texture_type=2, width=16, height=16, depth=1,
                    array_length=1, mipmap_levels=1, sample_count=1, usage=1,
                    row_stride=64, array_layers=1, resource_id=index + 1,
                    length=64 * 16)
    defaults.update(kw)
    return resource(cls, 2, index, access, **defaults)


def buffer(cls, index: int, access: int, length: int = 4096):
    return resource(cls, 1, index, access, array_layers=1, length=length)


def sampler(cls, index: int, minf=0, magf=0, mipf=0, address=0, aniso=1,
            lod_max=64.0, normalized=1, compare=0, reduction=0):
    return resource(cls, 3, index, 0, array_layers=1, min_filter=minf,
                    mag_filter=magf, mip_filter=mipf, address_s=address,
                    address_t=address, address_r=address,
                    normalized_coordinates=normalized, max_anisotropy=aniso,
                    compare_function=compare, reduction_mode=reduction,
                    lod_max_clamp=lod_max)


def open_pipeline(lib, fx: Path, air_name: str, function: str):
    air = (fx / air_name).read_bytes()
    profile = (fx / "profile.json").read_bytes()
    err = ctypes.create_string_buffer(1024)
    lib.tegu_air_library_open.restype = ctypes.c_void_p
    lib.tegu_air_library_open.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
    lib.tegu_air_library_close.argtypes = [ctypes.c_void_p]
    lib.tegu_air_pipeline_create.restype = ctypes.c_void_p
    lib.tegu_air_pipeline_create.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p,
        ctypes.c_size_t, ctypes.POINTER(Blob), ctypes.POINTER(Blob),
        ctypes.c_char_p, ctypes.c_size_t,
    ]
    lib.tegu_air_pipeline_close.argtypes = [ctypes.c_void_p]
    library = lib.tegu_air_library_open(air, len(air), err, 1024)
    if not library:
        raise RuntimeError(f"library_open {air_name}: {err.value!r}")
    spirv, metadata = Blob(), Blob()
    pipeline = lib.tegu_air_pipeline_create(
        library, function.encode(), len(function), profile, len(profile),
        ctypes.byref(spirv), ctypes.byref(metadata), err, 1024)
    if not pipeline:
        lib.tegu_air_library_close(library)
        raise RuntimeError(f"pipeline_create {function}: {err.value!r}")
    return lib, library, pipeline, spirv


def dispatch(lib, cls, pipeline, resources, mode=0, extents=(4, 4, 1), local=(8, 8, 1)):
    lib.tegu_air_pipeline_native_dispatch_v2.restype = ctypes.c_int32
    lib.tegu_air_pipeline_native_dispatch_v2.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(cls), ctypes.c_size_t, ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
    ]
    arr = (cls * len(resources))(*resources)
    out = ctypes.create_string_buffer(ctypes.sizeof(built["NativeDispatchV2"]))
    ctypes.memset(out, 0xAA, len(out))
    variant = Blob()
    err = ctypes.create_string_buffer(1024)
    e = (ctypes.c_uint32 * 3)(*extents)
    lo = (ctypes.c_uint32 * 3)(*local)
    rc = lib.tegu_air_pipeline_native_dispatch_v2(
        pipeline, arr, len(resources), mode, e, lo, ctypes.byref(variant), out, err, 1024)
    return rc, out, variant, err.value.decode(errors="replace")


def field(out, cls, name):
    return getattr(ctypes.cast(out, ctypes.POINTER(cls)).contents, name)


def descriptor(out, i):
    view = ctypes.cast(out, ctypes.POINTER(built["NativeDispatchV2"])).contents
    d = view.descriptors[i]
    return {name: getattr(d, name) for name, _ in d._fields_}


def region(out, i):
    view = ctypes.cast(out, ctypes.POINTER(built["NativeDispatchV2"])).contents
    r = view.regions[i]
    constants = [(r.constants[k].id, r.constants[k].size, bytes(r.constants[k].data))
                 for k in range(r.constant_count)]
    return {"groups": list(r.groups), "local": list(r.local), "flags": r.flags,
            "constant_count": r.constant_count, "constants": constants}


def run() -> dict:
    structs = abi_oracle.parse_rust(GPU / "metal/tegu/air_bridge.rs")
    global built
    built = build_ctypes(structs)
    path = library_path()
    lib = ctypes.CDLL(str(path))
    if not hasattr(lib, "tegu_air_pipeline_native_dispatch_v2"):
        raise RuntimeError(f"{path} lacks tegu_air_pipeline_native_dispatch_v2")
    fx = fixture_dir()
    result = {
        "library": str(path),
        "library_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "rust_source_sha256": hashlib.sha256((GPU / "metal/tegu/air_bridge.rs").read_bytes()).hexdigest(),
        "fixtures": str(fx),
        "checks": [],
    }

    def check(name, condition, detail=None):
        result["checks"].append({"name": name, "ok": bool(condition),
                                 "detail": detail if not condition else None})

    # --- test_fast_math: combined texture+sampler and a write-only buffer ---
    lib, library, pipeline, spirv = open_pipeline(lib, fx, "test_fast_math.air", "TestFastMath")
    resources = [
        buffer(built["ComputeResource"], 0, READ | WRITE),
        texture(built["ComputeResource"], 0, READ | SAMPLE),
        sampler(built["ComputeResource"], 0),
    ]
    rc, out, variant, err = dispatch(lib, built["ComputeResource"], pipeline, resources)
    check("fast_math rc", rc == 0, err)
    if rc == 0:
        v2 = ctypes.cast(out, ctypes.POINTER(built["NativeDispatchV2"])).contents
        check("fast_math version", v2.version == 2, v2.version)
        check("fast_math region count", v2.region_count == 1, v2.region_count)
        check("fast_math descriptor count", v2.descriptor_count == 2, v2.descriptor_count)
        check("fast_math local", list(v2.local) == [8, 8, 1], list(v2.local))
        d0, d1 = descriptor(out, 0), descriptor(out, 1)
        check("fast_math buffer native binding", d0["binding"] == 0 and d0["kind"] == 2, d0)
        check("fast_math buffer metal slot", d0["metal_index"] == 0, d0)
        check("fast_math buffer write flag", d0["flags"] & WRITE, d0["flags"])
        check("fast_math combined native binding != metal slot",
              d1["binding"] == 1 and d1["metal_index"] == 0 and d1["kind"] == 6, d1)
        check("fast_math combined sampler index", d1["sampler_index"] == 0, d1)
        check("fast_math combined read|sample flags",
              (d1["flags"] & (READ | SAMPLE)) == (READ | SAMPLE), d1["flags"])
        check("fast_math combined view format", d1["view_format"] == 80, d1["view_format"])
        check("fast_math combined span", d1["range_length"] == 1024, d1["range_length"])
        r0 = region(out, 0)
        check("fast_math region groups", r0["groups"] == [4, 4, 1], r0["groups"])
        check("fast_math constants", r0["constant_count"] == 6, r0["constant_count"])
        if r0["constant_count"] == 6:
            check("fast_math local X constant", r0["constants"][0][0] == 0 and r0["constants"][0][2][:4] == (8).to_bytes(4, "little"), r0["constants"][0])
            check("fast_math local Y constant", r0["constants"][1][0] == 1 and r0["constants"][1][2][:4] == (8).to_bytes(4, "little"), r0["constants"][1])
            check("fast_math local Z constant", r0["constants"][2][0] == 2 and r0["constants"][2][2][:4] == (1).to_bytes(4, "little"), r0["constants"][2])
        check("fast_math nearest variant empty", variant.length == 0, variant.length)

    # --- exact-thread dispatch: real translator edge regions ---
    rc_exact, out_exact, variant_exact, err_exact = dispatch(
        lib, built["ComputeResource"], pipeline, resources,
        mode=1, extents=(17, 9, 1), local=(8, 4, 1))
    check("exact-thread rc", rc_exact == 0, err_exact)
    if rc_exact == 0:
        v2 = ctypes.cast(out_exact, ctypes.POINTER(built["NativeDispatchV2"])).contents
        check("exact-thread regions present", v2.region_count == 4, v2.region_count)
        check("exact-thread nominal local", list(v2.local) == [8, 4, 1], list(v2.local))
        regions = [region(out_exact, i) for i in range(v2.region_count)]
        observed = sorted((tuple(r["local"]), tuple(r["groups"])) for r in regions)
        expected = sorted([
            ((8, 4, 1), (2, 2, 1)),
            ((8, 1, 1), (2, 1, 1)),
            ((1, 4, 1), (1, 2, 1)),
            ((1, 1, 1), (1, 1, 1)),
        ])
        check("exact-thread local/groups edges", observed == expected, observed)
        area = 0
        for r in regions:
            area += (r["groups"][0] * r["local"][0] *
                     r["groups"][1] * r["local"][1] *
                     r["groups"][2] * r["local"][2])
        check("exact-thread disjoint thread area", area == 17 * 9, area)
        for r in regions:
            for cid, size, data in r["constants"][:3]:
                if cid < 3 and size == 4:
                    check(f"exact-thread region constant{cid} matches local",
                          int.from_bytes(data[:4], "little") == r["local"][cid],
                          (r["local"], cid, data[:4].hex()))

    # --- invalid extents/local contract ---
    for label, mode, extents, local in (
        ("exact-thread zero extent", 1, (0, 9, 1), (8, 4, 1)),
        ("threadgroup zero extent", 0, (4, 0, 1), (8, 4, 1)),
        ("local over 256 threads", 0, (1, 1, 1), (32, 32, 1)),
        ("invalid dispatch mode", 2, (1, 1, 1), (8, 8, 1)),
    ):
        rc_bad, _o, _v, err_bad = dispatch(
            lib, built["ComputeResource"], pipeline, resources,
            mode=mode, extents=extents, local=local)
        check(label + " refused", rc_bad != 0, err_bad)

    # --- linear/repeat sampler: Metal enums flow through the plan ---
    resources = [
        buffer(built["ComputeResource"], 0, READ | WRITE),
        texture(built["ComputeResource"], 0, READ | SAMPLE),
        sampler(built["ComputeResource"], 0, minf=1, magf=1, mipf=2, address=2,
                aniso=4, lod_max=3.0),
    ]
    rc2, out2, variant2, err2 = dispatch(lib, built["ComputeResource"], pipeline, resources)
    check("linear sampler rc", rc2 == 0, err2)
    if rc2 == 0:
        d1 = descriptor(out2, 1)
        check("linear sampler combined still native binding1", d1["binding"] == 1 and d1["kind"] == 6, d1)
        check("linear sampler plan span", d1["range_length"] == 1024, d1["range_length"])

    # --- refusal: texture span exceeds retained allocation ---
    bad = [
        buffer(built["ComputeResource"], 0, READ | WRITE),
        texture(built["ComputeResource"], 0, READ | SAMPLE, length=100),
        sampler(built["ComputeResource"], 0),
    ]
    rc3, out3, variant3, err3 = dispatch(lib, built["ComputeResource"], pipeline, bad)
    check("bad span refused", rc3 != 0, err3)
    v3 = ctypes.cast(out3, ctypes.POINTER(built["NativeDispatchV2"])).contents
    check("bad span output zeroed", v3.version == 0 and v3.region_count == 0 and v3.descriptor_count == 0, (v3.version, v3.region_count, v3.descriptor_count))
    check("bad span variant empty", not variant3.bytes and variant3.length == 0, variant3.length)

    # --- refusal: sampled texture without shader-read usage ---
    bad_usage = [
        buffer(built["ComputeResource"], 0, READ | WRITE),
        texture(built["ComputeResource"], 0, READ | SAMPLE, usage=2),
        sampler(built["ComputeResource"], 0),
    ]
    rc4, _out4, _v4, err4 = dispatch(lib, built["ComputeResource"], pipeline, bad_usage)
    check("sampled texture without SHADER_READ refused", rc4 != 0, err4)

    # --- refusal: sampler address outside the Metal enum domain ---
    bad_sampler = [
        buffer(built["ComputeResource"], 0, READ | WRITE),
        texture(built["ComputeResource"], 0, READ | SAMPLE),
        sampler(built["ComputeResource"], 0, address=6),
    ]
    rc5, _out5, _v5, err5 = dispatch(lib, built["ComputeResource"], pipeline, bad_sampler)
    check("sampler address enum outside Metal domain refused", rc5 != 0, err5)

    lib.tegu_air_pipeline_close(pipeline)
    lib.tegu_air_library_close(library)

    # --- accumulate: UBO + sampled texture + read/write image ---
    lib, library, pipeline, spirv = open_pipeline(lib, fx, "accumulate_kernel.air", "accumulateKernel")
    resources = [
        buffer(built["ComputeResource"], 0, READ),
        texture(built["ComputeResource"], 0, READ | SAMPLE),
        texture(built["ComputeResource"], 1, STORAGE | WRITE, usage=3),
    ]
    rc6, out6, variant6, err6 = dispatch(lib, built["ComputeResource"], pipeline, resources)
    check("accumulate rc", rc6 == 0, err6)
    if rc6 == 0:
        v2 = ctypes.cast(out6, ctypes.POINTER(built["NativeDispatchV2"])).contents
        check("accumulate descriptor count", v2.descriptor_count == 3, v2.descriptor_count)
        d = [descriptor(out6, i) for i in range(3)]
        check("accumulate kinds", [x["kind"] for x in d] == [1, 3, 4], [x["kind"] for x in d])
        check("accumulate native bindings", [x["binding"] for x in d] == [0, 1, 2], [x["binding"] for x in d])
        check("accumulate metal slots", [x["metal_index"] for x in d] == [0, 0, 1], [x["metal_index"] for x in d])
        check("accumulate UBO read", d[0]["flags"] & READ, d[0]["flags"])
        check("accumulate image storage|write", (d[2]["flags"] & (STORAGE | WRITE)) == (STORAGE | WRITE), d[2]["flags"])
        check("accumulate sampled span", d[1]["range_length"] == 1024, d[1]["range_length"])
        check("accumulate image span", d[2]["range_length"] == 1024, d[2]["range_length"])
        r0 = region(out6, 0)
        check("accumulate constants", r0["constant_count"] == 7, r0["constant_count"])
        check("accumulate groups", r0["groups"] == [4, 4, 1], r0["groups"])

    lib.tegu_air_pipeline_close(pipeline)
    lib.tegu_air_library_close(library)

    result["ok"] = all(c["ok"] for c in result["checks"])
    return result


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    try:
        outcome = run()
    except Exception as exc:  # explicit unresolved dependency, never silent
        outcome = {"ok": False, "error": f"{type(exc).__name__}: {exc}", "checks": []}
    text = json.dumps(outcome, indent=2)
    if args.json:
        args.json.write_text(text + "\n")
    print(text)
    sys.exit(0 if outcome.get("ok") else 1)
