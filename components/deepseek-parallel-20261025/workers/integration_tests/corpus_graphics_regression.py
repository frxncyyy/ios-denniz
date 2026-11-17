#!/usr/bin/env python3
"""Regression probe for the real translate_graphics SIGSEGV inputs.

Runs one recorded corpus case in an isolated child (the caller starts one child
per case).  Success means the process survives and either creates a pipeline or
returns an explicit error.  SIGSEGV/SIGABRT/timeout is a hard failure.  Uses the
materialised AIR/constants/descriptor from corpus_inputs; never hardware.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[4]
GPU = WORK / "gpu"
sys.path.insert(0, str(GPU / "tools"))
import audit_shader_pipeline_inputs as audit  # noqa: E402

EVIDENCE = WORK / "gpu/build/deepseek-parallel-20261025/workers/corpus_inputs/evidence-current-ea4454b7-3fa3020d/graphics_fc_p0"
CORPUS = GPU / "build/ui-corpus-20261025"
PROFILE = CORPUS / "baseline/tegu-air-profile.h"
CASES = ("graphics-00086", "graphics-00498", "graphics-01278")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def case_inputs(case: str):
    for line in (EVIDENCE / "results.jsonl").read_text().splitlines():
        row = json.loads(line)
        if row.get("case") != case:
            continue
        run = EVIDENCE / "runs" / row["id"]
        spec = json.loads((run / "spec.json").read_text())
        result = json.loads((run / "result.json").read_text())
        return spec, result, run
    raise FileNotFoundError(f"{case} not found in {EVIDENCE}")


def descriptor_variant(spec, result):
    label = (result.get("descriptor") or {}).get("label")
    for variant in spec.get("descriptor_variants", []):
        if variant.get("label") == label:
            return variant
    return result.get("descriptor") or (spec.get("descriptor_variants") or [{}])[0]


def attachments(desc):
    raw = desc.get("attachments") or []
    if not raw:
        return []
    slots = [int(a.get("slot", i)) for i, a in enumerate(raw)]
    n = max(slots) + 1
    out = [None] * n
    for i, a in enumerate(raw):
        slot = int(a.get("slot", i))
        out[slot] = (int(a["format"]), int(a.get("write_mask", 15)))
    return [x if x is not None else (0, 0) for x in out]


def run_case(case: str, bridge_path: Path) -> dict:
    spec, result, run = case_inputs(case)
    bridge = audit.configure_bridge(bridge_path)
    profile = audit.profile_bytes(PROFILE)
    out = {
        "case": case,
        "run": str(run),
        "bridge": str(bridge_path),
        "bridge_sha256": sha256(bridge_path),
        "profile_sha256": sha256(PROFILE),
        "air_hashes": {k: spec[k]["sha256"] for k in ("vertex", "fragment")},
        "status": "unknown",
        "error": None,
    }
    err = C.create_string_buffer(2048)
    keepalive = []
    libs = []
    specialized = []
    try:
        for stage in ("vertex", "fragment"):
            path = CORPUS / spec[stage]["air"]
            data = path.read_bytes()
            if hashlib.sha256(data).hexdigest() != spec[stage]["sha256"]:
                out.update(status="refused", error=f"{stage} AIR hash mismatch")
                return out
            buffer = C.create_string_buffer(data)
            keepalive.append(buffer)
            lib = bridge.tegu_air_library_open(buffer, len(data), err, len(err))
            if not lib:
                out.update(status="refused", error=f"{stage} library_open: {err.value!r}")
                return out
            libs.append((stage, lib))
            values = (result.get("worker_progress") or {}).get("values", {}).get(stage, [])
            if not values:
                values = (result.get("constants") or {}).get(stage, [])
            rows = []
            for row in values:
                if "value_hex" not in row:
                    continue
                rows.append(row)
            if rows:
                array, names = audit._constant_structs(rows)
                keepalive.extend(names)
                err.value = b""
                spec_lib = bridge.tegu_air_library_specialize(
                    lib, spec[stage]["name"].encode(), len(spec[stage]["name"]),
                    C.cast(array, C.POINTER(audit.FunctionConstant)), len(rows),
                    err, len(err))
                if not spec_lib:
                    out.update(status="refused",
                               error=f"{stage} specialize: {err.value!r}")
                    return out
                specialized.append(spec_lib)
            else:
                specialized.append(lib)
        desc = descriptor_variant(spec, result)
        att = attachments(desc)
        attachment_array = (audit.Attachment * max(1, len(att)))()
        for i, (fmt, mask) in enumerate(att):
            attachment_array[i].format = fmt
            attachment_array[i].write_mask = mask
        attrs = desc.get("attributes") or []
        attr_array = (audit.VertexAttribute * max(1, len(attrs)))()
        for i, a in enumerate(attrs):
            attr_array[i].index = int(a["index"])
            attr_array[i].format = int(a["format"])
            attr_array[i].offset = int(a["offset"])
            attr_array[i].buffer_index = int(a["buffer_index"])
        layouts = desc.get("layouts") or []
        layout_array = (audit.VertexLayout * max(1, len(layouts)))()
        for i, l in enumerate(layouts):
            layout_array[i].index = int(l["index"])
            layout_array[i].stride = int(l["stride"])
            layout_array[i].step_function = int(l.get("step_function", 1))
            layout_array[i].step_rate = int(l.get("step_rate", 1))
        profiles = [C.create_string_buffer(profile)]
        keepalive.extend(profiles)
        vs, fs, meta = audit.BridgeBlob(), audit.BridgeBlob(), audit.BridgeBlob()
        err.value = b""
        pipeline = bridge.tegu_air_render_pipeline_create_with_attachments(
            specialized[0], spec["vertex"]["name"].encode(), len(spec["vertex"]["name"]),
            specialized[1], spec["fragment"]["name"].encode(), len(spec["fragment"]["name"]),
            attachment_array, len(att), attr_array, len(attrs),
            layout_array, len(layouts), profiles[0], len(profile),
            C.byref(vs), C.byref(fs), C.byref(meta), err, len(err))
        if pipeline:
            out.update(status="compiled", error=None)
            bridge.tegu_air_render_pipeline_close(pipeline)
            bridge.tegu_air_blob_free(C.byref(vs))
            bridge.tegu_air_blob_free(C.byref(fs))
            bridge.tegu_air_blob_free(C.byref(meta))
        else:
            out.update(status="refused", error=err.value.decode(errors="replace"))
    finally:
        for lib in specialized:
            bridge.tegu_air_library_close(lib)
        for stage, lib in libs:
            if lib not in specialized:
                bridge.tegu_air_library_close(lib)
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", choices=CASES, required=True)
    parser.add_argument("--bridge", type=Path, required=True)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    try:
        result = run_case(args.case, args.bridge.resolve())
    except Exception as exc:
        result = {"case": args.case, "status": "error",
                  "error": f"{type(exc).__name__}: {exc}"}
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
    return 0 if result.get("status") in ("compiled", "refused") else 1


if __name__ == "__main__":
    sys.exit(main())
