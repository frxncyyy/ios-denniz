#!/usr/bin/env python3
"""Exercise the additive per-attachment blend C ABI with an actual AIR library.

This is a compile/metadata ABI test only. It does not approve a native gate or
claim GPU execution. Keep the supplied QuartzCore metallib/profile external.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


class Blob(C.Structure):
    _fields_ = [("bytes", C.POINTER(C.c_uint8)), ("length", C.c_size_t)]


class Attachment(C.Structure):
    _fields_ = [("format", C.c_uint32), ("write_mask", C.c_uint32)]


class VertexAttribute(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ("index", "format", "offset", "buffer_index")]


class VertexLayout(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ("index", "stride", "step_function", "step_rate")]


class Blend(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in (
        "version", "enabled", "rgb_op", "rgb_src", "rgb_dst",
        "alpha_op", "alpha_src", "alpha_dst", "write_mask")]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def cbytes(data: bytes):
    buf = (C.c_uint8 * len(data)).from_buffer_copy(data)
    return buf, C.cast(buf, C.POINTER(C.c_uint8))


def blob_bytes(blob: Blob) -> bytes:
    if not blob.bytes and blob.length:
        raise AssertionError("blob has a length but a null data pointer")
    return C.string_at(blob.bytes, blob.length) if blob.length else b""


def blob_empty(blob: Blob) -> bool:
    return not bool(blob.bytes) and blob.length == 0


def blob_free(lib, blob: Blob) -> None:
    lib.tegu_air_blob_free(C.byref(blob))


def set_blob_sentinel(blob: Blob) -> None:
    blob.bytes = C.cast(C.c_void_p(0x12345678), C.POINTER(C.c_uint8))
    blob.length = 0x76543210


def blob_raw(blob: Blob) -> bytes:
    return C.string_at(C.byref(blob), C.sizeof(blob))


def json_diff(a, b, prefix=""):
    if type(a) is not type(b):
        return [prefix or "$"]
    if isinstance(a, dict):
        out = []
        for key in sorted(set(a) | set(b)):
            path = f"{prefix}.{key}" if prefix else key
            if key not in a or key not in b:
                out.append(path)
            else:
                out.extend(json_diff(a[key], b[key], path))
        return out
    if isinstance(a, list):
        if len(a) != len(b):
            return [prefix + ".length"]
        out = []
        for i, (x, y) in enumerate(zip(a, b)):
            out.extend(json_diff(x, y, f"{prefix}[{i}]"))
        return out
    return [] if a == b else [prefix or "$"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", type=Path, required=True, help="host C ABI shared library")
    parser.add_argument("--air", type=Path, required=True, help="external QuartzCore.metallib input")
    parser.add_argument("--profile", type=Path, required=True, help="external compositor profile JSON bytes")
    parser.add_argument("--work", type=Path, required=True, help="fresh ignored result directory")
    parser.add_argument("--mali", type=Path, help="optional resource-aware Mesa Mali compiler shared library")
    parser.add_argument("--spirv-val", default="spirv-val")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[3]
    bridge_path, air_path, profile_path = (p.resolve() for p in (args.bridge, args.air, args.profile))
    work = args.work.resolve()
    if work.exists():
        parser.error("--work must name a fresh directory")
    work.mkdir(parents=True)
    air_data, profile_data = air_path.read_bytes(), profile_path.read_bytes()
    if not air_data or not profile_data:
        parser.error("AIR and profile inputs must be nonempty")
    lib = C.CDLL(str(bridge_path))

    lib.tegu_air_library_open.argtypes = [C.POINTER(C.c_uint8), C.c_size_t, C.c_void_p, C.c_size_t]
    lib.tegu_air_library_open.restype = C.c_void_p
    lib.tegu_air_library_close.argtypes = [C.c_void_p]
    lib.tegu_air_render_pipeline_create_with_attachments.argtypes = [
        C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t,
        C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t,
        C.POINTER(Attachment), C.c_size_t,
        C.POINTER(VertexAttribute), C.c_size_t,
        C.POINTER(VertexLayout), C.c_size_t,
        C.POINTER(C.c_uint8), C.c_size_t,
        C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob), C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_create_with_attachments.restype = C.c_void_p
    lib.tegu_air_render_pipeline_create_with_attachment_blends.argtypes = [
        C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t,
        C.c_void_p, C.POINTER(C.c_uint8), C.c_size_t,
        C.POINTER(Attachment), C.c_size_t, C.POINTER(Blend),
        C.POINTER(VertexAttribute), C.c_size_t,
        C.POINTER(VertexLayout), C.c_size_t,
        C.POINTER(C.c_uint8), C.c_size_t,
        C.POINTER(Blob), C.POINTER(Blob), C.POINTER(Blob), C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_create_with_attachment_blends.restype = C.c_void_p
    lib.tegu_air_render_pipeline_close.argtypes = [C.c_void_p]
    lib.tegu_air_blob_free.argtypes = [C.POINTER(Blob)]

    air_buf, air_ptr = cbytes(air_data)
    profile_buf, profile_ptr = cbytes(profile_data)
    vn, fn = b"downsample_blur_vert_lph", b"downsample_4_frag_lph"
    vn_buf, vn_ptr = cbytes(vn)
    fn_buf, fn_ptr = cbytes(fn)
    error = C.create_string_buffer(512)
    handle = lib.tegu_air_library_open(air_ptr, len(air_data), error, len(error))
    if not handle:
        raise RuntimeError(f"AIR library open failed: {error.value.decode(errors='replace')}")

    attachments = (Attachment * 2)(Attachment(80, 15), Attachment(80, 0))
    attributes = (VertexAttribute * 2)(VertexAttribute(0, 31, 0, 0), VertexAttribute(1, 29, 16, 0))
    layouts = (VertexLayout * 1)(VertexLayout(0, 40, 1, 1))
    # Target 0 is the live premultiplied source-over descriptor: ADD, One,
    # OneMinusSourceAlpha in RGB and alpha. Target 1 deliberately carries a
    # complete, distinct disabled state while retaining its zero write mask.
    blends = (Blend * 2)(
        Blend(1, 1, 0, 1, 5, 0, 1, 5, 15),
        Blend(1, 0, 0, 1, 0, 0, 1, 0, 0),
    )

    def invoke(which="blend", *, att_ptr=None, att_count=2, blend_ptr=None,
               attr_ptr=None, attr_count=2, layout_ptr=None, layout_count=1,
               profile_arg=None, profile_len=None, name_ptr=None, name_len=None,
               out_ptrs=None, error_ptr=None, error_cap=512):
        outs = out_ptrs or [Blob(), Blob(), Blob()]
        def out_ref(i):
            if outs[i] is None:
                return None
            return C.byref(outs[i]) if isinstance(outs[i], Blob) else outs[i]
        null_error = error_ptr == "null"
        eptr = None if null_error else (error if error_ptr is None else error_ptr)
        err = C.cast(eptr, C.c_void_p) if eptr is not None else None
        prof = profile_ptr if profile_arg is None else profile_arg
        plen = len(profile_data) if profile_len is None else profile_len
        nvp, nvl = (vn_ptr, len(vn)) if name_ptr is None else name_ptr
        if which == "baseline":
            ptr = lib.tegu_air_render_pipeline_create_with_attachments(
                handle, nvp, nvl, handle, fn_ptr, len(fn),
                attachments if att_ptr is None else att_ptr, att_count,
                attributes if attr_ptr is None else attr_ptr, attr_count,
                layouts if layout_ptr is None else layout_ptr, layout_count,
                prof, plen, out_ref(0), out_ref(1), out_ref(2), err, error_cap)
        else:
            ptr = lib.tegu_air_render_pipeline_create_with_attachment_blends(
                handle, nvp, nvl, handle, fn_ptr, len(fn),
                attachments if att_ptr is None else att_ptr, att_count,
                blends if blend_ptr is None else blend_ptr,
                attributes if attr_ptr is None else attr_ptr, attr_count,
                layouts if layout_ptr is None else layout_ptr, layout_count,
                prof, plen, out_ref(0), out_ref(1), out_ref(2), err, error_cap)
        return ptr, outs, eptr

    def successful(which, tag):
        error.value = b""
        ptr, outs, _ = invoke(which)
        if not ptr:
            raise AssertionError(f"{tag} rejected: {error.value.decode(errors='replace')}")
        try:
            values = [blob_bytes(b) for b in outs]
            if any(not v for v in values):
                raise AssertionError(f"{tag} returned an empty SPIR-V or metadata blob")
            result = values
        finally:
            lib.tegu_air_render_pipeline_close(ptr)
            for blob in outs:
                blob_free(lib, blob)
        return result

    try:
        baseline = successful("baseline", "unblended baseline")
        blended = successful("blend", "per-attachment blend compile")

        for label, outputs in (("unblended", baseline), ("blended", blended)):
            for stage, data in zip(("vs", "fs"), outputs[:2]):
                (work / f"{label}.{stage}.spv").write_bytes(data)
                subprocess.run([args.spirv_val, "--target-env", "vulkan1.2",
                                "--uniform-buffer-standard-layout", "--scalar-block-layout",
                                str(work / f"{label}.{stage}.spv")], check=True)
        bmeta, vmeta = json.loads(baseline[2]), json.loads(blended[2])
        requested = vmeta.get("requested_attachments")
        if requested != [{"format": 80, "write_mask": 15}, {"format": 80, "write_mask": 0}]:
            raise AssertionError(f"attachment list/masks were not retained: {requested!r}")
        requested_blends = vmeta.get("requested_attachment_blends")
        expected_blends = [
            {"version": 1, "enabled": 1, "rgb_op": 0, "rgb_src": 1, "rgb_dst": 5,
             "alpha_op": 0, "alpha_src": 1, "alpha_dst": 5, "write_mask": 15},
            {"version": 1, "enabled": 0, "rgb_op": 0, "rgb_src": 1, "rgb_dst": 0,
             "alpha_op": 0, "alpha_src": 1, "alpha_dst": 0, "write_mask": 0},
        ]
        if requested_blends != expected_blends:
            raise AssertionError(f"full per-target blend array was not retained: {requested_blends!r}")
        graphics = vmeta.get("graphics", {})
        colors = graphics.get("color_attachments", [])
        if len(colors) != 2 or [c.get("write_mask") for c in colors] != [15, 0]:
            raise AssertionError(f"translator graphics state lost the two attachment masks: {colors!r}")
        expected_graphics_blend = {"alpha_op": 0, "color_op": 0, "dst_alpha": 7,
                                   "dst_color": 7, "src_alpha": 1, "src_color": 1}
        if colors[0].get("blend") != expected_graphics_blend:
            raise AssertionError(f"translator graphics blend state is not One/OneMinusSourceAlpha ADD: {colors[0].get('blend')!r}")
        if colors[1].get("format") != colors[0].get("format"):
            raise AssertionError("second zero-mask target was dropped or changed format")

        # Ordinary compiler refusals leave the caller's initially empty outputs empty.
        refusal_cases = []
        def refusal(label, **kw):
            error.value = b""
            ptr, outs, _ = invoke("blend", **kw)
            if ptr:
                lib.tegu_air_render_pipeline_close(ptr)
                for blob in outs:
                    blob_free(lib, blob)
                raise AssertionError(f"{label}: invalid descriptor unexpectedly compiled")
            if any(not blob_empty(b) for b in outs):
                raise AssertionError(f"{label}: refusal returned partial outputs")
            if not error.value:
                raise AssertionError(f"{label}: ordinary refusal did not report an error")
            refusal_cases.append({"case": label, "error": error.value.decode(errors="replace")})

        refusal("null blend array", blend_ptr=C.POINTER(Blend)())
        refusal("zero attachment count", att_count=0)
        nine_atts = (Attachment * 9)(*[Attachment(80, 15) for _ in range(9)])
        nine_blends = (Blend * 9)(*[Blend(1, 1, 0, 1, 5, 0, 1, 5, 15) for _ in range(9)])
        refusal("attachment count nine", att_ptr=nine_atts, att_count=9, blend_ptr=nine_blends)
        mismatch_blends = (Blend * 2)(*list(blends))
        mismatch_blends[1].write_mask = 15
        refusal("blend/attachment write-mask mismatch", blend_ptr=mismatch_blends)
        bad_mask_atts = (Attachment * 2)(Attachment(80, 16), Attachment(80, 0))
        refusal("attachment write-mask out of range", att_ptr=bad_mask_atts)
        bad_format_atts = (Attachment * 2)(Attachment(999, 15), Attachment(80, 0))
        refusal("unsupported attachment format", att_ptr=bad_format_atts)
        bgra_only_blend_atts = (Attachment * 2)(Attachment(115, 15), Attachment(115, 0))
        refusal("enabled blend on non-RGBA8/BGRA8 format", att_ptr=bgra_only_blend_atts)
        for field, val in (("version", 2), ("enabled", 2), ("rgb_op", 3), ("alpha_op", 3),
                           ("rgb_src", 10), ("alpha_dst", 10)):
            bad = (Blend * 2)(*list(blends))
            setattr(bad[0], field, val)
            refusal(f"invalid blend {field}={val}", blend_ptr=bad)
        refusal("null attachment span", att_ptr=C.POINTER(Attachment)())
        refusal("null active vertex attrs", attr_ptr=C.POINTER(VertexAttribute)())
        refusal("attribute count above cap", attr_count=32)
        refusal("layout count above cap", layout_count=32)
        refusal("empty profile span", profile_len=0)
        refusal("null vertex name", name_ptr=(C.POINTER(C.c_uint8)(), len(vn)))
        refusal("oversized vertex-name span", name_ptr=(vn_ptr, 4097))
        refusal("oversized profile span", profile_len=1024 * 1024 + 1)

        # Alias preflight promises no writes. The explicit cases below snapshot
        # both the aliased input and sentinel-filled output descriptors.
        duplicate = [Blob(), Blob(), Blob()]
        set_blob_sentinel(duplicate[0]); set_blob_sentinel(duplicate[1]); set_blob_sentinel(duplicate[2])
        before = blob_raw(duplicate[0])
        ptr, _, _ = invoke("blend", out_ptrs=[duplicate[0], duplicate[0], duplicate[2]])
        if ptr or blob_raw(duplicate[0]) != before:
            raise AssertionError("overlapping output blobs were not rejected without writes")
        refusal_cases.append({"case": "output/output overlap", "error": "<unchanged by alias-safe preflight>"})

        # Null output object and arithmetic-overflowing spans are preflight
        # refusals; all other output bytes remain untouched.
        null_out = [None, Blob(), Blob()]
        set_blob_sentinel(null_out[1]); set_blob_sentinel(null_out[2])
        null_before = [blob_raw(x) for x in null_out[1:]]
        ptr, _, _ = invoke("blend", out_ptrs=null_out)
        if ptr or [blob_raw(x) for x in null_out[1:]] != null_before:
            raise AssertionError("null output descriptor was not rejected without writes")
        refusal_cases.append({"case": "null output descriptor", "error": "<unchanged by alias-safe preflight>"})

        overflow_outs = [Blob(), Blob(), Blob()]
        for b in overflow_outs: set_blob_sentinel(b)
        overflow_before = [blob_raw(b) for b in overflow_outs]
        size_max = C.c_size_t(-1).value
        ptr, _, _ = invoke("blend", out_ptrs=overflow_outs, profile_len=size_max)
        if ptr or [blob_raw(b) for b in overflow_outs] != overflow_before:
            raise AssertionError("overflowing profile span was not refused without writes")
        refusal_cases.append({"case": "overflowing profile span", "error": "<unchanged by alias-safe preflight>"})

        # Output object placed over blend input, and error buffer over blend input.
        blend_out = C.cast(blends, C.POINTER(Blob))
        alias_outs = [Blob(), Blob(), Blob()]
        set_blob_sentinel(alias_outs[1]); set_blob_sentinel(alias_outs[2])
        before_blend_bytes = C.string_at(C.addressof(blends), C.sizeof(blends))
        ptr = lib.tegu_air_render_pipeline_create_with_attachment_blends(
            handle, vn_ptr, len(vn), handle, fn_ptr, len(fn), attachments, 2, blends,
            attributes, 2, layouts, 1, profile_ptr, len(profile_data), blend_out,
            C.byref(alias_outs[1]), C.byref(alias_outs[2]), error, len(error))
        if ptr or C.string_at(C.addressof(blends), C.sizeof(blends)) != before_blend_bytes:
            raise AssertionError("output/input overlap was not rejected before touching aliased blend bytes")
        refusal_cases.append({"case": "output aliases blend input", "error": "<unchanged by alias-safe preflight>"})

        err_alias = C.cast(blends, C.c_void_p)
        before_blend_bytes = C.string_at(C.addressof(blends), C.sizeof(blends))
        out = [Blob(), Blob(), Blob()]
        for b in out:
            set_blob_sentinel(b)
        before_out = [blob_raw(b) for b in out]
        ptr, _, _ = invoke("blend", out_ptrs=out, error_ptr=err_alias, error_cap=C.sizeof(blends))
        if ptr or [blob_raw(b) for b in out] != before_out or C.string_at(C.addressof(blends), C.sizeof(blends)) != before_blend_bytes:
            raise AssertionError("error/input overlap was not rejected without writes")
        refusal_cases.append({"case": "error aliases blend input", "error": "<unchanged by alias-safe preflight>"})

        # The error span may not overlap an output descriptor either.
        err_backing = C.create_string_buffer(128)
        C.memset(C.addressof(err_backing), 0x5a, C.sizeof(err_backing))
        before_err_backing = C.string_at(C.addressof(err_backing), C.sizeof(err_backing))
        err_out = [C.cast(err_backing, C.POINTER(Blob)), Blob(), Blob()]
        set_blob_sentinel(err_out[1]); set_blob_sentinel(err_out[2])
        before_err_out = [blob_raw(err_out[1]), blob_raw(err_out[2])]
        ptr, _, _ = invoke("blend", out_ptrs=err_out, error_ptr=C.cast(err_backing, C.c_void_p), error_cap=C.sizeof(err_backing))
        if ptr or C.string_at(C.addressof(err_backing), C.sizeof(err_backing)) != before_err_backing or [blob_raw(x) for x in err_out[1:]] != before_err_out:
            raise AssertionError("error/output overlap was not rejected without writes")
        refusal_cases.append({"case": "error aliases output", "error": "<unchanged by alias-safe preflight>"})

        # Errors from ordinary refusals are NUL-terminated when an independent
        # error buffer is available; a null error buffer is also safe.
        no_error_ptr, no_error_blobs, _ = invoke("blend", profile_len=0, error_ptr="null", error_cap=0)
        if no_error_ptr or any(not blob_empty(b) for b in no_error_blobs):
            raise AssertionError("null error span did not safely refuse")
        refusal_cases.append({"case": "null error span", "error": "<no error output requested>"})

        mali_report = {"status": "not-run"}
        mali = args.mali
        if mali is None:
            candidate = repo / "research/compositor-mrt-native-resources-r1/isolated/mesa-compiler-build/src/panfrost/clc/libtegu_mali_graphics.so"
            mali = candidate if candidate.exists() else None
        if mali:
            runner = repo / "tools/tegu/metal/resource_compiler_case.py"
            cmd = [sys.executable, str(runner), "--library", str(mali.resolve()),
                   "--vertex", str(work / "blended.vs.spv"), "--fragment", str(work / "blended.fs.spv"),
                   "--mode", "resources", "--output-dir", str(work / "mali-blended")]
            proc = subprocess.run(cmd, text=True, capture_output=True, timeout=120)
            if proc.returncode:
                raise RuntimeError(f"resource-aware Mali compile failed ({proc.returncode}):\n{proc.stdout}\n{proc.stderr}")
            mali_report = json.loads(proc.stdout.strip().splitlines()[-1])
            mali_report["status"] = "pass"
        else:
            mali_report = {"status": "skipped", "reason": "Mali shared library not found; pass --mali"}

        diff_paths = json_diff(bmeta, vmeta)
        report = {
            "scope": "C ABI compile planning and metadata only; no native gate or GPU execution claim",
            "input": {"air": str(air_path), "air_sha256": sha(air_data), "air_bytes": len(air_data),
                      "profile": str(profile_path), "profile_sha256": sha(profile_data), "profile_bytes": len(profile_data),
                      "bridge": str(bridge_path), "bridge_sha256": sha(bridge_path.read_bytes())},
            "descriptor": {"attachments": [{"format": a.format, "write_mask": a.write_mask} for a in attachments],
                           "vertex_attributes": [{n: getattr(a, n) for n, _ in VertexAttribute._fields_} for a in attributes],
                           "vertex_layout": {n: getattr(layouts[0], n) for n, _ in VertexLayout._fields_},
                           "blends": [{n: getattr(b, n) for n, _ in Blend._fields_} for b in blends]},
            "baseline_unblended": {"vs_bytes": len(baseline[0]), "fs_bytes": len(baseline[1]), "metadata_bytes": len(baseline[2]),
                                   "vs_sha256": sha(baseline[0]), "fs_sha256": sha(baseline[1]), "metadata_sha256": sha(baseline[2])},
            "blend_compile": {"vs_bytes": len(blended[0]), "fs_bytes": len(blended[1]), "metadata_bytes": len(blended[2]),
                              "vs_sha256": sha(blended[0]), "fs_sha256": sha(blended[1]), "metadata_sha256": sha(blended[2]),
                              "vs_matches_unblended": baseline[0] == blended[0],
                              "fs_matches_unblended": baseline[1] == blended[1],
                              "metadata_matches_unblended": baseline[2] == blended[2],
                              "metadata_diff_paths": diff_paths,
                              "requested_attachments": requested,
                              "requested_attachment_blends": requested_blends,
                              "graphics_color_attachments": colors,
                              "requirements_features": vmeta.get("requirements", {}).get("features", [])},
            "resource_aware_mali": mali_report,
            "refusal_cases": refusal_cases,
            "spirv_validation": "pass",
        }
        report_path = work / "report.json"
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(f"PASS attachment-blend C ABI checks; VS/FS SPIR-V match unblended baseline; "
              f"metadata preserves requested per-target blend; Mali={mali_report['status']}; "
              f"{len(refusal_cases)} refusal/alias cases; report={report_path}")
    finally:
        lib.tegu_air_library_close(handle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
