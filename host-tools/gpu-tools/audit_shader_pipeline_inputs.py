#!/usr/bin/env python3
"""Materialise and audit the *missing inputs* of the installed Metal shader corpus.

This tool is deliberately separate from the translator, the native compiler and the
existing corpus batch runners.  It never edits corpus evidence and it never changes a
production ABI.  It answers one question that a bare translation failure cannot:

    is this shader unsupported, or did the audit simply not supply a descriptor /
    function constant that the compiler requires?

The answer is recorded as a ledger of explicit, reproducible pipeline inputs.  Every
input row carries its origin (``captured``, ``inferred``, ``descriptor_derived`` or
``synthetic_diagnostic``), the exact bytes supplied, the frozen compiler/bridge hashes
and the exact translation / Mali-compile status.  Synthetic values are *never* reported
as application behaviour: they only show which branches of the shader were reachable
with the finite set actually explored.

Subcommands
-----------
``ledger``   Build the complete entry/exclusion ledger from the frozen corpus.
``plan``     Select cases and write auditable variant specifications.
``run``      Execute planned variants in isolated, resource-limited child processes.
``summarize`` Aggregate run directories into results/summary files.
``selftest`` Run pure unit checks and, with ``--host``, real representative shaders.

The production-side interface is intentionally small: the baseline ``libtegu_air_bridge``
exports ``tegu_air_library_specialize``, which is the only constant-materialisation route
used here.  No new symbol is required for this audit.
"""
from __future__ import annotations

import argparse
import ctypes as C
import gzip
import hashlib
import json
import os
import re
import resource
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SCHEMA = 1
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CORPUS = ROOT / "gpu" / "build" / "ui-corpus-20261025"
DEFAULT_WORKER = ROOT / "gpu" / "build" / "deepseek-parallel-20261025" / "workers" / "corpus_inputs"

# Frozen evidence hashes (baseline/audit-summary.json).  The runner refuses to use
# different libraries unless the caller explicitly permits it.
FROZEN_COMPILER = "3a7b691b57b3951746cf1ecab85fd4925690c98b9901d200208e5fadbc520278"
FROZEN_BRIDGE = "857f856d8ca79c99c799868e83a29de49dcbccc6f2669623bb58f0142ab2f6c8"

# Current takeover playground (Claude root, 25 October 2026).  These are
# defaults for --current only; every run still records and verifies the hash of
# the exact files it uses.
CURRENT_COMPILER_SHA256 = "ea4454b72b34b3f98c703fc06c301a0a5ca5dbefe506d98834ef51ef96bfe0b5"
CURRENT_COMPILER_REL = "gpu/build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so"
CURRENT_BRIDGE_REL = ("gpu/build/deepseek-parallel-20261025/workers/compute/"
                      "cargo-target/release/libtegu_air_bridge.so")

# ---------------------------------------------------------------------------
# Metal data types (MTLDataType and MTLVertexFormat).
# Values are the published enums; the compiler validates type + byte width, so a
# mismatch here is an audit error, not an application behaviour.
# ---------------------------------------------------------------------------


class MtlType:
    __slots__ = ("name", "numeric", "size", "family", "components")

    def __init__(self, name: str, numeric: int, size: int, family: str, components: int):
        self.name = name
        self.numeric = numeric
        self.size = size
        self.family = family  # bool | uint | sint | float
        self.components = components

    def as_json(self) -> dict:
        return {
            "name": self.name,
            "mtl_data_type": self.numeric,
            "bytes": self.size,
            "numeric": self.family,
            "components": self.components,
        }


def _build_types() -> dict[str, MtlType]:
    out: dict[str, MtlType] = {}
    scalar = {
        "char": (45, 1, "sint"),
        "uchar": (49, 1, "uint"),
        "short": (37, 2, "sint"),
        "ushort": (41, 2, "uint"),
        "int": (29, 4, "sint"),
        "uint": (33, 4, "uint"),
        "long": (81, 8, "sint"),
        "ulong": (85, 8, "uint"),
        "half": (16, 2, "float"),
        "float": (3, 4, "float"),
        "bool": (53, 1, "bool"),
        "bfloat": (121, 2, "float"),
    }
    for name, (numeric, size, family) in scalar.items():
        out[name] = MtlType(name, numeric, size, family, 1)
    vector = {
        "char": (46, 47, 48),
        "uchar": (50, 51, 52),
        "short": (38, 39, 40),
        "ushort": (42, 43, 44),
        "int": (30, 31, 32),
        "uint": (34, 35, 36),
        "long": (82, 83, 84),
        "ulong": (86, 87, 88),
        "half": (17, 18, 19),
        "float": (4, 5, 6),
        "bool": (54, 55, 56),
        "bfloat": (122, 123, 124),
    }
    for base, nums in vector.items():
        s = out[base]
        for comp, numeric in zip((2, 3, 4), nums):
            name = f"{base}{comp}"
            out[name] = MtlType(name, numeric, s.size * comp, s.family, comp)
    return out


MTL_TYPES = _build_types()
MTL_BY_NUMERIC = {t.numeric: t for t in MTL_TYPES.values()}

# MTLVertexFormat values, keyed by the AIR type name.  Unlike MTLDataType these
# are a distinct enum; the two must not be interchanged.
VERTEX_FORMAT = {
    "uchar2": 1, "uchar3": 2, "uchar4": 3,
    "char2": 4, "char3": 5, "char4": 6,
    "ushort2": 13, "ushort3": 14, "ushort4": 15,
    "short2": 16, "short3": 17, "short4": 18,
    "half2": 25, "half3": 26, "half4": 27,
    "float": 28, "float2": 29, "float3": 30, "float4": 31,
    "int": 32, "int2": 33, "int3": 34, "int4": 35,
    "uint": 36, "uint2": 37, "uint3": 38, "uint4": 39,
    "uchar": 45, "char": 46, "ushort": 49, "short": 50, "half": 53,
}

# MTLVertexFormat names used by the standalone translator CLI's vertex descriptor.
VERTEX_FORMAT_NAMES = {
    1: "UChar2", 2: "UChar3", 3: "UChar4", 4: "Char2", 5: "Char3", 6: "Char4",
    13: "UShort2", 14: "UShort3", 15: "UShort4", 16: "Short2", 17: "Short3",
    18: "Short4", 25: "Half2", 26: "Half3", 27: "Half4", 28: "Float",
    29: "Float2", 30: "Float3", 31: "Float4", 32: "Int", 33: "Int2",
    34: "Int3", 35: "Int4", 36: "UInt", 37: "UInt2", 38: "UInt3",
    39: "UInt4", 45: "UChar", 46: "Char", 49: "UShort", 50: "Short",
    53: "Half",
}
STEP_FUNCTION_NAMES = {0: "Constant", 1: "PerVertex", 2: "PerInstance",
                       3: "PerPatch", 4: "PerPatchControlPoint"}
ATTACHMENT_FORMAT_NAMES = {10: "R8Unorm", 70: "RGBA8Unorm",
                           80: "BGRA8Unorm", 115: "RGBA16Float"}


def mtl_type(name: str) -> MtlType | None:
    return MTL_TYPES.get(name)


def numeric_family(type_name: str) -> str | None:
    t = mtl_type(type_name)
    if t is None:
        return None
    if t.family == "bool":
        return "uint"
    return t.family


def encode_scalar(type_name: str, value) -> bytes | None:
    """Encode one value (scalar or per-lane sequence) exactly as the API expects."""
    t = mtl_type(type_name)
    if t is None:
        return None
    lanes = list(value) if isinstance(value, (list, tuple)) else [value] * t.components
    if len(lanes) != t.components:
        return None
    chunks = []
    for lane in lanes:
        if t.family == "bool":
            chunks.append(b"\x01" if lane else b"\x00")
        elif t.family == "uint":
            try:
                chunks.append(int(lane).to_bytes(t.size // t.components, "little", signed=False))
            except (OverflowError, ValueError):
                return None
        elif t.family == "sint":
            try:
                chunks.append(int(lane).to_bytes(t.size // t.components, "little", signed=True))
            except (OverflowError, ValueError):
                return None
        else:
            fmt = "<e" if t.size // t.components == 2 else "<f"
            try:
                chunks.append(struct.pack(fmt, float(lane)))
            except (OverflowError, ValueError, struct.error):
                return None
    return b"".join(chunks)


def value_label(type_name: str, raw: bytes) -> object:
    """Human-readable value for manifests (never used for compilation)."""
    t = mtl_type(type_name)
    if t is None:
        return raw.hex()
    lane_size = t.size // t.components
    lanes = []
    for i in range(t.components):
        chunk = raw[i * lane_size:(i + 1) * lane_size]
        if not chunk:
            break
        if t.family == "bool":
            lanes.append(bool(chunk[0]))
        elif t.family == "uint":
            lanes.append(int.from_bytes(chunk, "little", signed=False))
        elif t.family == "sint":
            lanes.append(int.from_bytes(chunk, "little", signed=True))
        else:
            fmt = "<e" if lane_size == 2 else "<f"
            lanes.append(round(struct.unpack(fmt, chunk)[0], 8))
    return lanes[0] if len(lanes) == 1 else lanes


# ---------------------------------------------------------------------------
# Small I/O helpers.
# ---------------------------------------------------------------------------


def atomic_write(path: Path, data: bytes | str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".tmp.{os.getpid()}")
    if isinstance(data, str):
        tmp.write_text(data, encoding="utf-8")
    else:
        tmp.write_bytes(data)
    tmp.replace(path)


def write_json(path: Path, obj) -> None:
    atomic_write(path, json.dumps(obj, indent=2, sort_keys=False) + "\n")


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def read_jsonl(path: Path):
    with path.open("r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line:
                yield json.loads(line)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def tool_sha256() -> str:
    return sha256_file(Path(__file__).resolve())


def resolve_source(path_value, corpus: Path, default_rel: str) -> Path:
    """Resolve a plan source path: absolute, repo-relative, or corpus-relative."""
    if not path_value:
        return (corpus / default_rel).resolve()
    path = Path(str(path_value))
    if path.is_absolute():
        return path.resolve()
    candidate = (ROOT / path)
    if candidate.exists():
        return candidate.resolve()
    return (corpus / path).resolve()


def source_rel(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path.resolve())


def current_source_paths() -> dict:
    return {
        "compiler": (ROOT / CURRENT_COMPILER_REL).resolve(),
        "bridge": (ROOT / CURRENT_BRIDGE_REL).resolve(),
        "profile": (DEFAULT_CORPUS / "baseline" / "tegu-air-profile.h").resolve(),
    }


def profile_bytes(path: Path) -> bytes:
    data = path.read_bytes()
    if data.lstrip().startswith(b"{"):
        json.loads(data)  # validate only
        return data
    text = data.decode("utf-8", errors="replace")
    match = re.search(r"\{([^{}]*)\}", text, re.S)
    if not match:
        raise ValueError("profile must be Khronos JSON or a generated C byte-array header")
    return bytes(int(value) for value in match.group(1).split(",") if value.strip())


# ---------------------------------------------------------------------------
# AIR / LLVM metadata readers.
# ---------------------------------------------------------------------------


def read_ll(corpus: Path, air_sha256: str) -> str:
    path = corpus / "llvm" / (air_sha256 + ".ll.gz")
    with gzip.open(path, "rt", encoding="utf-8", errors="replace") as fh:
        return fh.read()


_FC_RE = re.compile(
    r'^!(\d+) = !\{[^,\n]*?@([^,\n]+?), !"([^"]+)", !"([^"]+)", i32 (\d+), i1 (true|false)\}',
    re.M,
)


def parse_function_constants_ll(ll: str) -> list[dict]:
    """Return the module's declared function constants with exact AIR type names.

    The AIR metadata is authoritative for the type size (the MTLB container's numeric
    type is not always a known MTLDataType; e.g. type 101 appears in installed files).
    """
    group = re.search(r"!air\.function_constants = !\{([^}]*)\}", ll)
    refs = re.findall(r"!(\d+)", group.group(1)) if group else []
    by_ref = {}
    for match in _FC_RE.finditer(ll):
        by_ref[match.group(1)] = {
            "symbol": match.group(2),
            "type": match.group(3),
            "name": match.group(4),
            "index": int(match.group(5)),
            "required": match.group(6) == "true",
        }
    if refs:
        out = [by_ref[ref] for ref in refs if ref in by_ref]
    else:
        out = list(by_ref.values())
    return sorted(out, key=lambda c: (c["index"], c["name"]))


_VERTEX_INPUT_RE = re.compile(
    r'!\{i32 \d+, !"air\.vertex_input", !"air\.location_index", i32 (\d+), i32 (\d+),'
    r'[^}]*?"air\.arg_type_name", !"([^"]+)"'
)


def parse_vertex_inputs_ll(ll: str) -> list[dict]:
    out = []
    for match in _VERTEX_INPUT_RE.finditer(ll):
        out.append({
            "index": int(match.group(1)),
            "count": int(match.group(2)),
            "type": match.group(3),
        })
    return sorted(out, key=lambda a: a["index"])


_LOCATION_TYPE_RE = re.compile(
    r'!"air\.location_index", i32 (\d+), i32 \d+(?:[^}]*?)!"air\.arg_type_name", !"([^"]+)"'
)


def parse_patch_inputs_ll(ll: str) -> list[dict]:
    group = re.search(r'!\{i32 \d+, !"air\.patch_control_point_input",([^}]*)\}', ll)
    if not group:
        return []
    refs = re.findall(r"!(\d+)", group.group(1))
    by_ref = {}
    for match in re.finditer(r'^!(\d+) = !\{([^\n]*)\}', ll, re.M):
        body = match.group(2)
        loc = _LOCATION_TYPE_RE.search(body)
        if loc:
            by_ref[match.group(1)] = {
                "index": int(loc.group(1)),
                "type": loc.group(2),
            }
    return sorted((by_ref[ref] for ref in refs if ref in by_ref), key=lambda a: a["index"])


_STAGE_INPUT_RE = re.compile(
    r'!"air\.stage_in",\s*!"air\.location_index", i32 (\d+), i32 \d+.*?'
    r'!"air\.arg_type_name", !"([^"]+)"'
)


def parse_stage_inputs_ll(ll: str) -> list[dict]:
    """Kernel ``[[stage_in]]`` attributes declared by AIR metadata."""
    out = []
    for match in _STAGE_INPUT_RE.finditer(ll):
        out.append({"index": int(match.group(1)), "type": match.group(2)})
    return sorted(out, key=lambda item: item["index"])


_RT_A_RE = re.compile(
    r'!\{!"air\.render_target", i32 (\d+), i32 (\d+),([^}]*?)!"air\.arg_type_name", !"([^"]+)"'
)
_RT_B_RE = re.compile(
    r'!\{i32 \d+, !"air\.render_target", i32 (\d+),([^}]*?)!"air\.arg_type_name", !"([^"]+)"'
)


def parse_render_targets_ll(ll: str) -> list[dict]:
    """Collect colour-target declarations, separating fragment outputs from reads.

    AIR writes outputs as ``{!"air.render_target", slot, kind, ...}`` and
    framebuffer-fetch inputs as ``{arg_index, !"air.render_target", slot, ...}``.
    Both need the attachment present; only the output constrains write components.
    """
    seen = set()
    out = []
    for match in _RT_A_RE.finditer(ll):
        row = {"slot": int(match.group(1)), "form": "output", "type": match.group(4)}
        key = (row["slot"], row["form"], row["type"])
        if key not in seen:
            seen.add(key)
            out.append(row)
    for match in _RT_B_RE.finditer(ll):
        row = {"slot": int(match.group(1)), "form": "input", "type": match.group(3)}
        key = (row["slot"], row["form"], row["type"])
        if key not in seen:
            seen.add(key)
            out.append(row)
    return sorted(out, key=lambda r: (r["slot"], r["form"], r["type"]))


def patch_shader(ll: str) -> bool:
    # The pipeline-level patch marker; ``air.patch_control_point_input`` alone is a
    # metadata detail and must not turn an ordinary vertex function into tessellation.
    return '"air.patch"' in ll


def ir_compare_values(ll: str, constant: dict) -> list[int]:
    """Best-effort IR literal candidates for one constant.

    Only exact integer literals used in an ``icmp``/``switch`` against a value loaded
    from the constant's global are returned.  They are labelled as weak inference,
    never as the application's value.
    """
    symbol = constant.get("symbol")
    index = constant.get("index")
    if not symbol or index is None:
        return []
    base = re.sub(r"\.MTL_FC_INIT_\d+_.*$", "", symbol)
    values = set()
    loads = re.findall(
        r"%([\w.]+) = load i\d+, ptr addrspace\(2\) @" + re.escape(base) + r"\b", ll
    )
    for reg in loads:
        for match in re.finditer(
            r"icmp (?:eq|ne|slt|sle|sgt|sge|ult|ule|ugt|uge) i\d+ %" + re.escape(reg)
            + r", (-?\d+)", ll
        ):
            values.add(int(match.group(1)))
        for match in re.finditer(
            r"icmp (?:eq|ne|slt|sle|sgt|sge|ult|ule|ugt|uge) i\d+ (-?\d+), %"
            + re.escape(reg), ll
        ):
            values.add(int(match.group(1)))
        for match in re.finditer(r"switch i\d+ %" + re.escape(reg) + r", [^{]+\{([^}]*)\}", ll):
            for case in re.findall(r"i\d+ (-?\d+)", match.group(1)):
                values.add(int(case))
    return sorted(values)


# ---------------------------------------------------------------------------
# Descriptor/variant planning (pure; unit-tested).
# ---------------------------------------------------------------------------

# The frozen render bridge accepts only these colour-attachment formats:
# R8Unorm=10, RGBA8Unorm=70, BGRA8Unorm=80, RGBA16Float=115.
_BRIDGE_ATTACHMENT_FORMATS = {10, 70, 80, 115}
# Metal integer colour-attachment formats the frozen bridge does not expose yet.
# These are recorded as *requests* in blocked rows, never fabricated as bindings.
_INTEGER_ATTACHMENT_FORMATS = {
    ("uint", 1, 1): (13, "R8Uint"),
    ("uint", 1, 2): (23, "R16Uint"),
    ("uint", 1, 4): (53, "R32Uint"),
    ("uint", 2, 4): (103, "RG32Uint"),
    ("uint", 4, 4): (123, "RGBA32Uint"),
    ("sint", 1, 1): (14, "R8Sint"),
    ("sint", 1, 2): (24, "R16Sint"),
    ("sint", 1, 4): (54, "R32Sint"),
    ("sint", 2, 4): (104, "RG32Sint"),
    ("sint", 4, 4): (124, "RGBA32Sint"),
}


def _integer_attachment_requirement(rows: list[dict]) -> dict:
    family = rows[0]["family"]
    outputs = [row["components"] for row in rows if row["form"] == "output"]
    # A format's component count must not exceed any writer's component count; a
    # framebuffer-fetch input alone does not constrain the format width.
    component_count = min(outputs) if outputs else max(row["components"] for row in rows)
    element_bytes = None
    for row in rows:
        t = mtl_type(row["type"])
        if t is not None:
            element_bytes = t.size // t.components
            break
    if element_bytes is None:
        return {}
    key = (family, component_count, element_bytes)
    hit = _INTEGER_ATTACHMENT_FORMATS.get(key)
    if hit:
        return {"required_metal_pixel_format": hit[0],
                "required_metal_pixel_format_name": hit[1]}
    # A scalar integer output may use any wider integer format; record the natural
    # 32-bit scalar request. RGB32 integer attachments do not exist in Metal, so a
    # three-component integer target needs an explicit bridge decision.
    if component_count == 1 and element_bytes in (1, 2, 4):
        table = (13, 23, 53) if family == "uint" else (14, 24, 54)
        names = ("R8Uint", "R16Uint", "R32Uint") if family == "uint" else (
            "R8Sint", "R16Sint", "R32Sint")
        index = {1: 0, 2: 1, 4: 2}[element_bytes]
        return {"required_metal_pixel_format": table[index],
                "required_metal_pixel_format_name": names[index]}
    return {"required_metal_pixel_format": None,
            "required_metal_pixel_format_name": None,
            "note": "no Metal integer colour-attachment format has this component count"}
_MAX_COLOUR_ATTACHMENTS = 8


def _target_type_info(type_name: str) -> dict | None:
    t = mtl_type(type_name)
    if t is None:
        return None
    return {"type": type_name, "family": "uint" if t.family == "bool" else t.family,
            "components": t.components}


def plan_attachments(targets: list[dict]) -> dict:
    """Build a finite, explicit colour-attachment set from declared target types.

    Returns ``blocked`` with an exact reason when the frozen bridge's format surface
    cannot express the declaration.  A blocked result must never be counted as a
    shader refusal.
    """
    if not targets:
        return {"blocked": "no_declared_colour_targets", "slots": [], "attachments": []}
    slots = sorted({t["slot"] for t in targets})
    if slots and slots[-1] > _MAX_COLOUR_ATTACHMENTS - 1:
        return {"blocked": "colour_attachment_slot_out_of_bridge_range", "slots": slots,
                "attachments": []}
    if len(slots) > _MAX_COLOUR_ATTACHMENTS:
        return {"blocked": "colour_attachment_count_out_of_bridge_range", "slots": slots,
                "attachments": []}
    by_slot: dict[int, list[dict]] = {}
    for target in targets:
        info = _target_type_info(target["type"])
        if info is None:
            return {"blocked": "unknown_colour_target_type", "slots": slots,
                    "attachments": [], "detail": target}
        by_slot.setdefault(target["slot"], []).append({**target, **info})

    attachments = []
    blocked_slots = []
    reasons = []
    for slot in range(slots[-1] + 1):
        rows = by_slot.get(slot, [])
        if not rows:
            # The explicit attachment ABI keeps inactive attachments present; a zero
            # write mask is the declaration's "not written", not a substituted value.
            attachments.append({"slot": slot, "format": 10, "write_mask": 0,
                                "reason": "inactive_contiguous_slot"})
            continue
        families = {row["family"] for row in rows}
        if len(families) != 1:
            blocked_slots.append(slot)
            reasons.append({"slot": slot, "reason": "mixed_numeric_class",
                            "families": sorted(families)})
            continue
        family = next(iter(families))
        output_components = [row["components"] for row in rows if row["form"] == "output"]
        if family == "float":
            # BGRA8 when every writer has four components, otherwise R8Unorm.  Both
            # are float-class; the latter is the only bridge format that satisfies a
            # one/two/three-component writer.
            fmt = 80 if (output_components and min(output_components) >= 4) else 10
        else:
            blocked_slots.append(slot)
            reasons.append({"slot": slot, "reason": "bridge_attachment_format_surface",
                            "numeric": family,
                            "note": "frozen bridge accepts R8/RGBA8/BGRA8/RGBA16F only",
                            **_integer_attachment_requirement(rows)})
            continue
        has_output = any(row["form"] == "output" for row in rows)
        attachments.append({"slot": slot, "format": fmt,
                            "write_mask": 15 if has_output else 0,
                            "reason": "declared_target" if has_output else "framebuffer_fetch_input"})
    if blocked_slots:
        return {
            "blocked": "bridge_attachment_format_surface",
            "slots": slots,
            "attachments": attachments,
            "blocked_slots": blocked_slots,
            "reasons": reasons,
            "format_surface": sorted(_BRIDGE_ATTACHMENT_FORMATS),
        }
    return {"blocked": None, "slots": slots, "attachments": attachments,
            "blocked_slots": [], "reasons": []}


def plan_vertex_descriptor(ll: str) -> dict:
    """Derive a tightly packed synthetic vertex descriptor from AIR declarations."""
    if patch_shader(ll):
        inputs = parse_patch_inputs_ll(ll)
        step_function = 4  # MTLVertexStepFunctionPerPatchControlPoint
        provenance = "air.patch_control_point_input"
    else:
        inputs = parse_vertex_inputs_ll(ll)
        step_function = 1  # MTLVertexStepFunctionPerVertex
        provenance = "air.vertex_input"
    attributes = []
    offset = 0
    for item in inputs:
        fmt = VERTEX_FORMAT.get(item["type"])
        t = mtl_type(item["type"])
        if fmt is None or t is None:
            return {"blocked": "unknown_vertex_input_type", "attributes": [], "layouts": [],
                    "detail": item, "provenance": provenance}
        offset = (offset + 3) // 4 * 4
        attributes.append({"index": item["index"], "format": fmt, "offset": offset,
                           "buffer_index": 0})
        offset += t.size
    if not attributes:
        return {"blocked": None, "attributes": [], "layouts": [], "provenance": provenance}
    stride = max(16, (offset + 15) // 16 * 16)
    return {
        "blocked": None,
        "attributes": attributes,
        "layouts": [{"index": 0, "stride": stride, "step_function": step_function,
                     "step_rate": 1}],
        "provenance": provenance,
    }


def merge_vertex_descriptor(primary: dict, fallback: dict) -> dict:
    """Append missing AIR-derived attributes/layouts to a synthetic descriptor."""
    attrs = list(primary.get("attributes", []))
    layouts = list(primary.get("layouts", []))
    have = {a["index"] for a in attrs}
    for attr in fallback.get("attributes", []):
        if attr["index"] not in have:
            attrs.append(attr)
            have.add(attr["index"])
    lay = {l["index"] for l in layouts}
    for layout in fallback.get("layouts", []):
        if layout["index"] not in lay:
            layouts.append(layout)
            lay.add(layout["index"])
    attrs.sort(key=lambda a: a["index"])
    layouts.sort(key=lambda l: l["index"])
    return {**primary, "attributes": attrs, "layouts": layouts,
            "merged_with_air": True, "provenance": "job_descriptor+air_derived"}


def metal_stage_input_descriptor(ll: str) -> dict | None:
    """JSON form of a kernel's Metal StageInputDescriptor.

    A kernel ``[[stage_in]]`` fetch steps by thread position in the grid. The
    synthetic descriptor packs attributes in location order; the exact
    application buffer layout is not known and is labelled synthetic.
    """
    inputs = parse_stage_inputs_ll(ll)
    if not inputs:
        return None
    attrs = {}
    offset = 0
    for item in inputs:
        fmt_name = VERTEX_FORMAT_NAMES.get(VERTEX_FORMAT.get(item["type"]))
        t = mtl_type(item["type"])
        if fmt_name is None or t is None:
            return {"blocked": "unknown_stage_input_type", "detail": item}
        offset = (offset + 3) // 4 * 4
        attrs[str(item["index"])] = {
            "format": fmt_name, "offset": offset, "buffer_index": 0,
        }
        offset += t.size
    stride = max(4, (offset + 3) // 4 * 4)
    return {
        "attributes": attrs,
        "layouts": {"0": {"stride": stride,
                          "step_function": "ThreadPositionInGridX",
                          "step_rate": 1}},
        "index_type": "UInt16",
        "index_buffer_index": 0,
    }


def metal_vertex_descriptor(job: dict, vertex_ll: str) -> dict | None:
    """The JSON form of the Metal vertex descriptor for the standalone CLI.

    The translator CLI takes ``BTreeMap<u32, ...>`` serialized as JSON objects;
    step functions and vertex formats are Metal enum *names*.
    """
    attrs = list(job.get("attributes", []))
    layouts = list(job.get("layouts", []))
    if not attrs and not layouts:
        derived = plan_vertex_descriptor(vertex_ll)
        if derived.get("blocked"):
            return {"blocked": derived["blocked"], "detail": derived.get("detail")}
        attrs = derived.get("attributes", [])
        layouts = derived.get("layouts", [])
    if not attrs:
        return None
    out_attrs = {}
    for attr in attrs:
        name = VERTEX_FORMAT_NAMES.get(attr.get("format"))
        if name is None:
            return {"blocked": "unknown_cli_vertex_format",
                    "detail": attr}
        out_attrs[str(attr["index"])] = {
            "format": name,
            "offset": attr["offset"],
            "buffer_index": attr["buffer_index"],
        }
    out_layouts = {}
    for layout in layouts:
        name = STEP_FUNCTION_NAMES.get(layout.get("step_function"))
        if name is None:
            return {"blocked": "unknown_cli_step_function", "detail": layout}
        out_layouts[str(layout["index"])] = {
            "stride": layout["stride"],
            "step_function": name,
            "step_rate": layout["step_rate"],
        }
    return {"attributes": out_attrs, "layouts": out_layouts}


def cli_descriptor_plan(job: dict, vertex_ll: str, attachment_plan: dict) -> dict:
    """Translate the synthetic attachment set to exact Metal pixel-format names.

    This is the audit-only route for formats the frozen C bridge does not expose.
    It does not change or bypass the production bridge; it lets the translation
    layer answer whether the shader semantics are reachable with a correct target
    format, while the native compiler verdict remains separate.
    """
    attachments = attachment_plan.get("attachments", [])
    reasons = {row.get("slot"): row for row in attachment_plan.get("reasons", [])}
    slots = attachment_plan.get("slots", [])
    if not slots:
        return {"blocked": "no_declared_colour_targets"}
    max_slot = max(slots)
    by_slot = {row.get("slot"): row for row in attachments}
    color_formats = []
    for slot in range(max_slot + 1):
        att = by_slot.get(slot)
        if att is not None:
            name = ATTACHMENT_FORMAT_NAMES.get(att.get("format"))
        else:
            reason = reasons.get(slot, {})
            name = reason.get("required_metal_pixel_format_name")
        if name is None:
            return {"blocked": "no_metal_format_for_slot", "slot": slot,
                    "reason": reasons.get(slot)}
        color_formats.append({"slot": slot, "format": name})
    vertex_descriptor = metal_vertex_descriptor(job, vertex_ll)
    if vertex_descriptor and vertex_descriptor.get("blocked"):
        return {"blocked": vertex_descriptor["blocked"], "detail": vertex_descriptor}
    if job.get("vertex", {}).get("library_sha256") != job.get("fragment", {}).get("library_sha256"):
        return {"blocked": "separate_source_libraries"}
    return {
        "blocked": None,
        "library": job["vertex"].get("library_saved_as"),
        "function": job["vertex"].get("name"),
        "fragment": job["fragment"].get("name"),
        "color_formats": color_formats,
        "vertex_descriptor": vertex_descriptor,
    }


# ---------------------------------------------------------------------------
# Function-constant value policies (pure; unit-tested).
# ---------------------------------------------------------------------------


def choose_constant_value(constant: dict, policy: str, descriptor: dict | None = None,
                          attachments: list[dict] | None = None,
                          optional_defined: bool = False) -> dict:
    """Return one labelled, explicit value for a declared constant.

    ``descriptor_derived`` values come from the synthetic descriptor that is actually
    being compiled (attachment count, has_attachmentN, vertex layout).  Everything
    else is a named diagnostic branch choice.
    """
    name = constant.get("name", "")
    type_name = constant.get("type", "")
    t = mtl_type(type_name)
    if t is None:
        raise ValueError(f"unsupported constant type: {type_name}")

    origin = f"synthetic_diagnostic:{policy}"
    value = None
    evidence = None

    if name == "fc_attachment_count" and attachments is not None:
        value = len(attachments)
        origin = "descriptor_derived:attachment_count"
        evidence = "attachment set length"
    else:
        match = re.fullmatch(r"fc_has_attachment_(\d+)", name)
        if match and attachments is not None:
            slot = int(match.group(1))
            value = 1 if slot < len(attachments) and attachments[slot].get("write_mask", 0) else 0
            origin = "descriptor_derived:has_attachment"
            evidence = f"attachment slot {slot}"
        elif name in ("fc_dest_count",):
            value = 1
            origin = "descriptor_derived:single_target"
        elif name == "fc_vertex_layout" and descriptor is not None:
            value = 0
            origin = "descriptor_derived:vertex_layout"
        elif name in ("fc_attachment_0",) and attachments is not None:
            value = 1 if attachments else 0
            origin = "descriptor_derived:attachment_0"

    if value is None:
        if t.family == "bool":
            value = 1 if policy == "canonical" else 0
        elif t.family == "uint":
            value = 1 if policy == "canonical" else 2
        elif t.family == "sint":
            value = 1 if policy == "canonical" else -1
        else:
            value = 0.5 if policy == "canonical" else 1.0

    raw = encode_scalar(type_name, value)
    if raw is None:
        raise ValueError(f"cannot encode {value!r} as {type_name}")
    if optional_defined and not constant.get("required") and origin.startswith("synthetic"):
        origin += ":optional_defined"
        evidence = "explicitly defined optional constant (is_function_constant_defined=true)"
    return {
        "index": constant["index"],
        "name": name,
        "type": type_name,
        "size": len(raw),
        "value_hex": raw.hex(),
        "value": value_label(type_name, raw),
        "origin": origin,
        "evidence": evidence,
        # The bridge accepts either selector.  Names are used by default because they
        # are stable across container variants and are the selector the captured
        # device fixtures use; the index remains recorded for audit.
        "selector": "name",
    }


def initial_constant_rows(constants: list[dict], policy: str,
                          descriptor: dict | None = None,
                          attachments: list[dict] | None = None,
                          include_optional: bool = False) -> list[dict]:
    rows = []
    for constant in constants:
        if not constant.get("required") and not include_optional:
            continue
        if mtl_type(constant.get("type", "")) is None:
            raise ValueError(f"unsupported declared constant type: {constant.get('type')}")
        rows.append(choose_constant_value(
            constant, policy, descriptor, attachments,
            optional_defined=include_optional and not constant.get("required")))
    return rows


# ---------------------------------------------------------------------------
# Error classification (pure; unit-tested).
# ---------------------------------------------------------------------------

_RE_FC = re.compile(r"needs pipeline input: FunctionConstant \{ index: (\d+) \}")
_RE_INPUT = re.compile(r"needs pipeline input: ([A-Za-z_]+)")


def classify_error(error: str, stage: str | None = None) -> dict:
    error = error or ""
    match = _RE_FC.search(error)
    if match:
        return {"class": "missing_function_constant", "input": "FunctionConstant",
                "index": int(match.group(1)), "stage": stage}
    match = _RE_INPUT.search(error)
    if match:
        return {"class": "missing_pipeline_input", "input": match.group(1), "stage": stage}
    if error == "unsupported expanded render stage topology":
        return {"class": "bridge_stage_topology_refused", "stage": stage}
    if "attachment format or write mask outside bounded class" in error:
        return {"class": "bridge_attachment_format_surface", "stage": stage}
    if error.startswith("invalid descriptor: ColorAttachment"):
        index = re.search(r"ColorAttachment \{ index: (\d+) \}", error)
        return {"class": "colour_attachment_descriptor", "stage": stage,
                "attachment": int(index.group(1)) if index else None}
    if error.startswith("invalid descriptor: VertexAttribute"):
        index = re.search(r"attribute: (\d+)", error)
        return {"class": "vertex_attribute_descriptor", "stage": stage,
                "attribute": int(index.group(1)) if index else None}
    if error.startswith("invalid descriptor: VertexLayout"):
        index = re.search(r"layout: (\d+)", error)
        return {"class": "vertex_layout_descriptor", "stage": stage,
                "layout": int(index.group(1)) if index else None}
    if "Intrinsic(SignatureMismatch)" in error:
        return {"class": "air_decoder_signature_mismatch", "stage": stage}
    if error.startswith("unsupported: Tessellation"):
        return {"class": "translator_tessellation_unsupported", "stage": stage}
    if error.startswith("unsupported: Reflection"):
        return {"class": "translator_reflection_unsupported", "stage": stage}
    if error.startswith("unsupported:"):
        feature = error[len("unsupported:"):].split(";")[0].strip()
        return {"class": "translator_unsupported", "feature": feature, "stage": stage}
    if error.startswith("invalid input:"):
        return {"class": "air_input_invalid", "stage": stage}
    if error.startswith("invalid descriptor:"):
        return {"class": "descriptor_invalid", "stage": stage}
    if error.startswith("LimitExceeded") or error.startswith("limit exceeded"):
        return {"class": "translator_limit_exceeded", "stage": stage}
    if not error:
        return {"class": "no_error", "stage": stage}
    return {"class": "unclassified", "stage": stage, "raw": error}


# ---------------------------------------------------------------------------
# Ledger.
# ---------------------------------------------------------------------------


def build_ledger(corpus: Path, out_dir: Path) -> dict:
    coverage_path = corpus / "coverage.jsonl"
    entries = list(read_jsonl(coverage_path))
    ledger_path = out_dir / "ledger.jsonl"
    out_dir.mkdir(parents=True, exist_ok=True)
    tmp = ledger_path.with_name(ledger_path.name + f".tmp.{os.getpid()}")
    counts: dict[str, int] = {}
    exclusion_count = 0
    other_carriers = 0
    unpaired_count = 0
    bounded_unknown = 0
    with tmp.open("w", encoding="utf-8") as fh:
        for row in entries:
            record = {"record": "entry", "schema": SCHEMA, **row}
            fh.write(json.dumps(record, separators=(",", ":")) + "\n")
            counts[row.get("status", "unknown")] = counts.get(row.get("status", "unknown"), 0) + 1
            if row.get("status") == "bounded_attempt_killed_or_timeout":
                bounded_unknown += 1
                fh.write(json.dumps({
                    "record": "cpu_limit_unknown", "schema": SCHEMA,
                    "name": row.get("name"), "stage": row.get("stage"),
                    "library_source": row.get("library_source"),
                    "air_sha256": row.get("air_sha256"),
                    "attempts": row.get("attempts", []),
                    "note": "CPU/wall limit reached; no compiler verdict is implied",
                }, separators=(",", ":")) + "\n")

        embedded_path = corpus / "embedded-scan.json"
        if embedded_path.exists():
            embedded = read_json(embedded_path)
            for item in embedded.get("excluded_special_files", []):
                fh.write(json.dumps({
                    "record": "extraction_exclusion", "schema": SCHEMA,
                    "source": item.get("source"), "mode": item.get("mode"),
                    "bytes": item.get("bytes"),
                    "reason": "special_or_extent_refused",
                    "evidence": "embedded-scan.json",
                }, separators=(",", ":")) + "\n")
                exclusion_count += 1

        other_path = corpus / "other-formats.json"
        if other_path.exists():
            other = read_json(other_path)
            for lib in other.get("libraries", []):
                counts_for_lib = other.get("entry_counts", {}).get(lib.get("source"), 0)
                fh.write(json.dumps({
                    "record": "other_carrier", "schema": SCHEMA,
                    "source": lib.get("source"), "sha256": lib.get("sha256"),
                    "bytes": lib.get("bytes"), "status": lib.get("status"),
                    "entry_count": counts_for_lib,
                    "note": "not an ordinary MTLB entry; coverage rows carry the symbols/helpers",
                }, separators=(",", ":")) + "\n")
                other_carriers += 1

        jobs_path = corpus / "graphics-jobs.json"
        if jobs_path.exists():
            jobs = read_json(jobs_path)
            for row in jobs.get("unpaired", []):
                fh.write(json.dumps({
                    "record": "unpaired_graphics_entry", "schema": SCHEMA, **row,
                }, separators=(",", ":")) + "\n")
                unpaired_count += 1
    tmp.replace(ledger_path)

    summary = {
        "schema": SCHEMA,
        "tool_sha256": tool_sha256(),
        "ledger": str(ledger_path),
        "ledger_sha256": sha256_file(ledger_path),
        "entry_rows": len(entries),
        "entry_status_counts": counts,
        "extraction_exclusions": exclusion_count,
        "other_carriers": other_carriers,
        "unpaired_graphics_entries": unpaired_count,
        "cpu_limit_unknowns": bounded_unknown,
        "evidence_hashes": {
            name: sha256_file(corpus / name)
            for name in ("coverage.jsonl", "translation-failure-classification.json",
                         "functions.json", "graphics-jobs.json", "embedded-scan.json",
                         "other-formats.json")
            if (corpus / name).exists()
        },
        "scope": (
            "18190 coverage rows are preserved including non-render/helper/other-carrier "
            "statuses; 2430 special/extent exclusions and 9 unpaired graphics entries are "
            "explicit rows. This is an inventory ledger, not a compilation claim."
        ),
    }
    write_json(out_dir / "ledger-summary.json", summary)
    return summary


# ---------------------------------------------------------------------------
# Case selection and variant planning.
# ---------------------------------------------------------------------------


def load_cases(corpus: Path) -> list[dict]:
    path = corpus / "translation-failure-classification.json"
    return read_json(path)["cases"]


def load_entries(corpus: Path) -> dict[str, dict]:
    data = read_json(corpus / "functions.json")
    return {row["air_sha256"]: row for row in data["entries"]}


def load_jobs(corpus: Path) -> dict[str, dict]:
    path = corpus / "graphics-jobs.json"
    if not path.exists():
        return {}
    return {job["id"]: job for job in read_json(path)["jobs"]}


def _case_key(case: dict) -> str:
    return Path(case["evidence_directory"]).name


def select_cases(cases: list[dict], family: str, limit: int, ids: str | None,
                 representative: bool) -> list[dict]:
    if family == "all":
        selected = [c for c in cases if c.get("family") in (
            "color_attachment_format_or_MRT", "tessellation_patch_control_point_descriptor",
            "FunctionConstant")]
    elif family == "graphics_mrt":
        selected = [c for c in cases if c.get("family") == "color_attachment_format_or_MRT"]
    elif family == "graphics_patch":
        selected = [c for c in cases
                    if c.get("family") == "tessellation_patch_control_point_descriptor"]
    elif family == "graphics_fc":
        selected = [c for c in cases
                    if c.get("family") == "FunctionConstant" and c.get("kind") == "graphics"]
    elif family == "compute_fc":
        selected = [c for c in cases
                    if c.get("family") == "FunctionConstant" and c.get("kind") == "compute"]
    else:
        raise SystemExit(f"unknown family: {family}")

    if ids:
        wanted = {value.strip() for value in ids.split(",") if value.strip()}
        selected = [c for c in selected if _case_key(c) in wanted
                    or c.get("evidence_directory") in wanted]

    if representative:
        chosen = []
        seen = set()
        for case in selected:
            if case.get("family") == "color_attachment_format_or_MRT":
                key = (tuple(case.get("declared_target_slots", [])),
                       tuple(case.get("declared_target_types", [])))
            elif case.get("family") == "tessellation_patch_control_point_descriptor":
                key = case.get("source")
            elif case.get("kind") == "compute":
                matches = case.get("declared_constant_matches", [])
                key = (matches[0].get("type") if matches else None,
                       matches[0].get("name") if matches else None)
            else:
                matches = case.get("declared_constant_matches", [])
                key = (matches[0].get("function") if matches else None,
                       matches[0].get("type") if matches else None)
            if key not in seen:
                seen.add(key)
                chosen.append(case)
        selected = chosen

    selected.sort(key=lambda c: (c.get("library_source", c.get("source", "")),
                                 c.get("name", ""), _case_key(c)))
    if limit and limit > 0:
        selected = selected[:limit]
    return selected


def _function_constants_for(entry: dict | None, ll: str) -> list[dict]:
    declared = parse_function_constants_ll(ll)
    if declared:
        return declared
    # Fallback for a module whose AIR metadata omitted the group (should not happen
    # for ordinary MTLB entries): use the container's declarations and mark it.
    if not entry:
        return []
    out = []
    for row in entry.get("constants", []):
        t = MTL_BY_NUMERIC.get(row.get("type"))
        if t is None:
            continue
        out.append({
            "symbol": None, "type": t.name, "name": row.get("name"),
            "index": row.get("index"), "required": bool(row.get("flags")),
            "source": "mtlb_container_fallback",
        })
    return sorted(out, key=lambda c: (c["index"], c["name"]))


def _job_descriptor(job: dict) -> dict:
    return {
        "attributes": job.get("attributes", []),
        "layouts": job.get("layouts", []),
        "origin": ("captured_donor_recipe" if job.get("attachments")
                   else "synthetic_job_descriptor"),
        "job_id": job.get("id"),
        "pairing_evidence": job.get("pairing_evidence"),
        "descriptor_evidence": job.get("descriptor_evidence"),
    }


def _apply_captured_constants(rows: list[dict], captured: list[dict], air_sha: str,
                              function: str) -> list[dict]:
    by_name = {}
    for item in captured or []:
        if item.get("air_sha256") == air_sha and item.get("function") == function:
            by_name[item.get("name")] = item
    out = []
    for row in rows:
        hit = by_name.get(row.get("name"))
        if hit is None:
            out.append(row)
            continue
        t = mtl_type(hit.get("type"))
        if t is None or hit.get("value_hex") is None:
            out.append(row)
            continue
        raw = bytes.fromhex(hit["value_hex"])
        out.append({
            **row, "type": hit.get("type"), "size": len(raw),
            "value_hex": raw.hex(), "value": value_label(hit.get("type"), raw),
            "origin": "captured", "evidence": hit.get("source", "captured manifest"),
        })
    return out


def plan_variants(corpus: Path, out_dir: Path, family: str, limit: int, ids: str | None,
                  representative: bool, explore_alternates: bool,
                  captured_path: Path | None, include_optional: bool = False,
                  allow_compiler_change: bool = False,
                  compiler_path: Path | None = None,
                  bridge_path: Path | None = None,
                  profile_path: Path | None = None,
                  expected_compiler_sha256: str | None = None,
                  expected_bridge_sha256: str | None = None,
                  priority_manifest: Path | None = None,
                  compute_entry: str = "v1") -> dict:
    cases = load_cases(corpus)
    selected = select_cases(cases, family, 0 if priority_manifest else limit, ids,
                            representative)
    priority_rank = None
    if priority_manifest:
        priority_rows = list(read_jsonl(priority_manifest))
        priority_rank = {row["case"]: int(row["selection_rank"])
                         for row in priority_rows}
        selected = [case for case in selected if _case_key(case) in priority_rank]
        selected.sort(key=lambda case: priority_rank[_case_key(case)])
        if limit and limit > 0:
            selected = selected[:limit]
    entries = load_entries(corpus)
    jobs = load_jobs(corpus)
    captured = read_json(captured_path).get("values", []) if captured_path else []
    compiler = (compiler_path or corpus / "baseline" / "libnord2_mali_runtime.so").resolve()
    bridge = (bridge_path or corpus / "baseline" / "libtegu_air_bridge.so").resolve()
    profile = (profile_path or corpus / "baseline" / "tegu-air-profile.h").resolve()
    cli_path = ROOT / "gpu" / "build" / "cargo-target" / "release" / "metal2vulkan"
    hashes = {
        "compiler_sha256": sha256_file(compiler),
        "bridge_sha256": sha256_file(bridge),
        "profile_sha256": sha256_file(profile),
        "translator_cli_sha256": sha256_file(cli_path) if cli_path.exists() else None,
    }
    if not allow_compiler_change:
        if expected_compiler_sha256 and hashes["compiler_sha256"] != expected_compiler_sha256:
            raise SystemExit("compiler hash mismatch: " + hashes["compiler_sha256"])
        if expected_bridge_sha256 and hashes["bridge_sha256"] != expected_bridge_sha256:
            raise SystemExit("bridge hash mismatch: " + hashes["bridge_sha256"])
    sources = {
        "compiler": source_rel(compiler),
        "bridge": source_rel(bridge),
        "profile": source_rel(profile),
    }

    variants = []
    plan_notes = []
    for case in selected:
        fam = case.get("family")
        key = _case_key(case)
        if fam == "FunctionConstant" and case.get("kind") == "compute":
            entry = entries.get(key)
            ll = read_ll(corpus, key)
            declared = _function_constants_for(entry, ll)
            if not declared:
                plan_notes.append({"case": key, "blocked": "no_declared_function_constants"})
                continue
            stage_input = metal_stage_input_descriptor(ll)
            cli_plan = None
            if stage_input and not stage_input.get("blocked"):
                if not entry or not entry.get("library_saved_as"):
                    plan_notes.append({"case": key,
                                       "blocked": "stage_input_library_missing"})
                else:
                    cli_plan = {
                        "blocked": None,
                        "path": "gpu/build/cargo-target/release/metal2vulkan",
                        "sha256": hashes.get("translator_cli_sha256"),
                        "library": entry["library_saved_as"],
                        "function": case.get("name"),
                        "fragment": None,
                        "stage_input": stage_input,
                        "color_formats": [],
                    }
                    plan_notes.append({"case": key,
                                       "route": "cli_stage_input_descriptor"})
            elif stage_input and stage_input.get("blocked"):
                plan_notes.append({"case": key,
                                   "blocked": "stage_input:" + str(stage_input["blocked"])})
            policies = ["canonical"] + (["alternate"] if explore_alternates else [])
            optional_modes = [False] + ([True] if include_optional else [])
            for policy in policies:
                for optional_defined in optional_modes:
                    try:
                        initial = initial_constant_rows(
                            declared, policy, include_optional=optional_defined)
                    except ValueError as exc:
                        plan_notes.append({"case": key, "blocked": str(exc)})
                        continue
                    initial = _apply_captured_constants(initial, captured, key,
                                                        case.get("name", ""))
                    core = {
                        "schema": SCHEMA, "kind": "compute_fc", "case": key,
                        "family": fam, "baseline": case,
                        "policy": policy + (":optional_defined" if optional_defined else ""),
                        "optional_mode": "defined" if optional_defined else "undefined",
                        "compiler": hashes, "sources": sources, "kernel": {
                            "air": f"air/{key}.air", "sha256": key,
                            "name": case.get("name"), "stage": "kernel"},
                        "fragment": None,
                        "declared_constants": {"kernel": declared},
                        "initial_constants": {"kernel": initial},
                        "descriptor_variants": [],
                        "create_mode": "compute",
                        "compute_entry": compute_entry,
                        "translator": "cli" if cli_plan else "bridge",
                        "cli": cli_plan,
                    }
                    variants.append(_finalise_variant(core, case))
        elif case.get("kind") == "graphics":
            job = jobs.get(key)
            if not job:
                plan_notes.append({"case": key, "blocked": "graphics_job_missing"})
                continue
            vertex_ll = read_ll(corpus, job["vertex"]["air_sha256"])
            fragment_ll = read_ll(corpus, job["fragment"]["air_sha256"])
            declared = {
                "vertex": _function_constants_for(entries.get(job["vertex"]["air_sha256"]), vertex_ll),
                "fragment": _function_constants_for(entries.get(job["fragment"]["air_sha256"]), fragment_ll),
            }
            policies = ["canonical"] + (["alternate"] if explore_alternates else [])
            if include_optional:
                policies += [p + ":optional_defined" for p in list(policies)]
            for policy_label in policies:
                optional_defined = policy_label.endswith(":optional_defined")
                policy = (policy_label[:-len(":optional_defined")]
                          if optional_defined else policy_label)
                initial = {}
                for stage, rows in declared.items():
                    try:
                        stage_rows = initial_constant_rows(
                            rows, policy, include_optional=optional_defined)
                    except ValueError as exc:
                        plan_notes.append({"case": key, "blocked": str(exc)})
                        continue
                    stage_rows = _apply_captured_constants(
                        stage_rows, captured,
                        job[stage]["air_sha256"], job[stage]["name"])
                    initial[stage] = stage_rows
                descriptor_variants = []
                attachment_note = None
                cli_plan = None
                cli_blocked = None
                if fam == "color_attachment_format_or_MRT":
                    targets = parse_render_targets_ll(fragment_ll)
                    attachment_plan = plan_attachments(targets)
                    descriptor = _job_descriptor(job)
                    if not descriptor["attributes"]:
                        air_derived = plan_vertex_descriptor(vertex_ll)
                        descriptor["air_derived"] = air_derived
                    base = {
                        "label": "declared_targets",
                        "attachments": attachment_plan["attachments"],
                        "blocked": attachment_plan["blocked"],
                        "blocked_slots": attachment_plan.get("blocked_slots", []),
                        "reasons": attachment_plan.get("reasons", []),
                        "attributes": descriptor.get("attributes", []),
                        "layouts": descriptor.get("layouts", []),
                        "provenance": "synthetic_mrt_descriptor",
                        "plan": attachment_plan,
                    }
                    descriptor_variants.append(base)
                    if attachment_plan["blocked"] is None:
                        alt = json.loads(json.dumps(base))
                        for i, att in enumerate(alt["attachments"]):
                            if att.get("write_mask") and att.get("format") == 80:
                                alt["attachments"][i]["format"] = 70
                        alt["label"] = "declared_targets_rgba8"
                        descriptor_variants.append(alt)
                    elif attachment_plan["blocked"] == "bridge_attachment_format_surface":
                        # The frozen C bridge has no integer target formats.  Use the
                        # existing translator CLI to answer the *translation* question
                        # with exact Metal formats; this is audit-only and is recorded
                        # separately from production bridge support.
                        cli_plan = cli_descriptor_plan(job, vertex_ll, attachment_plan)
                        if cli_plan.get("blocked"):
                            cli_blocked = "cli:" + str(cli_plan["blocked"])
                        else:
                            cli_plan.update({
                                "path": "gpu/build/cargo-target/release/metal2vulkan",
                                "sha256": hashes.get("translator_cli_sha256"),
                            })
                            base = {
                                "label": "cli_declared_targets",
                                "attachments": attachment_plan["attachments"],
                                "blocked": None,
                                "blocked_slots": [],
                                "reasons": attachment_plan.get("reasons", []),
                                "attributes": descriptor.get("attributes", []),
                                "layouts": descriptor.get("layouts", []),
                                "provenance": "metal2vulkan_cli_declared_targets",
                                "plan": attachment_plan,
                            }
                            descriptor_variants = [base]
                    attachment_note = attachment_plan
                elif fam == "tessellation_patch_control_point_descriptor":
                    descriptor = _job_descriptor(job)
                    air_derived = plan_vertex_descriptor(vertex_ll)
                    if air_derived.get("blocked"):
                        plan_notes.append({"case": key,
                                           "blocked": air_derived["blocked"]})
                        continue
                    targets = parse_render_targets_ll(fragment_ll)
                    attachment_plan = plan_attachments(targets)
                    base = {
                        "label": "patch_control_point",
                        "attachments": attachment_plan["attachments"],
                        "blocked": attachment_plan["blocked"],
                        "blocked_slots": attachment_plan.get("blocked_slots", []),
                        "reasons": attachment_plan.get("reasons", []),
                        "attributes": air_derived["attributes"],
                        "layouts": air_derived["layouts"],
                        "provenance": "air.patch_control_point_input",
                    }
                    descriptor_variants.append(base)
                    alt = json.loads(json.dumps(base))
                    for layout in alt["layouts"]:
                        layout["step_function"] = 3  # PerPatch (diagnostic alternative)
                    alt["label"] = "patch_control_point_as_per_patch"
                    descriptor_variants.append(alt)
                    attachment_note = attachment_plan
                else:  # graphics FunctionConstant
                    descriptor = _job_descriptor(job)
                    # The baseline job descriptor already reached the function-constant
                    # check, so it is the primary probe.  A declared-target attachment
                    # set is only an additive second probe.
                    if job.get("attachments"):
                        primary_attachments = job["attachments"]
                        primary_origin = "captured_donor_recipe"
                    else:
                        primary_attachments = [{"format": job.get("format", 80),
                                                "write_mask": 15, "reason": "baseline_job"}]
                        primary_origin = descriptor["origin"]
                    base = {
                        "label": "job_descriptor",
                        "attachments": primary_attachments,
                        "blocked": None,
                        "blocked_slots": [],
                        "reasons": [],
                        "attributes": descriptor.get("attributes", []),
                        "layouts": descriptor.get("layouts", []),
                        "provenance": primary_origin,
                    }
                    descriptor_variants.append(base)
                    targets = parse_render_targets_ll(fragment_ll)
                    attachment_plan = plan_attachments(targets)
                    if attachment_plan["blocked"] is None and targets:
                        alt = {
                            "label": "declared_targets",
                            "attachments": attachment_plan["attachments"],
                            "blocked": None,
                            "blocked_slots": [],
                            "reasons": [],
                            "attributes": descriptor.get("attributes", []),
                            "layouts": descriptor.get("layouts", []),
                            "provenance": "synthetic_mrt_descriptor",
                        }
                        descriptor_variants.append(alt)
                    attachment_note = None
                core = {
                    "schema": SCHEMA, "kind": _kind_for(fam), "case": key,
                    "family": fam, "baseline": case, "policy": policy_label,
                    "optional_mode": "defined" if optional_defined else "undefined",
                    "compiler": hashes,
                    "sources": sources,
                    "vertex": {"air": job["vertex"]["air_saved_as"],
                               "sha256": job["vertex"]["air_sha256"],
                               "name": job["vertex"]["name"], "stage": "vertex"},
                    "fragment": {"air": job["fragment"]["air_saved_as"],
                                 "sha256": job["fragment"]["air_sha256"],
                                 "name": job["fragment"]["name"], "stage": "fragment"},
                    "declared_constants": declared,
                    "initial_constants": initial,
                    "descriptor_variants": descriptor_variants,
                    "create_mode": "attachments",
                    "baseline_descriptor_error": case.get("error"),
                    "air_derived": plan_vertex_descriptor(vertex_ll),
                    "translator": "cli" if cli_plan and not cli_blocked else "bridge",
                    "cli": cli_plan if cli_plan and not cli_blocked else None,
                }
                if cli_blocked:
                    core["descriptor_blocked"] = cli_blocked
                elif attachment_note and attachment_note.get("blocked") and not cli_plan:
                    core["descriptor_blocked"] = attachment_note["blocked"]
                variants.append(_finalise_variant(core, case))
    out_dir.mkdir(parents=True, exist_ok=True)
    variant_path = out_dir / "variants.jsonl"
    tmp = variant_path.with_name(variant_path.name + f".tmp.{os.getpid()}")
    with tmp.open("w", encoding="utf-8") as fh:
        for variant in variants:
            fh.write(json.dumps(variant, separators=(",", ":")) + "\n")
    tmp.replace(variant_path)
    summary = {
        "schema": SCHEMA,
        "tool_sha256": tool_sha256(),
        "family": family,
        "selected_cases": len(selected),
        "variants": len(variants),
        "runnable": sum(1 for v in variants if not v.get("descriptor_blocked")),
        "descriptor_blocked": sum(1 for v in variants if v.get("descriptor_blocked")),
        "notes": plan_notes,
        "compilers": hashes,
        "sources": sources,
        "priority_manifest": str(priority_manifest) if priority_manifest else None,
        "priority_order": bool(priority_manifest),
        "compute_entry": compute_entry,
        "frozen_baseline": (hashes["compiler_sha256"] == FROZEN_COMPILER
                            and hashes["bridge_sha256"] == FROZEN_BRIDGE),
        "variants_sha256": sha256_file(variant_path),
        "scope": (
            "One finite, labelled variant set per case. Synthetic constants are diagnostic "
            "branch choices, not application values. Unbounded value spaces are not enumerated."
        ),
    }
    write_json(out_dir / "plan-summary.json", summary)
    return summary


def _kind_for(family: str) -> str:
    if family == "color_attachment_format_or_MRT":
        return "graphics_mrt"
    if family == "tessellation_patch_control_point_descriptor":
        return "graphics_patch"
    return "graphics_fc"


def _finalise_variant(core: dict, case: dict) -> dict:
    digest = sha256_bytes(json.dumps(core, sort_keys=True, separators=(",", ":")).encode())
    return {
        **core,
        "id": "ci-" + digest[:20],
        "variant_sha256": digest,
        "baseline_family": case.get("family"),
        "baseline_error": case.get("error"),
    }


# ---------------------------------------------------------------------------
# ctypes ABI used by the isolated worker.
# ---------------------------------------------------------------------------


class BridgeBlob(C.Structure):
    _fields_ = [("bytes", C.POINTER(C.c_uint8)), ("length", C.c_size_t)]


class CompilerBlob(C.Structure):
    _fields_ = [("data", C.c_void_p), ("size", C.c_size_t)]


class FunctionConstant(C.Structure):
    _fields_ = [("kind", C.c_uint32), ("type", C.c_uint32), ("index", C.c_uint32),
                ("size", C.c_uint32), ("name", C.POINTER(C.c_uint8)),
                ("name_len", C.c_size_t), ("data", C.c_uint8 * 64)]


class VertexAttribute(C.Structure):
    _fields_ = [("index", C.c_uint32), ("format", C.c_uint32), ("offset", C.c_uint32),
                ("buffer_index", C.c_uint32)]


class VertexLayout(C.Structure):
    _fields_ = [("index", C.c_uint32), ("stride", C.c_uint32),
                ("step_function", C.c_uint32), ("step_rate", C.c_uint32)]


class Attachment(C.Structure):
    _fields_ = [("format", C.c_uint32), ("write_mask", C.c_uint32)]


class Graphics(C.Structure):
    _fields_ = [("code", C.c_void_p), ("size", C.c_size_t)] + [
        (name, C.c_uint32) for name in
        ("stage", "registers", "tls", "fau_words", "sysval_mask", "push_bytes")
    ] + [("preload", C.c_uint64)]


class Varying(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("location", "offset", "components", "alu_type")]


class Pair(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in (
        "version", "flags", "position_offset", "position_size", "point_position_offset",
        "point_position_size", "secondary_offset", "secondary_size",
        "secondary_registers", "generic_size_B")] + [("secondary_preload", C.c_uint64)] + [
        (name, C.c_uint32) for name in
        ("vs_attribute_mask", "vs_varying_mask", "fs_varying_mask", "fs_uses_ld_var",
         "noperspective_mask", "needs_extended_fifo", "point_size_section",
         "point_size_offset", "point_size_alu_type", "varying_count")
    ] + [("varyings", Varying * 4)]


class Binding(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("binding", "kind", "stage_mask", "declared_bytes",
                 "input_attachment_index")] + [("reserved", C.c_uint32 * 3)]


class Plan(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("version", "count", "reads_framebuffer", "reserved")] + [
        ("bindings", Binding * 16)]


def configure_bridge(path: Path):
    lib = C.CDLL(str(path.resolve()))
    lib.tegu_air_library_open.restype = C.c_void_p
    lib.tegu_air_library_open.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
    lib.tegu_air_library_specialize.restype = C.c_void_p
    lib.tegu_air_library_specialize.argtypes = [
        C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(FunctionConstant), C.c_size_t,
        C.c_void_p, C.c_size_t]
    lib.tegu_air_library_close.argtypes = [C.c_void_p]
    lib.tegu_air_blob_free.argtypes = [C.POINTER(BridgeBlob)]
    lib.tegu_air_compile_compute.restype = C.c_int
    lib.tegu_air_compile_compute.argtypes = [
        C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
        C.POINTER(BridgeBlob), C.POINTER(BridgeBlob), C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_create_with_attachments.restype = C.c_void_p
    lib.tegu_air_render_pipeline_create_with_attachments.argtypes = [
        C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p, C.c_size_t,
        C.POINTER(Attachment), C.c_size_t, C.POINTER(VertexAttribute), C.c_size_t,
        C.POINTER(VertexLayout), C.c_size_t, C.c_void_p, C.c_size_t,
        C.POINTER(BridgeBlob), C.POINTER(BridgeBlob), C.POINTER(BridgeBlob),
        C.c_void_p, C.c_size_t]
    lib.tegu_air_render_pipeline_close.argtypes = [C.c_void_p]
    return lib


def configure_compiler(path: Path):
    lib = C.CDLL(str(path.resolve()))
    lib.tegu_mali_compile.restype = C.c_int
    lib.tegu_mali_compile.argtypes = [
        C.c_void_p, C.c_size_t, C.POINTER(CompilerBlob), C.c_void_p, C.c_size_t]
    try:
        lib.tegu_mali_compile_compute_v2.restype = C.c_int
        lib.tegu_mali_compile_compute_v2.argtypes = [
            C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
            C.POINTER(CompilerBlob), C.c_void_p, C.c_size_t]
    except AttributeError:
        lib.tegu_mali_compile_compute_v2 = None
    lib.tegu_mali_compile_generic_pair.restype = C.c_int
    lib.tegu_mali_compile_generic_pair.argtypes = [
        C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
        C.POINTER(Graphics), C.POINTER(Graphics), C.POINTER(Pair),
        C.POINTER(Plan), C.c_void_p, C.c_size_t]
    return lib


def _constant_structs(rows: list[dict]) -> tuple[object, list]:
    array = (FunctionConstant * max(1, len(rows)))()
    keepalive = []
    for i, row in enumerate(rows):
        raw = bytes.fromhex(row["value_hex"])
        array[i].type = mtl_type(row["type"]).numeric
        array[i].index = row["index"]
        array[i].size = len(raw)
        array[i].data[:len(raw)] = raw
        if row.get("selector", "name") == "name":
            name = row["name"].encode()
            buffer = (C.c_uint8 * max(1, len(name))).from_buffer_copy(name or b"\0")
            keepalive.append(buffer)
            array[i].kind = 1
            array[i].name = buffer
            array[i].name_len = len(name)
        else:
            array[i].kind = 0
            array[i].name = None
            array[i].name_len = 0
    return array, keepalive


def specialize_library(bridge, base, function: str, rows: list[dict], error: C.Array) -> int | None:
    # Always specialize, even with zero supplied constants.  The Rust bridge
    # returns an owned clone for count=0; returning the base handle here and then
    # closing it in a retry was a use-after-free (SIGSEGV) in this audit tool.
    array, keepalive = _constant_structs(rows)
    error.value = b""
    handle = bridge.tegu_air_library_specialize(
        base, function.encode(), len(function),
        C.cast(array, C.POINTER(FunctionConstant)), len(rows), error, len(error))
    del keepalive
    return handle or None


# ---------------------------------------------------------------------------
# Isolated worker.
# ---------------------------------------------------------------------------


def _limit_resources() -> None:
    try:
        cpu = int(os.environ.get("N2_AUDIT_CPU_SECONDS", "12"))
    except ValueError:
        cpu = 12
    try:
        resource.setrlimit(resource.RLIMIT_AS, (2147483648, 2147483648))
        resource.setrlimit(resource.RLIMIT_CPU, (cpu, cpu))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    except (ValueError, OSError):
        pass


def _blob_bytes(blob: BridgeBlob) -> bytes:
    if not blob.bytes or not blob.length:
        return b""
    return C.string_at(blob.bytes, blob.length)


def _cli_value_text(row: dict) -> str:
    value = row.get("value")
    if isinstance(value, list):
        return ",".join("true" if item is True else
                        "false" if item is False else str(item) for item in value)
    if value is True:
        return "true"
    if value is False:
        return "false"
    return str(value)


def _cli_constant_args(spec: dict, values: dict) -> list[str]:
    args = []
    for stage in ("vertex", "fragment", "kernel"):
        for row in values.get(stage, []):
            if row.get("name") is None:
                continue
            args += ["--constant",
                     f"{row['name']}:{row['type']}:{_cli_value_text(row)}"]
    return args


def worker_cli_main(spec: dict, corpus: Path, out_dir: Path) -> int:
    """Audit-only translation through the existing standalone metal2vulkan CLI.

    The CLI is not the production C ABI.  It is used only to answer the translator
    question for target formats the frozen bridge cannot expose.  The result keeps
    the CLI hash and the bridge surface limitation separate.
    """
    _limit_resources()
    try:
        import faulthandler
        faulthandler.enable()
    except Exception:
        pass
    result = {
        "id": spec["id"], "schema": SCHEMA, "kind": spec["kind"],
        "case": spec["case"], "family": spec.get("family"),
        "policy": spec.get("policy"), "variant_sha256": spec.get("variant_sha256"),
        "tool_sha256": tool_sha256(),
        "translator_backend": "metal2vulkan_cli",
        "translation_status": "not_attempted",
        "compile_status": "not_attempted",
        "hardware_executed": False,
        "constants": spec.get("initial_constants", {}),
        "attempts": [],
        "descriptor": None,
    }
    cli = spec.get("cli") or {}
    cli_path = ROOT / cli.get("path", "")
    if not cli_path.exists():
        result["worker_error"] = "translator CLI missing"
        write_json(out_dir / "result.json", result)
        return 2
    cli_sha = sha256_file(cli_path)
    result["cli_sha256"] = cli_sha
    if cli.get("sha256") and cli_sha != cli["sha256"]:
        result["worker_error"] = "translator CLI hash changed"
        write_json(out_dir / "result.json", result)
        return 2
    profile_path = out_dir / "profile.json"
    sources = spec.get("sources", {})
    compiler_path = resolve_source(sources.get("compiler"), corpus,
                                   "baseline/libnord2_mali_runtime.so")
    profile_source = resolve_source(sources.get("profile"), corpus,
                                    "baseline/tegu-air-profile.h")
    expected = spec.get("compiler", {})
    if expected.get("compiler_sha256") and sha256_file(compiler_path) != expected["compiler_sha256"]:
        result["worker_error"] = "compiler source hash changed"
        write_json(out_dir / "result.json", result)
        return 2
    if expected.get("profile_sha256") and sha256_file(profile_source) != expected["profile_sha256"]:
        result["worker_error"] = "profile source hash changed"
        write_json(out_dir / "result.json", result)
        return 2
    atomic_write(profile_path, profile_bytes(profile_source))
    vd_path = None
    vertex_descriptor = cli.get("vertex_descriptor")
    if vertex_descriptor:
        vd_path = out_dir / "vertex-descriptor.json"
        write_json(vd_path, vertex_descriptor)
    stage_input_path = None
    stage_input_descriptor = cli.get("stage_input")
    if stage_input_descriptor:
        stage_input_path = out_dir / "stage-input.json"
        write_json(stage_input_path, stage_input_descriptor)
    is_compute = spec["kind"] == "compute_fc"
    values = {stage: [dict(row) for row in spec.get("initial_constants", {}).get(stage, [])]
              for stage in ("vertex", "fragment", "kernel")}
    declared = spec.get("declared_constants", {})
    cli_out = out_dir / "cli-out.spv"
    result["descriptor"] = {
        "label": "cli_stage_input" if is_compute else "cli_declared_targets",
        "color_formats": cli.get("color_formats", []),
        "vertex_descriptor": vertex_descriptor,
        "stage_input": stage_input_descriptor,
        "provenance": ("metal2vulkan_cli_stage_input" if is_compute
                       else "metal2vulkan_cli_declared_targets"),
        "blocked": None,
    }
    for round_index in range(1, 6):
        for suffix in (".vert.spv", ".frag.spv", ".tesc.spv", ".tese.spv"):
            try:
                (out_dir / ("cli-out" + suffix)).unlink()
            except FileNotFoundError:
                pass
        args = [str(cli_path), cli.get("library", ""), str(cli_out),
                "--profile", str(profile_path),
                "--function", cli.get("function", ""),
                "--timeout-ms", "15000",
                "--emit-meta", str(out_dir / "cli-meta.json")]
        if not is_compute:
            args += ["--fragment", cli.get("fragment", "")]
        if stage_input_path is not None:
            args += ["--stage-input", str(stage_input_path)]
        if vd_path is not None:
            args += ["--vertex-descriptor", str(vd_path)]
        for item in cli.get("color_formats", []):
            args += ["--color-format", f"{item['slot']}:{item['format']}"]
        args += _cli_constant_args(spec, values)
        env = {**os.environ, "PYTHONHASHSEED": "0",
               "METAL2VULKAN_REPRO_DIR": str(out_dir / "repro")}
        try:
            proc = subprocess.run(args, cwd=str(corpus), capture_output=True,
                                  text=True, timeout=25, env=env)
        except subprocess.TimeoutExpired:
            result.update({"translation_status": "refused",
                           "constants": values,
                           "translation_error": "translator CLI timeout",
                           "translation_classification": {"class": "cli_timeout"}})
            write_json(out_dir / "result.json", result)
            return 0
        stdout = proc.stdout or ""
        stderr = proc.stderr or ""
        atomic_write(out_dir / "cli.log", stdout + stderr)
        attempts_row = {
            "round": round_index,
            "returncode": proc.returncode,
            "color_formats": cli.get("color_formats", []),
            "constants": values,
            "stdout_tail": stdout[-1200:],
            "stderr_tail": stderr[-1200:],
        }
        result["attempts"].append(attempts_row)
        if proc.returncode != 0:
            failure = re.search(r"compile failed: (.+)", stderr)
            if failure:
                error = failure.group(1).strip()
            else:
                error = (stderr.strip().splitlines() or stdout.strip().splitlines() or
                         ["translator CLI failed"])[-1]
            classified = classify_error(error)
            if classified["class"] == "missing_function_constant":
                if _add_missing_constant(values, declared, ["vertex", "fragment", "kernel"],
                                         classified["index"], spec.get("policy", "canonical"),
                                         None, None, result["attempts"]):
                    continue
            result.update({
                "translation_status": "refused",
                "constants": values,
                "translation_error": error,
                "translation_classification": classified,
            })
            write_json(out_dir / "result.json", result)
            return 0
        if is_compute:
            if not cli_out.exists():
                result.update({
                    "translation_status": "refused",
                    "constants": values,
                    "translation_error": "translator CLI produced no kernel SPIR-V",
                    "translation_classification": {"class": "cli_no_output"},
                })
                write_json(out_dir / "result.json", result)
                return 0
            spv = cli_out.read_bytes()
            meta_path = out_dir / "cli-meta.json"
            meta = meta_path.read_bytes() if meta_path.exists() else b""
            translation = {
                "status": "translated",
                "backend": "metal2vulkan_cli",
                "cli_sha256": cli_sha,
                "specialization_status": "ok",
                "constants": values,
                "descriptor": result["descriptor"],
                "spirv_bytes": len(spv),
                "spirv_sha256": sha256_bytes(spv),
                "metadata_bytes": len(meta),
                "metadata_sha256": sha256_bytes(meta),
                "attempts": result["attempts"],
            }
            write_json(out_dir / "translation.json", translation)
            atomic_write(out_dir / "compute.spv", spv)
            if meta:
                atomic_write(out_dir / "metadata.json", meta)
            result["translation_status"] = "translated"
            result["translation_artifacts"] = translation
            result["constants"] = values
            compiler = configure_compiler(compiler_path)
            error = C.create_string_buffer(4096)
            out_blob = CompilerBlob()
            compute_entry = spec.get("compute_entry", "v1")
            if compute_entry == "v2" and \
                    compiler.tegu_mali_compile_compute_v2 is not None:
                rc = compiler.tegu_mali_compile_compute_v2(
                    spv, len(spv), None, 0, C.byref(out_blob), error, len(error))
            else:
                rc = compiler.tegu_mali_compile(
                    spv, len(spv), C.byref(out_blob), error, len(error))
                compute_entry = "v1"
            result.update({
                "compute_entry": compute_entry,
                "compile_status": "compiled" if rc == 0 else "refused",
                "compile_rc": rc,
                "compile_error": error.value.decode(errors="replace"),
            })
            if rc == 0:
                result["package_bytes"] = out_blob.size
                if out_blob.data and out_blob.size >= 8:
                    result["package_magic"] = C.string_at(out_blob.data, 8).hex()
            write_json(out_dir / "result.json", result)
            return 0
        vert = out_dir / "cli-out.vert.spv"
        frag = out_dir / "cli-out.frag.spv"
        expanded = [name for name in ("cli-out.tesc.spv", "cli-out.tese.spv")
                    if (out_dir / name).exists()]
        if not vert.exists() or not frag.exists():
            result.update({
                "translation_status": "refused",
                "constants": values,
                "translation_error": "translator CLI produced no vertex/fragment SPIR-V",
                "translation_classification": {"class": "cli_no_output"},
            })
            write_json(out_dir / "result.json", result)
            return 0
        if expanded:
            result.update({
                "translation_status": "refused",
                "constants": values,
                "translation_error": "unsupported expanded render stage topology",
                "translation_classification": {"class": "bridge_stage_topology_refused"},
                "expanded_stages": expanded,
            })
            write_json(out_dir / "result.json", result)
            return 0
        vs = vert.read_bytes()
        fs = frag.read_bytes()
        meta_path = out_dir / "cli-meta.json"
        meta = meta_path.read_bytes() if meta_path.exists() else b""
        translation = {
            "status": "translated",
            "backend": "metal2vulkan_cli",
            "cli_sha256": cli_sha,
            "specialization_status": "ok",
            "constants": values,
            "descriptor": result["descriptor"],
            "vertex_spirv_bytes": len(vs), "vertex_spirv_sha256": sha256_bytes(vs),
            "fragment_spirv_bytes": len(fs), "fragment_spirv_sha256": sha256_bytes(fs),
            "metadata_bytes": len(meta), "metadata_sha256": sha256_bytes(meta),
            "attempts": result["attempts"],
        }
        write_json(out_dir / "translation.json", translation)
        atomic_write(out_dir / "vertex.spv", vs)
        atomic_write(out_dir / "fragment.spv", fs)
        if meta:
            atomic_write(out_dir / "metadata.json", meta)
        result["translation_status"] = "translated"
        result["translation_artifacts"] = translation
        result["constants"] = values
        compiler = configure_compiler(compiler_path)
        error = C.create_string_buffer(4096)
        vb = C.create_string_buffer(vs)
        fb = C.create_string_buffer(fs)
        gv, gf, pair, plan = Graphics(), Graphics(), Pair(), Plan()
        rc = compiler.tegu_mali_compile_generic_pair(
            vb, len(vs), fb, len(fs), C.byref(gv), C.byref(gf),
            C.byref(pair), C.byref(plan), error, len(error))
        result.update({
            "compile_status": "compiled" if rc == 0 else "refused",
            "compile_rc": rc,
            "compile_error": error.value.decode(errors="replace"),
            "vertex_code_bytes": gv.size, "fragment_code_bytes": gf.size,
            "vertex_registers": gv.registers, "fragment_registers": gf.registers,
            "binding_count": plan.count, "reads_framebuffer": plan.reads_framebuffer,
        })
        write_json(out_dir / "result.json", result)
        return 0
    result.update({
        "translation_status": "refused",
        "constants": values,
        "translation_error": "constant materialisation exhausted five rounds",
        "translation_classification": {"class": "constant_round_limit"},
    })
    write_json(out_dir / "result.json", result)
    return 0


def _free_blob(bridge, blob: BridgeBlob) -> None:
    if blob.bytes:
        bridge.tegu_air_blob_free(C.byref(blob))


def _decl_for(declared: dict, stage: str, index: int) -> dict | None:
    for row in declared.get(stage, []):
        if row.get("index") == index:
            return row
    return None


def _add_missing_constant(values: dict, declared: dict, stages: list[str], index: int,
                          policy: str, descriptor: dict | None, attachments: list[dict] | None,
                          history: list) -> bool:
    added = False
    for stage in stages:
        if any(row["index"] == index for row in values.get(stage, [])):
            continue
        decl = _decl_for(declared, stage, index)
        if decl is None:
            continue
        try:
            row = choose_constant_value(decl, policy, descriptor, attachments)
        except ValueError as exc:
            history.append({"stage": stage, "index": index, "error": str(exc)})
            continue
        values.setdefault(stage, []).append(row)
        history.append({"stage": stage, "index": index, "name": decl.get("name"),
                        "type": decl.get("type"), "value_hex": row["value_hex"],
                        "origin": row["origin"]})
        added = True
    return added


def worker_main(spec_path: Path, corpus: Path, out_dir: Path) -> int:
    _limit_resources()
    try:
        import faulthandler
        faulthandler.enable()
    except Exception:
        pass
    spec = read_json(spec_path)
    if spec.get("translator") == "cli":
        return worker_cli_main(spec, corpus, out_dir)
    result = {
        "id": spec["id"], "schema": SCHEMA, "kind": spec["kind"],
        "case": spec["case"], "family": spec.get("family"),
        "policy": spec.get("policy"), "variant_sha256": spec.get("variant_sha256"),
        "tool_sha256": tool_sha256(),
        "baseline_error": spec.get("baseline_error"),
        "translation_status": "not_attempted",
        "compile_status": "not_attempted",
        "hardware_executed": False,
        "constants": {"vertex": [], "fragment": [], "kernel": []},
        "descriptor": None,
        "attempts": [],
        "started_unix": time.time(),
    }
    try:
        sources = spec.get("sources", {})
        bridge_path = resolve_source(sources.get("bridge"), corpus,
                                     "baseline/libtegu_air_bridge.so")
        compiler_path = resolve_source(sources.get("compiler"), corpus,
                                       "baseline/libnord2_mali_runtime.so")
        profile_source = resolve_source(sources.get("profile"), corpus,
                                        "baseline/tegu-air-profile.h")
        expected = spec.get("compiler", {})
        for label, path, key in (
                ("compiler", compiler_path, "compiler_sha256"),
                ("bridge", bridge_path, "bridge_sha256"),
                ("profile", profile_source, "profile_sha256")):
            if expected.get(key) and sha256_file(path) != expected[key]:
                raise RuntimeError(f"{label} source hash changed")
        bridge = configure_bridge(bridge_path)
        compiler = configure_compiler(compiler_path)
        profile = profile_bytes(profile_source)
        error = C.create_string_buffer(4096)
        stages = [s for s in ("vertex", "fragment", "kernel") if spec.get(s)]
        bases = {}
        for stage in stages:
            claim = spec[stage]
            raw = (corpus / claim["air"]).read_bytes()
            handle = bridge.tegu_air_library_open(raw, len(raw), error, len(error))
            if not handle:
                raise RuntimeError(f"{stage} library open: " + error.value.decode(errors="replace"))
            bases[stage] = handle

        values = {stage: [dict(row) for row in spec.get("initial_constants", {}).get(stage, [])]
                  for stage in stages}
        declared = spec.get("declared_constants", {})
        descriptor_variants = list(spec.get("descriptor_variants", []))
        attempts = result["attempts"]
        progress_path = out_dir / "progress.json"

        def progress(phase: str, **extra) -> None:
            write_json(progress_path, {
                "id": spec["id"], "phase": phase,
                "values": values, "attempts": attempts,
                "updated_unix": time.time(), **extra,
            })

        progress("libraries_open")
        translation_done = False
        translation_error = ""
        translated = None
        descriptor_index = 0
        constant_round = 0
        descriptor_round = 0
        attachment_plan = None

        while constant_round < 8 and descriptor_round < 8:
            constant_round += 1
            handles = {}
            spec_error = None
            for stage in stages:
                progress("specialize_" + stage)
                handles[stage] = specialize_library(
                    bridge, bases[stage], spec[stage]["name"], values[stage], error)
                if handles[stage] is None:
                    spec_error = (f"{stage} specialization refused: "
                                  + error.value.decode(errors="replace"))
                    break
            if spec_error:
                translation_error = spec_error
                result["specialization_status"] = "refused"
                break
            result["specialization_status"] = "ok"

            if spec["kind"] == "compute_fc":
                spv, meta = BridgeBlob(), BridgeBlob()
                error.value = b""
                progress("translate_compute")
                rc = bridge.tegu_air_compile_compute(
                    handles["kernel"], spec["kernel"]["name"].encode(),
                    len(spec["kernel"]["name"]), profile, len(profile),
                    C.byref(spv), C.byref(meta), error, len(error))
                message = error.value.decode(errors="replace")
                attempts.append({"round": constant_round, "descriptor": None,
                                 "translation_error": message if rc else ""})
                if rc == 0:
                    translated = _blob_bytes(spv)
                    metadata = _blob_bytes(meta)
                    write_json(out_dir / "translation.json", {
                        "status": "translated", "kind": spec["kind"],
                        "specialization_status": "ok",
                        "constants": values,
                        "spirv_bytes": len(translated),
                        "spirv_sha256": sha256_bytes(translated),
                        "metadata_bytes": len(metadata),
                        "metadata_sha256": sha256_bytes(metadata),
                        "attempts": attempts,
                    })
                    atomic_write(out_dir / "compute.spv", translated)
                    atomic_write(out_dir / "metadata.json", metadata)
                    translation_done = True
                    break
                classified = classify_error(message)
                if classified["class"] == "missing_function_constant":
                    if _add_missing_constant(values, declared, stages, classified["index"],
                                             spec.get("policy", "canonical"), None, None,
                                             attempts):
                        for stage in stages:
                            bridge.tegu_air_library_close(handles[stage])
                        continue
                translation_error = message
                break

            # Graphics: try descriptor variants, refining on the exact descriptor error.
            if descriptor_index >= len(descriptor_variants):
                translation_error = "no descriptor variant could be attempted"
                break
            variant = descriptor_variants[descriptor_index]
            descriptor_round += 1
            if variant.get("blocked"):
                result["descriptor"] = {"label": variant.get("label"),
                                        "blocked": variant["blocked"],
                                        "reasons": variant.get("reasons", [])}
                translation_error = "descriptor blocked: " + variant["blocked"]
                break
            attachments = variant.get("attachments", [])
            attrs = variant.get("attributes", [])
            layouts = variant.get("layouts", [])
            att_structs = (Attachment * len(attachments))(
                *[Attachment(a["format"], a["write_mask"]) for a in attachments])
            attr_structs = (VertexAttribute * len(attrs))(
                *[VertexAttribute(a["index"], a["format"], a["offset"], a["buffer_index"])
                  for a in attrs])
            layout_structs = (VertexLayout * len(layouts))(
                *[VertexLayout(l["index"], l["stride"], l["step_function"], l["step_rate"])
                  for l in layouts])
            out = [BridgeBlob(), BridgeBlob(), BridgeBlob()]
            error.value = b""
            progress("translate_graphics", descriptor=variant.get("label"),
                     attachments=attachments, attributes=attrs, layouts=layouts)
            pipeline = bridge.tegu_air_render_pipeline_create_with_attachments(
                handles["vertex"], spec["vertex"]["name"].encode(), len(spec["vertex"]["name"]),
                handles["fragment"], spec["fragment"]["name"].encode(),
                len(spec["fragment"]["name"]),
                att_structs, len(attachments), attr_structs, len(attrs),
                layout_structs, len(layouts), profile, len(profile),
                C.byref(out[0]), C.byref(out[1]), C.byref(out[2]), error, len(error))
            message = error.value.decode(errors="replace")
            attempts.append({"round": constant_round, "descriptor": variant.get("label"),
                             "attachments": attachments, "attributes": attrs,
                             "layouts": layouts, "translation_error": message})
            if pipeline:
                vs = _blob_bytes(out[0]); fs = _blob_bytes(out[1]); meta = _blob_bytes(out[2])
                write_json(out_dir / "translation.json", {
                    "status": "translated", "kind": spec["kind"],
                    "specialization_status": "ok",
                    "constants": values,
                    "descriptor": {"label": variant.get("label"),
                                   "attachments": attachments,
                                   "attributes": attrs, "layouts": layouts,
                                   "provenance": variant.get("provenance")},
                    "vertex_spirv_bytes": len(vs), "vertex_spirv_sha256": sha256_bytes(vs),
                    "fragment_spirv_bytes": len(fs), "fragment_spirv_sha256": sha256_bytes(fs),
                    "metadata_bytes": len(meta), "metadata_sha256": sha256_bytes(meta),
                    "attempts": attempts,
                })
                atomic_write(out_dir / "vertex.spv", vs)
                atomic_write(out_dir / "fragment.spv", fs)
                atomic_write(out_dir / "metadata.json", meta)
                translated = (vs, fs)
                result["descriptor"] = {"label": variant.get("label"),
                                        "attachments": attachments,
                                        "attributes": attrs, "layouts": layouts,
                                        "provenance": variant.get("provenance")}
                bridge.tegu_air_render_pipeline_close(pipeline)
                translation_done = True
                break
            classified = classify_error(message)
            if classified["class"] == "missing_function_constant":
                if _add_missing_constant(values, declared, ["vertex", "fragment"],
                                         classified["index"], spec.get("policy", "canonical"),
                                         variant, attachments, attempts):
                    for stage in stages:
                        bridge.tegu_air_library_close(handles[stage])
                    continue
            if classified["class"] in ("vertex_attribute_descriptor", "vertex_layout_descriptor"):
                air_derived = spec.get("air_derived", {})
                merged = merge_vertex_descriptor(variant, air_derived)
                if merged.get("attributes") != variant.get("attributes") or \
                        merged.get("layouts") != variant.get("layouts"):
                    merged["label"] = variant.get("label", "descriptor") + "+air_attributes"
                    descriptor_variants.append(merged)
                    descriptor_index += 1
                    translation_error = message
                    for stage in stages:
                        bridge.tegu_air_library_close(handles[stage])
                    continue
                if variant.get("layouts") and any(
                        l.get("step_function") == 4 for l in variant["layouts"]):
                    alt = json.loads(json.dumps(variant))
                    for layout in alt["layouts"]:
                        layout["step_function"] = 3
                    alt["label"] = variant.get("label", "") + "+per_patch"
                    descriptor_variants.append(alt)
                    descriptor_index += 1
                    for stage in stages:
                        bridge.tegu_air_library_close(handles[stage])
                    continue
            if classified["class"] == "colour_attachment_descriptor" and \
                    classified.get("attachment") is not None and \
                    classified["attachment"] >= len(attachments):
                # The translator named an attachment the baseline/synthetic descriptor
                # omitted (dual-source or framebuffer-fetch).  Extend it explicitly; the
                # format is a labelled diagnostic choice, not a captured target format.
                extended = json.loads(json.dumps(variant))
                while len(extended["attachments"]) <= classified["attachment"]:
                    extended["attachments"].append({
                        "format": 80, "write_mask": 15,
                        "reason": "translator_requested_extension"})
                extended["label"] = (variant.get("label", "descriptor")
                                     + f"+attachment{classified['attachment']}")
                extended["provenance"] = "translator_requested_extension"
                descriptor_variants.append(extended)
                descriptor_index += 1
                for stage in stages:
                    bridge.tegu_air_library_close(handles[stage])
                continue
            if classified["class"] == "colour_attachment_descriptor" and \
                    descriptor_index + 1 < len(descriptor_variants):
                descriptor_index += 1
                for stage in stages:
                    bridge.tegu_air_library_close(handles[stage])
                continue
            translation_error = message
            break

        if not translation_done:
            if not translation_error:
                translation_error = "descriptor refinement limit reached"
            result["translation_status"] = "refused"
            result["translation_error"] = translation_error
            result["translation_classification"] = classify_error(translation_error)
            result["constants"] = {stage: values[stage] for stage in stages}
            write_json(out_dir / "result.json", result)
            return 0

        result["translation_status"] = "translated"
        result["constants"] = {stage: values[stage] for stage in stages}
        error.value = b""
        progress("mali")
        mali_start = time.time()
        if spec["kind"] == "compute_fc":
            out_blob = CompilerBlob()
            compute_entry = spec.get("compute_entry", "v1")
            if compute_entry == "v2":
                if compiler.tegu_mali_compile_compute_v2 is None:
                    result.update({
                        "compile_status": "refused",
                        "compile_error": "tegu_mali_compile_compute_v2 missing",
                        "compute_entry": "v2-missing",
                    })
                    write_json(out_dir / "result.json", result)
                    return 0
                rc = compiler.tegu_mali_compile_compute_v2(
                    translated, len(translated), None, 0,
                    C.byref(out_blob), error, len(error))
            else:
                rc = compiler.tegu_mali_compile(translated, len(translated),
                                                C.byref(out_blob), error, len(error))
            result.update({
                "compile_status": "compiled" if rc == 0 else "refused",
                "compile_rc": rc,
                "compile_error": error.value.decode(errors="replace"),
                "compile_seconds": round(time.time() - mali_start, 3),
                "compute_entry": compute_entry,
            })
            if rc == 0:
                result["package_bytes"] = out_blob.size
                if out_blob.data and out_blob.size >= 8:
                    result["package_magic"] = C.string_at(out_blob.data, 8).hex()
        else:
            vs, fs = translated
            vb = C.create_string_buffer(vs)
            fb = C.create_string_buffer(fs)
            gv, gf, pair, plan = Graphics(), Graphics(), Pair(), Plan()
            rc = compiler.tegu_mali_compile_generic_pair(
                vb, len(vs), fb, len(fs), C.byref(gv), C.byref(gf),
                C.byref(pair), C.byref(plan), error, len(error))
            result.update({
                "compile_status": "compiled" if rc == 0 else "refused",
                "compile_rc": rc,
                "compile_error": error.value.decode(errors="replace"),
                "compile_seconds": round(time.time() - mali_start, 3),
                "vertex_code_bytes": gv.size,
                "fragment_code_bytes": gf.size,
                "vertex_registers": gv.registers,
                "fragment_registers": gf.registers,
                "binding_count": plan.count,
                "reads_framebuffer": plan.reads_framebuffer,
            })
        result["finished_unix"] = time.time()
        write_json(out_dir / "result.json", result)
        return 0
    except Exception as exc:  # keep a machine-readable failure, never a traceback-only run
        result["worker_error"] = f"{type(exc).__name__}: {exc}"
        result["translation_status"] = "worker_error"
        write_json(out_dir / "result.json", result)
        return 2


# ---------------------------------------------------------------------------
# Static input classification (all cases, no compiler invocation).
# ---------------------------------------------------------------------------


def _case_input_row(case: dict, entries: dict, jobs: dict, corpus: Path) -> dict:
    key = _case_key(case)
    fam = case.get("family")
    row = {
        "schema": SCHEMA, "case": key, "kind": case.get("kind"),
        "family": fam, "name": case.get("name"),
        "library_source": case.get("source"),
        "baseline_error": case.get("error"),
        "baseline_classification": case.get("classification"),
        "compiler_sha256": FROZEN_COMPILER,
        "bridge_sha256": FROZEN_BRIDGE,
        "hardware_executed": False,
        "compiled": False,
    }
    try:
        if case.get("kind") == "compute" and fam == "FunctionConstant":
            entry = entries.get(key)
            ll = read_ll(corpus, key)
            declared = _function_constants_for(entry, ll)
            row["air_sha256"] = key
            row["declared_constants"] = declared
            row["required_constant_count"] = sum(1 for c in declared if c.get("required"))
            row["optional_constant_count"] = sum(1 for c in declared if not c.get("required"))
            row["constant_types"] = sorted({c.get("type") for c in declared})
            if not declared:
                row["materialization_status"] = "no_declared_function_constants"
                return row
            try:
                initial = initial_constant_rows(declared, "canonical")
                row["initial_values"] = initial
                row["materialization_status"] = "materializable_function_constants"
                row["value_origin_note"] = (
                    "Values are explicit synthetic diagnostic branch choices; the declared "
                    "type/index/required flag is real AIR metadata, the value is not captured.")
            except ValueError as exc:
                row["materialization_status"] = "blocked:" + str(exc)
            return row

        job = jobs.get(key)
        if not job:
            row["materialization_status"] = "blocked:graphics_job_missing"
            return row
        vertex_ll = read_ll(corpus, job["vertex"]["air_sha256"])
        fragment_ll = read_ll(corpus, job["fragment"]["air_sha256"])
        row["vertex_air_sha256"] = job["vertex"]["air_sha256"]
        row["fragment_air_sha256"] = job["fragment"]["air_sha256"]
        row["job_pairing_evidence"] = job.get("pairing_evidence")
        row["descriptor_evidence"] = job.get("descriptor_evidence")
        declared = {
            "vertex": _function_constants_for(entries.get(job["vertex"]["air_sha256"]), vertex_ll),
            "fragment": _function_constants_for(entries.get(job["fragment"]["air_sha256"]), fragment_ll),
        }
        row["declared_constants"] = declared
        row["required_constant_count"] = sum(
            1 for rows in declared.values() for c in rows if c.get("required"))
        row["optional_constant_count"] = sum(
            1 for rows in declared.values() for c in rows if not c.get("required"))
        row["constant_types"] = sorted({c.get("type") for rows in declared.values() for c in rows})

        targets = parse_render_targets_ll(fragment_ll)
        attachment_plan = plan_attachments(targets)
        row["render_targets"] = targets
        row["attachment_plan"] = attachment_plan
        vertex_plan = plan_vertex_descriptor(vertex_ll)
        if fam == "tessellation_patch_control_point_descriptor":
            row["patch_metadata"] = True
            row["vertex_plan"] = vertex_plan
            if vertex_plan.get("blocked"):
                row["materialization_status"] = "blocked:" + vertex_plan["blocked"]
                return row
            if attachment_plan.get("blocked"):
                row["materialization_status"] = "blocked:" + attachment_plan["blocked"]
                row["blocked_slots"] = attachment_plan.get("blocked_slots", [])
                return row
            row["materialization_status"] = "materializable_patch_descriptor"
            row["stage_topology_note"] = (
                "A successful descriptor translation reaches the frozen bridge's expanded-"
                "stage refusal; this is a bridge topology gap, not a missing descriptor.")
            return row

        if fam == "color_attachment_format_or_MRT":
            row["vertex_plan"] = vertex_plan
            if attachment_plan.get("blocked"):
                row["production_bridge_blocked"] = attachment_plan["blocked"]
                row["blocked_slots"] = attachment_plan.get("blocked_slots", [])
                if attachment_plan["blocked"] == "bridge_attachment_format_surface":
                    cli_plan = cli_descriptor_plan(job, vertex_ll, attachment_plan)
                    row["cli_plan"] = cli_plan
                    cli_path = ROOT / "gpu" / "build" / "cargo-target" / "release" / "metal2vulkan"
                    row["translator_cli_path"] = "gpu/build/cargo-target/release/metal2vulkan"
                    row["translator_cli_sha256"] = (
                        sha256_file(cli_path) if cli_path.exists() else None)
                    if not cli_plan.get("blocked"):
                        row["materialization_status"] = "materializable_cli_colour_attachments"
                        row["note"] = (
                            "The frozen production C bridge lacks these integer target "
                            "formats; the existing translator CLI can materialise the exact "
                            "Metal formats. This is an audit route, not production support.")
                        return row
                    row["materialization_status"] = "blocked:" + str(cli_plan["blocked"])
                    return row
                row["materialization_status"] = "blocked:" + attachment_plan["blocked"]
                return row
            row["materialization_status"] = "materializable_colour_attachments"
            return row

        # graphics FunctionConstant: the baseline job descriptor is the primary probe.
        row["primary_descriptor"] = (
            job.get("attachments") if job.get("attachments")
            else [{"format": job.get("format", 80), "write_mask": 15,
                   "reason": "baseline_job"}])
        if not declared.get("vertex") and not declared.get("fragment"):
            row["materialization_status"] = "no_declared_function_constants"
        else:
            try:
                row["initial_values"] = {
                    stage: initial_constant_rows(rows, "canonical")
                    for stage, rows in declared.items()
                }
                row["materialization_status"] = "materializable_function_constants"
                row["value_origin_note"] = (
                    "Values are explicit synthetic diagnostic branch choices; the declared "
                    "type/index/required flag is real AIR metadata, the value is not captured.")
            except ValueError as exc:
                row["materialization_status"] = "blocked:" + str(exc)
        return row
    except Exception as exc:  # an audit row must remain explicit even on a parse error
        row["materialization_status"] = "blocked:analysis_error"
        row["analysis_error"] = f"{type(exc).__name__}: {exc}"
        return row


def classify_inputs(corpus: Path, out_dir: Path, family: str, limit: int, ids: str | None,
                    representative: bool) -> dict:
    cases = load_cases(corpus)
    selected = select_cases(cases, family, limit, ids, representative)
    entries = load_entries(corpus)
    jobs = load_jobs(corpus)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "inputs-ledger.jsonl"
    tmp = path.with_name(path.name + f".tmp.{os.getpid()}")
    status_counts: dict[str, int] = {}
    family_counts: dict[str, dict] = {}
    blocked_reasons: dict[str, int] = {}
    constant_type_counts: dict[str, int] = {}
    required_histogram: dict[str, int] = {}
    surface_requests: dict[str, int] = {}
    zero_required = 0
    optional_only = 0
    production_bridge_blocked = 0
    with tmp.open("w", encoding="utf-8") as fh:
        for case in selected:
            row = _case_input_row(case, entries, jobs, corpus)
            fh.write(json.dumps(row, separators=(",", ":")) + "\n")
            if row.get("production_bridge_blocked"):
                production_bridge_blocked += 1
            status = row["materialization_status"]
            status_counts[status] = status_counts.get(status, 0) + 1
            fam = row.get("family") or "unknown"
            family_counts.setdefault(fam, {})
            family_counts[fam][status] = family_counts[fam].get(status, 0) + 1
            if status.startswith("blocked:"):
                reason = status.split(":", 1)[1]
                blocked_reasons[reason] = blocked_reasons.get(reason, 0) + 1
            required = row.get("required_constant_count")
            if required is not None:
                required_histogram[str(required)] = required_histogram.get(str(required), 0) + 1
                if required == 0 and row.get("declared_constants"):
                    zero_required += 1
                    if row.get("materialization_status") == "materializable_function_constants":
                        optional_only += 1
            declared = row.get("declared_constants")
            if isinstance(declared, dict):
                for rows in declared.values():
                    for constant in rows:
                        type_name = constant.get("type")
                        constant_type_counts[type_name] = constant_type_counts.get(type_name, 0) + 1
            elif isinstance(declared, list):
                for constant in declared:
                    type_name = constant.get("type")
                    constant_type_counts[type_name] = constant_type_counts.get(type_name, 0) + 1
            for reason in (row.get("attachment_plan") or {}).get("reasons", []):
                name = reason.get("required_metal_pixel_format_name")
                if name:
                    surface_requests[name] = surface_requests.get(name, 0) + 1
    tmp.replace(path)
    summary = {
        "schema": SCHEMA,
        "tool_sha256": tool_sha256(),
        "family": family,
        "cases": len(selected),
        "status_counts": status_counts,
        "family_status_counts": family_counts,
        "blocked_reasons": blocked_reasons,
        "constant_type_counts": constant_type_counts,
        "required_constant_count_histogram": required_histogram,
        "cases_with_zero_required_constants": zero_required,
        "cases_optional_only": optional_only,
        "production_bridge_blocked_cases": production_bridge_blocked,
        "bridge_surface_requests": surface_requests,
        "ledger": str(path),
        "ledger_sha256": sha256_file(path),
        "compiler_sha256": FROZEN_COMPILER,
        "bridge_sha256": FROZEN_BRIDGE,
        "translator_cli_sha256": (
            sha256_file(ROOT / "gpu" / "build" / "cargo-target" / "release" / "metal2vulkan")
            if (ROOT / "gpu" / "build" / "cargo-target" / "release" / "metal2vulkan").exists()
            else None),
        "specialization_constraints": {
            "type_source": "AIR !air.function_constants type name; the MTLB numeric type is "
                           "recorded but is not authoritative when it is outside MTLDataType",
            "required_flag": "AIR metadata fifth operand i1; the MTLB CNST flag is also recorded",
            "required_rule": "every required constant must be supplied in the same specialization",
            "type_rule": "supplied MTLDataType and byte width must match the declared type exactly",
            "selector_rule": "a constant is supplied by name or index, never both; names are used "
                             "by default and the index remains in the ledger",
            "limits": "at most 256 constants, each at most 64 bytes",
            "default_semantics": "an unsupplied constant is undefined "
                                 "(is_function_constant_defined=false), not a zero value",
        },
        "scope": (
            "Static missing-input classification only. No compiler invocation, no SPIR-V, "
            "no Mali code and no hardware execution. Descriptor/constant values are "
            "explicitly synthetic or descriptor-derived and are never claimed as captured "
            "application state."
        ),
    }
    write_json(out_dir / "inputs-summary.json", summary)
    return summary


# ---------------------------------------------------------------------------
# Priority manifest for the remaining function-constant batches.
# ---------------------------------------------------------------------------

PRIORITY_SCHEMA = 1

# Source classes are deliberately explicit rather than inferred from a single
# substring: the installed corpus contains no SpringBoard or backboardd
# metallib in the remaining FC sets, so a truthful manifest must say so.
_P0_SOURCE_CLASSES = {
    "springboard", "backboard", "core_services", "frontboard", "baseboard",
    "coreui", "iosurface", "uikit", "swiftui",
}
_P1_SOURCE_CLASSES = {"ui_framework", "quartzcore", "renderbox", "corere"}


def source_class(source: str | None) -> str:
    s = (source or "").lower()
    if "springboard" in s:
        return "springboard"
    if "backboard" in s:
        return "backboard"
    if "quartzcore" in s:
        return "quartzcore"
    if "renderbox" in s:
        return "renderbox"
    if "corere" in s:
        return "corere"
    if "coreservices" in s:
        return "core_services"
    if "frontboard" in s:
        return "frontboard"
    if "baseboard" in s:
        return "baseboard"
    if "coreui" in s:
        return "coreui"
    if "iosurface" in s:
        return "iosurface"
    if "uikit" in s:
        return "uikit"
    if "swiftui" in s:
        return "swiftui"
    if any(token in s for token in (
            "vfx.framework", "vectorkit", "scenekit", "spritekit", "pencilkit",
            "metalkit", "arkit", "realitykit", "passkitui", "photosui",
            "siriui", "roomplan", "reality", "renderkit")):
        return "ui_framework"
    if any(token in s for token in (
            "espresso", "metalperformanceshaders", "mtlbvh", "metal.framework",
            "coreimage", "mps", "accelerate", "bnns", "vision.framework")):
        return "compute_ml"
    if "videoprocess" in s or "videodecoder" in s or "videoencoder" in s:
        return "video_processor"
    return "system_framework"


def priority_tier(source: str | None) -> int:
    cls = source_class(source)
    if cls in _P0_SOURCE_CLASSES:
        return 0
    if cls in _P1_SOURCE_CLASSES:
        return 1
    if cls == "compute_ml":
        return 2
    return 3


_SOURCE_BONUS = {
    "springboard": 9000, "backboard": 8500, "quartzcore": 8000,
    "renderbox": 7500, "corere": 7000, "core_services": 8000,
    "frontboard": 7800, "baseboard": 7600, "coreui": 7400,
    "iosurface": 7200, "uikit": 6800, "swiftui": 6600,
    "ui_framework": 3000, "compute_ml": 1500, "video_processor": 500,
    "system_framework": 1000,
}
_ORIGIN_RANK = {"captured": 2, "inferred": 1, "synthetic": 0}


def _flatten_constant_values(values) -> list[dict]:
    out = []
    if isinstance(values, dict):
        for rows in values.values():
            out.extend(_flatten_constant_values(rows))
    elif isinstance(values, list):
        for row in values:
            if isinstance(row, dict):
                out.append(row)
    return out


def case_constant_origin(row: dict) -> str:
    values = _flatten_constant_values(row.get("initial_values"))
    origins = [str(v.get("origin", "")) for v in values]
    if any(o.startswith("captured") for o in origins):
        return "captured"
    if any(o.startswith("descriptor_derived") or o.startswith("inferred")
           for o in origins):
        return "inferred"
    return "synthetic"


def case_descriptor_origin(job: dict | None) -> str:
    if not job:
        return "not_applicable"
    pairing = str(job.get("pairing_evidence", ""))
    evidence = str(job.get("descriptor_evidence", "")).lower()
    # Do not use a bare "captured" substring: synthetic descriptor text contains
    # the phrase "not captured runtime state".  Only explicit pairing/capture
    # evidence counts.
    if pairing in ("captured_donor_recipe", "captured_fixed_ui") or \
            job.get("attachments") or job.get("serial"):
        return "captured"
    if pairing == "abi_candidate" or "inferred from declared air" in evidence:
        return "inferred"
    return "synthetic"


def joint_origin_rank(constant_origin: str, descriptor_origin: str) -> str:
    if constant_origin == "captured":
        return "captured"
    if constant_origin == "inferred" or descriptor_origin in ("captured", "inferred"):
        return "inferred"
    return "synthetic"


def _priority_score(source: str | None, joint_origin: str) -> tuple[int, int]:
    tier = priority_tier(source)
    tier_base = {0: 100000, 1: 50000, 2: 20000, 3: 10000}[tier]
    cls = source_class(source)
    score = tier_base + _ORIGIN_RANK[joint_origin] * 10000 + _SOURCE_BONUS.get(cls, 0)
    return tier, score


def _observed_entries(corpus: Path, critical_path: Path | None = None) -> tuple[dict, str]:
    """Observed SpringBoard/backboardd/QuartzCore evidence, keyed by AIR hash.

    Preferred input is metal-shader-corpus's machine-readable
    ledger-claude-20261025/springboard_critical.json.  Until it exists, derive
    the provisional observed set from the exactly stated corpus evidence:
    SiriUICore all entries, QuartzCore all entries (backboardd renderer role),
    and MPSImage D*F*H/V kernels.  This is an observation join, not a library
    name-only priority rule.
    """
    if critical_path is None:
        candidate = corpus / "ledger-claude-20261025" / "springboard_critical.json"
        if candidate.exists():
            critical_path = candidate
    if critical_path and critical_path.exists():
        data = read_json(critical_path)
        if isinstance(data, dict):
            for key in ("items", "entries", "cases", "critical",
                        "springboard_critical"):
                if key in data:
                    data = data[key]
                    break
        entries = data if isinstance(data, list) else []
        out = {}
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            airs = set()
            for key in ("air_sha256", "air", "module_sha256", "subject",
                        "attempt", "vertex_air_sha256", "fragment_air_sha256"):
                value = entry.get(key)
                if not value:
                    continue
                value = str(value)
                if ":" in value:
                    value = value.rsplit(":", 1)[-1]
                airs.add(value)
            for air in airs:
                out[air] = entry
        return out, "explicit:" + str(critical_path)
    functions = read_json(corpus / "functions.json")["entries"]
    out = {}
    for entry in functions:
        source = entry.get("library_source", "")
        name = entry.get("name", "")
        observed = False
        tier = None
        if "SiriUICore" in source:
            observed = True
            tier = "observed_siriui"
        elif "QuartzCore" in source:
            observed = True
            tier = "observed_quartzcore_backboardd"
        elif "MPSImage" in source and re.fullmatch(
                r"D[A-Za-z0-9]*F[A-Za-z0-9]*[HV]", name):
            observed = True
            tier = "observed_mpsimage_springboard"
        if observed:
            out[entry["air_sha256"]] = {
                "air_sha256": entry["air_sha256"],
                "name": name,
                "library_source": source,
                "evidence_tier": tier,
                "evidence": "metal-shader-corpus board 179295? / provisional from functions.json",
            }
    return out, "provisional:corpus-message+functions.json"


def _observed_rank(entry: dict) -> int:
    name = entry.get("name") or ""
    tier = str(entry.get("evidence_tier") or entry.get("tier") or "")
    library = str(entry.get("library") or entry.get("library_source") or "")
    if name == "D8F18H":
        return 5000
    if tier.startswith("S0"):
        return 4500
    if "captured" in tier:
        return 4000
    if "quartzcore" in library.lower():
        return 3500
    if tier.startswith("S1"):
        return 3000
    if tier.startswith("S2"):
        return 2000
    return 1000


def _priority_row(row: dict, job: dict | None, family: str,
                  compiler_sha: str, bridge_sha: str,
                  observed: dict | None = None) -> dict:
    constant_origin = case_constant_origin(row)
    descriptor_origin = case_descriptor_origin(job)
    joint = joint_origin_rank(constant_origin, descriptor_origin)
    source = row.get("library_source")
    tier, score = _priority_score(source, joint)
    observed_hit = None
    if observed:
        keys = [row.get("air_sha256"), row.get("vertex_air_sha256"),
                row.get("fragment_air_sha256"), row.get("case")]
        for key in keys:
            if key and key in observed:
                observed_hit = observed[key]
                break
    observed_p0 = False
    if observed_hit:
        observed_rank = _observed_rank(observed_hit)
        observed_p0 = observed_rank >= 3500
        if observed_p0:
            tier = 0
            score = 200000 + observed_rank + _ORIGIN_RANK[joint] * 10
        else:
            tier = 1
            score = 50000 + observed_rank + _ORIGIN_RANK[joint] * 10
    cls = source_class(source)
    declared = row.get("declared_constants")
    shape = []
    if isinstance(declared, dict):
        for stage, rows in declared.items():
            for constant in rows:
                shape.append({
                    "stage": stage, "name": constant.get("name"),
                    "type": constant.get("type"), "index": constant.get("index"),
                    "required": bool(constant.get("required")),
                })
    else:
        for constant in declared or []:
            shape.append({
                "stage": "kernel", "name": constant.get("name"),
                "type": constant.get("type"), "index": constant.get("index"),
                "required": bool(constant.get("required")),
            })
    reasons = [f"tier{tier}", f"source_class_{cls}",
               f"constant_origin_{constant_origin}",
               f"descriptor_origin_{descriptor_origin}",
               f"joint_origin_{joint}"]
    if observed_hit:
        reasons += ["observed_springboard_critical",
                    "observed_" + str(observed_hit.get("tier") or
                                      observed_hit.get("evidence_tier"))]
        if observed_p0:
            reasons.append("observed_p0")
    elif cls in ("quartzcore", "renderbox", "corere"):
        reasons.append("role_inferred_ui_source")
    if cls in ("springboard", "backboard", "quartzcore"):
        reasons.append("ui_crash_priority_source")
    return {
        "schema": PRIORITY_SCHEMA,
        "family": family,
        "case": row.get("case"),
        "name": row.get("name"),
        "library_source": source,
        "library_sha256": row.get("library_sha256"),
        "vertex_air_sha256": row.get("vertex_air_sha256"),
        "fragment_air_sha256": row.get("fragment_air_sha256"),
        "air_sha256": row.get("air_sha256"),
        "source_class": cls,
        "priority_tier": tier,
        "priority_score": score,
        "observed_priority": bool(observed_p0),
        "observed_evidence": observed_hit,
        "observed_name": (observed_hit or {}).get("name"),
        "observed_tier": (observed_hit or {}).get("tier"),
        "constant_origin": constant_origin,
        "descriptor_origin": descriptor_origin,
        "joint_origin_rank": joint,
        "origin_rank": _ORIGIN_RANK[joint],
        "app_coverage": constant_origin == "captured",
        "synthetic_constant_values": constant_origin == "synthetic",
        "declared_constant_count": row.get("required_constant_count", 0)
        + row.get("optional_constant_count", 0),
        "required_constant_count": row.get("required_constant_count"),
        "optional_constant_count": row.get("optional_constant_count"),
        "constant_types": row.get("constant_types"),
        "expected_input_shape": shape,
        "pairing_evidence": (job or {}).get("pairing_evidence"),
        "descriptor_evidence": (job or {}).get("descriptor_evidence"),
        "priority_reasons": reasons,
        "compiler_sha256": compiler_sha,
        "bridge_sha256": bridge_sha,
        "remaining": True,
        "previously_executed": False,
    }


def _executed_cases(evidence_dir: Path, dirs: list[str]) -> set[str]:
    executed = set()
    for name in dirs:
        path = evidence_dir / name / "results.jsonl"
        if not path.exists():
            continue
        for row in read_jsonl(path):
            case = row.get("case")
            if case:
                executed.add(case)
    return executed


def build_priority_manifest(corpus: Path, evidence_dir: Path, out_dir: Path,
                            compiler_path: Path, bridge_path: Path,
                            critical_path: Path | None = None) -> dict:
    graphics_rows = list(read_jsonl(
        evidence_dir / "inputs_graphics_fc" / "inputs-ledger.jsonl"))
    compute_rows = list(read_jsonl(
        evidence_dir / "inputs_compute_fc" / "inputs-ledger.jsonl"))
    executed_graphics = _executed_cases(
        evidence_dir, ["graphics_fc", "graphics_fc_optional_only"])
    executed_compute = _executed_cases(
        evidence_dir, ["compute_fc", "compute_fc_optional_only"])
    remaining_graphics = [r for r in graphics_rows if r["case"] not in executed_graphics]
    remaining_compute = [r for r in compute_rows if r["case"] not in executed_compute]
    if len(remaining_graphics) != 273:
        raise SystemExit(f"priority: expected 273 remaining graphics FC, got "
                         f"{len(remaining_graphics)}")
    if len(remaining_compute) != 2875:
        raise SystemExit(f"priority: expected 2875 remaining compute FC, got "
                         f"{len(remaining_compute)}")
    compiler_sha = sha256_file(compiler_path)
    bridge_sha = sha256_file(bridge_path)
    observed, observed_source = _observed_entries(corpus, critical_path)
    jobs = load_jobs(corpus)
    rows = []
    for row in remaining_graphics:
        job = jobs.get(row["case"])
        rows.append(_priority_row(row, job, "graphics_fc", compiler_sha, bridge_sha,
                                  observed))
    for row in remaining_compute:
        rows.append(_priority_row(row, None, "compute_fc", compiler_sha, bridge_sha,
                                  observed))
    rows.sort(key=lambda r: (-r["priority_score"], r["family"],
                             r.get("library_source") or "", r.get("name") or "",
                             r.get("case") or ""))
    for index, row in enumerate(rows, 1):
        row["selection_rank"] = index
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "priority-manifest.jsonl"
    tmp = path.with_name(path.name + f".tmp.{os.getpid()}")
    with tmp.open("w", encoding="utf-8") as fh:
        for row in rows:
            fh.write(json.dumps(row, separators=(",", ":")) + "\n")
    tmp.replace(path)
    top_path = out_dir / "priority-top100.jsonl"
    with top_path.open("w", encoding="utf-8") as fh:
        for row in rows[:100]:
            fh.write(json.dumps(row, separators=(",", ":")) + "\n")

    def count(field):
        counts = {}
        for row in rows:
            key = row.get(field)
            counts[str(key)] = counts.get(str(key), 0) + 1
        return dict(sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])))

    source_counts = {}
    for row in rows:
        source = row.get("library_source") or "unknown"
        source_counts[source] = source_counts.get(source, 0) + 1
    top_sources = sorted(source_counts.items(), key=lambda kv: (-kv[1], kv[0]))[:25]
    springboard = [r for r in rows if r["source_class"] in ("springboard", "backboard")]
    quartzcore = [r for r in rows if r["source_class"] == "quartzcore"]
    observed_rows = [r for r in rows if r["observed_priority"]]
    matched_entries = {id(r.get("observed_evidence")) for r in observed_rows
                       if r.get("observed_evidence")}
    outside_ids = {id(entry) for entry in observed.values()} - matched_entries
    observed_outside = sorted(air for air, entry in observed.items()
                              if id(entry) in outside_ids)
    observed_total = len({id(entry) for entry in observed.values()})
    summary = {
        "schema": PRIORITY_SCHEMA,
        "tool_sha256": tool_sha256(),
        "compiler_sha256": compiler_sha,
        "compiler_path": str(compiler_path),
        "bridge_sha256": bridge_sha,
        "bridge_path": str(bridge_path),
        "observed_source": observed_source,
        "observed_entries_total": observed_total,
        "observed_p0_rows_remaining": len(observed_rows),
        "observed_outside_remaining": len(outside_ids),
        "observed_outside_air_sample": observed_outside[:50],
        "remaining_graphics_fc": len(remaining_graphics),
        "remaining_compute_fc": len(remaining_compute),
        "remaining_total": len(rows),
        "previously_executed_graphics_fc": len(graphics_rows) - len(remaining_graphics),
        "previously_executed_compute_fc": len(compute_rows) - len(remaining_compute),
        "priority_tiers": count("priority_tier"),
        "origin_ranks": count("joint_origin_rank"),
        "constant_origins": count("constant_origin"),
        "descriptor_origins": count("descriptor_origin"),
        "source_classes": count("source_class"),
        "top_libraries": [{"source": source, "cases": count}
                          for source, count in top_sources],
        "springboard_or_backboard_cases": len(springboard),
        "quartzcore_cases": len(quartzcore),
        "no_springboard_backboard_note": (
            "No SpringBoard or backboardd metallib path appears in the remaining "
            "FC sets. P0 is the OBSERVED intersection: 20 MPSImage D*F*H/V "
            "kernels (D8F18H first), 4 QuartzCore cases and 1 SiriUICore case "
            "present in the remaining FC ledger; the other 153 observed entries "
            "are outside these remaining FC batches. RenderBox/CoreRE/VectorKit "
            "are role-inferred P1, not observed P0."),
        "manifest": str(path),
        "manifest_sha256": sha256_file(path),
        "top100": str(top_path),
        "top100_sha256": sha256_file(top_path),
        "scope": (
            "Static priority plan only; no compiler invocation. Joint origin uses "
            "captured constants first, then captured/inferred descriptor or AIR "
            "pairing context, else synthetic. synthetic_constant_values=true means "
            "the exact application value is unknown; app_coverage=false."),
    }
    write_json(out_dir / "priority-summary.json", summary)
    return summary


# ---------------------------------------------------------------------------
# Runner.
# ---------------------------------------------------------------------------


def _load_done(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        return read_json(path)
    except Exception:
        return {}


def _is_done(done: dict, spec: dict, compiler_sha: str, bridge_sha: str) -> bool:
    row = done.get(spec["id"])
    if not row:
        return False
    return (row.get("variant_sha256") == spec.get("variant_sha256")
            and row.get("compiler_sha256") == compiler_sha
            and row.get("bridge_sha256") == bridge_sha)


def _compose_run_result(run_dir: Path, spec: dict, returncode: int | None,
                        timed_out: bool, wall: float) -> dict:
    result_path = run_dir / "result.json"
    translation_path = run_dir / "translation.json"
    progress_path = run_dir / "progress.json"
    worker_progress = None
    if progress_path.exists():
        try:
            worker_progress = read_json(progress_path)
        except Exception:
            worker_progress = None
    if result_path.exists():
        try:
            result = read_json(result_path)
        except Exception:
            result = {"id": spec["id"], "translation_status": "worker_error",
                      "worker_error": "unreadable result.json"}
    else:
        result = {"id": spec["id"], "schema": SCHEMA, "kind": spec["kind"],
                  "case": spec["case"], "family": spec.get("family"),
                  "baseline_error": spec.get("baseline_error"),
                  "translation_status": "worker_error",
                  "compile_status": "not_attempted",
                  "constants": spec.get("initial_constants", {}),
                  "attempts": []}
        if translation_path.exists():
            try:
                translation = read_json(translation_path)
                result.update({
                    "translation_status": translation.get("status", "translated"),
                    "translation_artifacts": translation,
                    "specialization_status": translation.get("specialization_status"),
                    "constants": translation.get("constants", result.get("constants")),
                })
                if translation.get("backend"):
                    result["translator_backend"] = translation["backend"]
                if translation.get("descriptor") and not result.get("descriptor"):
                    result["descriptor"] = translation["descriptor"]
            except Exception:
                pass
    if worker_progress:
        phase = worker_progress.get("phase")
        result["worker_progress"] = {
            "phase": phase,
            "values": worker_progress.get("values"),
            "attempts": worker_progress.get("attempts"),
            "descriptor": worker_progress.get("descriptor"),
            "attachments": worker_progress.get("attachments"),
            "attributes": worker_progress.get("attributes"),
            "layouts": worker_progress.get("layouts"),
        }
        if not result.get("descriptor") and worker_progress.get("descriptor"):
            result["descriptor"] = {
                "label": worker_progress.get("descriptor"),
                "attachments": worker_progress.get("attachments"),
                "attributes": worker_progress.get("attributes"),
                "layouts": worker_progress.get("layouts"),
                "blocked": None,
                "provenance": "last_worker_attempt",
            }
        if phase and str(phase).startswith("specialize_"):
            result["specialization_status"] = "crashed"
            result["specialization_phase"] = phase
        elif phase in ("translate_compute", "translate_graphics", "mali", "translated"):
            result["specialization_status"] = "ok"
        if not any(result.get("constants", {}).get(stage) for stage in
                   ("vertex", "fragment", "kernel")):
            result["constants"] = worker_progress.get("values") or {}
        if worker_progress.get("attempts") and not result.get("attempts"):
            result["attempts"] = worker_progress["attempts"]
    result["worker_exit"] = returncode
    result["timed_out"] = timed_out
    result["wall_seconds"] = round(wall, 3)
    result["compiler_sha256"] = (spec.get("compiler") or {}).get("compiler_sha256")
    result["bridge_sha256"] = (spec.get("compiler") or {}).get("bridge_sha256")
    if result.get("descriptor") is None:
        variants = spec.get("descriptor_variants") or []
        if variants:
            result["descriptor"] = {
                "label": variants[0].get("label"),
                "attachments": variants[0].get("attachments"),
                "attributes": variants[0].get("attributes"),
                "layouts": variants[0].get("layouts"),
                "provenance": variants[0].get("provenance"),
                "blocked": variants[0].get("blocked"),
            }
    if timed_out:
        result["worker_status"] = "timeout"
    elif returncode == 0:
        result["worker_status"] = "exited"
    elif returncode is not None and returncode < 0:
        result["worker_status"] = f"signal_{abs(returncode)}"
    else:
        result["worker_status"] = f"exit_{returncode}"

    # Separate translator and backend verdicts.  A Mali abort must not erase the
    # already-saved translation.
    translation = result.get("translation_status")
    compile_status = result.get("compile_status", "not_attempted")
    if translation == "translated" and result["worker_status"].startswith("signal_"):
        result["compile_status"] = "crashed"
        result["compile_signal"] = result["worker_status"]
        compile_status = "crashed"
    elif translation == "translated" and result["worker_status"] == "timeout":
        result["compile_status"] = "timeout"
        compile_status = "timeout"
    if result["worker_status"] == "timeout":
        outcome = "worker_timeout"
    elif translation == "translated" and compile_status == "compiled":
        outcome = "translated_native_compiled"
    elif translation == "translated" and compile_status == "refused":
        outcome = "translated_native_refused"
    elif translation == "translated" and result["worker_status"].startswith("signal_"):
        outcome = "translated_backend_abort"
    elif translation == "translated" and result["worker_status"].startswith("exit_"):
        outcome = "translated_worker_exit"
    elif translation == "refused":
        cls = result.get("translation_classification", {}).get("class", "unknown")
        if cls == "bridge_stage_topology_refused":
            outcome = "bridge_stage_topology_refused"
        elif cls.startswith("missing_"):
            outcome = "translation_missing_input"
        elif cls == "bridge_attachment_format_surface":
            outcome = "bridge_attachment_format_surface"
        elif cls.startswith("translator_"):
            outcome = "translation_unsupported"
        else:
            outcome = "translation_refused"
    elif result.get("descriptor") and result["descriptor"].get("blocked"):
        outcome = "descriptor_blocked"
    elif translation == "worker_error":
        if result["worker_status"].startswith("signal_") or result["worker_status"] == "timeout":
            outcome = "worker_abort_before_translation"
        else:
            outcome = "worker_error"
    else:
        outcome = "worker_no_translation"
    result["outcome_class"] = outcome
    result["baseline_classification"] = (spec.get("baseline", {}) or {}).get("classification")
    descriptor_class = (result.get("translation_classification") or {}).get("class")
    result["descriptor_materialised"] = bool(
        spec.get("family") in ("color_attachment_format_or_MRT",
                               "tessellation_patch_control_point_descriptor")
        and translation in ("translated", "refused")
        and (result.get("descriptor") or {}).get("blocked") is None
        and descriptor_class not in (
            "colour_attachment_descriptor", "vertex_attribute_descriptor",
            "vertex_layout_descriptor", "bridge_attachment_format_surface"))
    result["constant_materialised"] = bool(
        spec.get("family") == "FunctionConstant"
        and result.get("specialization_status") in ("ok", None)
        and any(result.get("constants", {}).get(stage) for stage in
                ("vertex", "fragment", "kernel")))
    result["hardware_executed"] = False
    return result


def run_variants(corpus: Path, out_dir: Path, limit: int, workers: int,
                 resume: bool, allow_compiler_change: bool,
                 cpu_limit: int = 12) -> dict:
    plan_path = out_dir / "variants.jsonl"
    if not plan_path.exists():
        raise SystemExit("run: variants.jsonl missing; run plan first")
    variants = list(read_jsonl(plan_path))
    if variants:
        first = variants[0]
        compiler_sha = (first.get("compiler") or {}).get("compiler_sha256")
        bridge_sha = (first.get("compiler") or {}).get("bridge_sha256")
        sources = first.get("sources", {})
        compiler = resolve_source(sources.get("compiler"), corpus,
                                  "baseline/libnord2_mali_runtime.so")
        bridge = resolve_source(sources.get("bridge"), corpus,
                                "baseline/libtegu_air_bridge.so")
        for spec in variants:
            if (spec.get("compiler") or {}).get("compiler_sha256") != compiler_sha or \
                    (spec.get("compiler") or {}).get("bridge_sha256") != bridge_sha:
                raise SystemExit("run: variants use different compiler/bridge hashes")
        if sha256_file(compiler) != compiler_sha:
            raise SystemExit("run: compiler file does not match plan hash")
        if sha256_file(bridge) != bridge_sha:
            raise SystemExit("run: bridge file does not match plan hash")
    else:
        compiler_sha = FROZEN_COMPILER
        bridge_sha = FROZEN_BRIDGE
    runnable = [v for v in variants if not v.get("descriptor_blocked")]
    if limit and limit > 0:
        runnable = runnable[:limit]
    blocked_variants = [v for v in variants if v.get("descriptor_blocked")]
    done_path = out_dir / "done.json"
    done = _load_done(done_path)
    runs = out_dir / "runs"
    runs.mkdir(parents=True, exist_ok=True)
    executed = 0
    skipped = 0
    blocked_recorded = 0

    def record_blocked(spec: dict) -> dict:
        run_dir = runs / spec["id"]
        run_dir.mkdir(parents=True, exist_ok=True)
        write_json(run_dir / "spec.json", spec)
        for stale in ("result.json", "translation.json"):
            try:
                (run_dir / stale).unlink()
            except FileNotFoundError:
                pass
        descriptor = (spec.get("descriptor_variants") or [{}])[0]
        result = {
            "id": spec["id"], "schema": SCHEMA, "kind": spec["kind"],
            "case": spec["case"], "family": spec.get("family"),
            "baseline_error": spec.get("baseline_error"),
            "baseline_classification": (spec.get("baseline") or {}).get("classification"),
            "translation_status": "not_attempted",
            "compile_status": "not_attempted",
            "outcome_class": "descriptor_blocked",
            "descriptor_blocked": spec.get("descriptor_blocked"),
            "descriptor": {
                "label": descriptor.get("label"),
                "blocked": descriptor.get("blocked"),
                "blocked_slots": descriptor.get("blocked_slots", []),
                "reasons": descriptor.get("reasons", []),
                "attachments": descriptor.get("attachments", []),
                "attributes": descriptor.get("attributes", []),
                "layouts": descriptor.get("layouts", []),
                "provenance": descriptor.get("provenance"),
            },
            "worker_status": "not_run",
            "hardware_executed": False,
            "descriptor_materialised": False,
            "constant_materialised": False,
        }
        write_json(run_dir / "result.json", result)
        return result

    for spec in blocked_variants:
        if resume and _is_done(done, spec, compiler_sha, bridge_sha):
            skipped += 1
            continue
        result = record_blocked(spec)
        done[spec["id"]] = {
            "variant_sha256": spec.get("variant_sha256"),
            "compiler_sha256": compiler_sha, "bridge_sha256": bridge_sha,
            "outcome_class": result["outcome_class"], "finished_unix": time.time(),
        }
        write_json(done_path, done)
        blocked_recorded += 1

    def execute(spec: dict) -> tuple[str, dict | None]:
        run_dir = runs / spec["id"]
        run_dir.mkdir(parents=True, exist_ok=True)
        write_json(run_dir / "spec.json", spec)
        # A stale result from a previous attempt must never masquerade as this run's
        # verdict (the child may abort in the native compiler after translation).
        for stale in ("result.json", "translation.json"):
            try:
                (run_dir / stale).unlink()
            except FileNotFoundError:
                pass
        start = time.time()
        timed_out = False
        log_path = run_dir / "worker.log"
        try:
            with log_path.open("wb") as log:
                proc = subprocess.run(
                    [sys.executable, str(Path(__file__).resolve()),
                     "--corpus", str(corpus), "--output", str(run_dir),
                     "worker", str(run_dir / "spec.json")],
                    stdout=log, stderr=subprocess.STDOUT,
                    timeout=max(30, cpu_limit * 2),
                    env={**os.environ, "PYTHONHASHSEED": "0",
                         "N2_AUDIT_CPU_SECONDS": str(cpu_limit)})
                returncode = proc.returncode
        except subprocess.TimeoutExpired:
            timed_out = True
            returncode = None
        wall = time.time() - start
        result = _compose_run_result(run_dir, spec, returncode, timed_out, wall)
        write_json(run_dir / "result.json", result)
        return spec["id"], result

    if workers <= 1:
        for spec in runnable:
            if resume and _is_done(done, spec, compiler_sha, bridge_sha):
                skipped += 1
                continue
            cid, result = execute(spec)
            done[cid] = {
                "variant_sha256": spec.get("variant_sha256"),
                "compiler_sha256": compiler_sha, "bridge_sha256": bridge_sha,
                "outcome_class": (result or {}).get("outcome_class"),
                "finished_unix": time.time(),
            }
            write_json(done_path, done)
            executed += 1
            print(f"{cid} {result['outcome_class'] if result else 'no-result'} "
                  f"{spec['kind']} {spec['case']}", flush=True)
    else:
        def task(spec):
            if resume and _is_done(done, spec, compiler_sha, bridge_sha):
                return spec["id"], "skipped", None
            cid, result = execute(spec)
            return cid, "executed", result

        with ThreadPoolExecutor(max_workers=workers) as pool:
            for cid, state, result in pool.map(task, runnable):
                if state == "skipped":
                    skipped += 1
                    continue
                done[cid] = {
                    "variant_sha256": next(s["variant_sha256"] for s in runnable if s["id"] == cid),
                    "compiler_sha256": compiler_sha, "bridge_sha256": bridge_sha,
                    "outcome_class": (result or {}).get("outcome_class"),
                    "finished_unix": time.time(),
                }
                executed += 1
                print(f"{cid} {result['outcome_class'] if result else 'no-result'}", flush=True)
        write_json(done_path, done)

    summary = {
        "schema": SCHEMA, "tool_sha256": tool_sha256(),
        "planned": len(variants), "runnable": len(runnable),
        "executed": executed, "blocked_recorded": blocked_recorded,
        "skipped_done": skipped,
        "workers": workers, "compiler_sha256": compiler_sha,
        "bridge_sha256": bridge_sha, "cpu_limit": cpu_limit,
    }
    write_json(out_dir / "run-summary.json", summary)
    return summary


# ---------------------------------------------------------------------------
# Summary.
# ---------------------------------------------------------------------------


def summarize(corpus: Path, out_dir: Path) -> dict:
    runs = out_dir / "runs"
    results = []
    if runs.exists():
        for run_dir in sorted(runs.iterdir()):
            if not run_dir.is_dir():
                continue
            result_path = run_dir / "result.json"
            spec_path = run_dir / "spec.json"
            if not result_path.exists() or not spec_path.exists():
                continue
            try:
                result = read_json(result_path)
                spec = read_json(spec_path)
            except Exception:
                continue
            result.setdefault("family", spec.get("family"))
            result.setdefault("baseline_classification", (spec.get("baseline") or {}).get("classification"))
            result.setdefault("descriptor", None)
            if result.get("compute_entry") is None and spec.get("compute_entry"):
                result["compute_entry"] = spec["compute_entry"]
            if result.get("worker_status") == "signal_9" and \
                    result.get("translation_status") == "translated":
                # SIGKILL after translation is the hard CPU/AS limit, not a
                # backend refusal. Keep it in the explicit unknown class.
                result["compile_status"] = "killed"
                result["outcome_class"] = "cpu_limit_unknown"
                result["limit_note"] = "signal_9 after translation (RLIMIT_CPU/AS)"
            result["spec"] = {
                "kind": spec.get("kind"), "policy": spec.get("policy"),
                "descriptor_variants": spec.get("descriptor_variants", []),
                "initial_constants": spec.get("initial_constants", {}),
                "declared_constants": spec.get("declared_constants", {}),
            }
            results.append(result)
    results.sort(key=lambda r: r.get("id", ""))
    result_path = out_dir / "results.jsonl"
    tmp = result_path.with_name(result_path.name + f".tmp.{os.getpid()}")
    with tmp.open("w", encoding="utf-8") as fh:
        for row in results:
            fh.write(json.dumps(row, separators=(",", ":")) + "\n")
    tmp.replace(result_path)

    counts: dict[str, dict] = {}
    best_rank = {
        "translated_native_compiled": 100,
        "translated_native_refused": 90,
        "translated_backend_abort": 80,
        "bridge_stage_topology_refused": 70,
        "translated_worker_exit": 65,
        "translation_refused": 60,
        "translation_unsupported": 55,
        "translation_missing_input": 50,
        "bridge_attachment_format_surface": 40,
        "descriptor_blocked": 30,
        "worker_abort_before_translation": 20,
        "worker_timeout": 15,
        "worker_error": 10,
    }
    unique: dict[str, tuple[int, str, str]] = {}
    for row in results:
        fam = row.get("family") or "unknown"
        counts.setdefault(fam, {})
        key = row.get("outcome_class", "unknown")
        counts[fam][key] = counts[fam].get(key, 0) + 1
        case = row.get("case")
        rank = best_rank.get(key, 0)
        if case and (case not in unique or rank > unique[case][0]):
            unique[case] = (rank, key, fam)
    unique_outcomes: dict[str, int] = {}
    unique_by_family: dict[str, dict] = {}
    backend_counts: dict[str, int] = {}
    for _case, (_rank, key, fam) in unique.items():
        unique_outcomes[key] = unique_outcomes.get(key, 0) + 1
        unique_by_family.setdefault(fam, {})
        unique_by_family[fam][key] = unique_by_family[fam].get(key, 0) + 1
    for row in results:
        backend = row.get("translator_backend") or "bridge"
        backend_counts[backend] = backend_counts.get(backend, 0) + 1
    compute_entry_counts: dict[str, int] = {}
    for row in results:
        entry = row.get("compute_entry")
        if entry:
            compute_entry_counts[entry] = compute_entry_counts.get(entry, 0) + 1
    baseline = {}
    superseded = []
    for row in results:
        key = (row.get("baseline_classification"), row.get("outcome_class"))
        baseline[f"{key[0]} -> {key[1]}"] = baseline.get(f"{key[0]} -> {key[1]}", 0) + 1
        if row.get("descriptor_materialised"):
            superseded.append({
                "id": row.get("id"), "case": row.get("case"),
                "baseline_error": row.get("baseline_error"),
                "outcome_class": row.get("outcome_class"),
                "translation_status": row.get("translation_status"),
                "descriptor_label": (row.get("descriptor") or {}).get("label"),
            })
    summary = {
        "schema": SCHEMA,
        "tool_sha256": tool_sha256(),
        "results": len(results),
        "unique_cases": len(unique),
        "counts_by_family_and_outcome": counts,
        "unique_case_outcomes": unique_outcomes,
        "unique_case_outcomes_by_family": unique_by_family,
        "translator_backend_counts": backend_counts,
        "compute_entry_counts": compute_entry_counts,
        "baseline_to_outcome": baseline,
        "descriptor_materialised_cases": len(superseded),
        "descriptor_materialised": superseded,
        "hardware_executed": False,
        "limitations": [
            "Synthetic function-constant values and synthetic descriptors are audit branch "
            "choices, not captured application state.",
            "Compiled code is not GPU execution and no pixel correctness is claimed.",
            "The frozen render bridge exposes only R8/RGBA8/BGRA8/RGBA16F targets; integer "
            "or other-format declarations remain an explicit surface limitation.",
            "Rows with translator_backend=metal2vulkan_cli use the existing standalone "
            "translator CLI to explore exact Metal target formats the production bridge "
            "does not expose. That is an audit route, not production bridge support.",
            "The finite explored value set is not the unbounded specialization space.",
            "The frozen bridge refuses expanded (tessellation) stage topology after a "
            "successful internal translation; that is a bridge/backend gap, not an AIR "
            "decode failure.",
        ],
    }
    write_json(out_dir / "summary.json", summary)
    lines = ["# corpus_inputs audit summary", "",
             f"variant results: {len(results)}",
             f"unique cases: {len(unique)}", "", "## unique case outcomes", ""]
    for key, value in sorted(unique_outcomes.items()):
        lines.append(f"- {key}: {value}")
    lines += ["", "## variant outcomes", ""]
    for fam, rows in sorted(counts.items()):
        lines.append(f"### {fam}")
        for key, value in sorted(rows.items()):
            lines.append(f"- {key}: {value}")
        lines.append("")
    lines += ["## baseline -> outcome", ""]
    for key, value in sorted(baseline.items()):
        lines.append(f"- {key}: {value}")
    lines += ["", "## descriptor-materialised cases", ""]
    for row in superseded[:50]:
        lines.append(f"- {row['case']}: {row['baseline_error']} -> {row['outcome_class']} "
                     f"({row['descriptor_label']})")
    if len(superseded) > 50:
        lines.append(f"- ... {len(superseded) - 50} more")
    lines += ["", "## limitations", ""] + [f"- {item}" for item in summary["limitations"]]
    atomic_write(out_dir / "summary.md", "\n".join(lines) + "\n")
    return summary


# ---------------------------------------------------------------------------
# Selftest.
# ---------------------------------------------------------------------------


def _check(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def selftest(host: bool, corpus: Path, out_dir: Path, current: bool = False) -> int:
    failures: list[str] = []

    # Published Metal enum values and byte widths.
    _check(mtl_type("ushort").numeric == 41 and mtl_type("ushort").size == 2,
           "ushort type mapping", failures)
    _check(mtl_type("bool").numeric == 53 and mtl_type("bool").size == 1,
           "bool type mapping", failures)
    _check(mtl_type("half4").numeric == 19 and mtl_type("half4").size == 8,
           "half4 type mapping", failures)
    _check(mtl_type("uchar3").numeric == 51 and mtl_type("uchar3").size == 3,
           "uchar3 type mapping", failures)
    _check(mtl_type("uint2").numeric == 34 and mtl_type("uint2").size == 8,
           "uint2 type mapping", failures)
    _check(encode_scalar("bool", True) == b"\x01", "bool encoding", failures)
    _check(encode_scalar("bool", False) == b"\x00", "bool false encoding", failures)
    _check(encode_scalar("ushort", 32) == b"\x20\x00", "ushort encoding", failures)
    _check(encode_scalar("uint", 0x12345678) == bytes.fromhex("78563412"),
           "uint encoding", failures)
    _check(encode_scalar("int", -1) == b"\xff\xff\xff\xff", "int encoding", failures)
    _check(encode_scalar("half", 0.5) == b"\x00\x38", "half encoding", failures)
    _check(encode_scalar("uchar3", [1, 2, 3]) == b"\x01\x02\x03",
           "uchar3 encoding", failures)

    sample_ll = r'''
!air.function_constants = !{!26, !27}
!26 = !{ptr addrspace(2) @_ZN5V3HLR14kXhlrbTileSizeE.MTL_FC_INIT_0_t, !"ushort", !"V3HLR::kXhlrbTileSize", i32 0, i1 true}
!27 = !{ptr addrspace(2) @_Z16fc_vertex_layout.MTL_FC_INIT_3_h, !"uchar", !"fc_vertex_layout", i32 3, i1 false}
!16 = !{i32 0, !"air.vertex_input", !"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"float4", !"air.arg_name", !"posBuf"}
!17 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"c0"}
!18 = !{i32 0, !"air.render_target", i32 2, !"air.arg_type_name", !"float", !"air.arg_name", !"d0"}
!19 = !{i32 0, !"air.patch_control_point_input", !20, !21}
!20 = !{!"air.patch_control_point_function", ptr @_Z12ControlPoint.MTL_CONTROL_POINT_FN}
!21 = !{!"air.location_index", i32 1, i32 1, !"air.arg_type_name", !"float", !"air.arg_name", !"lathe"}
'''
    fcs = parse_function_constants_ll(sample_ll)
    _check(len(fcs) == 2 and fcs[0]["index"] == 0 and fcs[0]["required"]
           and fcs[1]["type"] == "uchar" and not fcs[1]["required"],
           "function-constant metadata parsing", failures)
    vin = parse_vertex_inputs_ll(sample_ll)
    _check(vin == [{"index": 0, "count": 1, "type": "float4"}],
           "ordinary vertex-input parsing", failures)
    targets = parse_render_targets_ll(sample_ll)
    _check({(t["slot"], t["form"], t["type"]) for t in targets}
           == {(0, "output", "float4"), (2, "input", "float")},
           "render-target input/output parsing", failures)
    pin = parse_patch_inputs_ll(sample_ll)
    _check(pin == [{"index": 1, "type": "float"}], "patch input parsing", failures)

    att = plan_attachments([
        {"slot": 0, "form": "output", "type": "float4"},
        {"slot": 1, "form": "output", "type": "float4"},
        {"slot": 2, "form": "input", "type": "float"},
    ])
    _check(att["blocked"] is None, "float MRT plan not blocked", failures)
    _check([(a["format"], a["write_mask"]) for a in att["attachments"]]
           == [(80, 15), (80, 15), (10, 0)], "float MRT format selection", failures)
    half2 = plan_attachments([{"slot": 0, "form": "output", "type": "half2"}])
    _check([(a["format"], a["write_mask"]) for a in half2["attachments"]] == [(10, 15)],
           "two-component float uses R8", failures)
    integer = plan_attachments([{"slot": 0, "form": "output", "type": "int4"},
                                {"slot": 1, "form": "output", "type": "uint4"}])
    _check(integer["blocked"] == "bridge_attachment_format_surface"
           and integer["blocked_slots"] == [0, 1],
           "integer target refusal is explicit", failures)
    gap = plan_attachments([
        {"slot": 0, "form": "output", "type": "float4"},
        {"slot": 1, "form": "output", "type": "float4"},
        {"slot": 3, "form": "output", "type": "float4"},
    ])
    _check([(a["format"], a["write_mask"]) for a in gap["attachments"]]
           == [(80, 15), (80, 15), (10, 0), (80, 15)],
           "inactive contiguous slot is mask zero", failures)
    over = plan_attachments([{"slot": 8, "form": "output", "type": "float4"}])
    _check(over["blocked"] == "colour_attachment_slot_out_of_bridge_range",
           "out-of-range attachment refusal", failures)

    # Descriptor derivation from the synthetic AIR samples.
    vd = plan_vertex_descriptor(sample_ll)
    _check(vd["blocked"] is None and vd["attributes"][0]["format"] == 31
           and vd["layouts"][0]["step_function"] == 1,
           "ordinary vertex descriptor derivation", failures)
    pd = plan_vertex_descriptor('!19 = !{i32 0, !"air.patch_control_point_input", !21}\n'
                                '!21 = !{!"air.location_index", i32 1, i32 1, '
                                '!"air.arg_type_name", !"float"}\n'
                                '!34 = !{!"air.patch", !"quad"}\n')
    _check(pd["attributes"] == [{"index": 1, "format": 28, "offset": 0, "buffer_index": 0}]
           and pd["layouts"][0]["step_function"] == 4,
           "patch control-point descriptor derivation", failures)

    fc = {"index": 0, "name": "V3HLR::kXhlrbTileSize", "type": "ushort", "required": True}
    can = choose_constant_value(fc, "canonical")
    alt = choose_constant_value(fc, "alternate")
    _check(can["value_hex"] == "0100" and alt["value_hex"] == "0200",
           "canonical/alternate values are explicit and distinct", failures)
    _check(can["origin"].startswith("synthetic_diagnostic:"),
           "synthetic constant origin is labelled", failures)
    count = choose_constant_value(
        {"index": 6, "name": "fc_attachment_count", "type": "uchar", "required": True},
        "canonical", attachments=[{"format": 80, "write_mask": 15}] * 2)
    _check(count["value"] == 2 and count["origin"] == "descriptor_derived:attachment_count",
           "attachment-count constant is descriptor-derived", failures)

    _check(classify_error("needs pipeline input: FunctionConstant { index: 7 }")["class"]
           == "missing_function_constant", "constant error classification", failures)
    _check(classify_error("needs pipeline input: StorageTextureFormat { slot: 1 }")["class"]
           == "missing_pipeline_input", "other missing-input classification", failures)
    _check(classify_error("invalid descriptor: VertexAttribute { attribute: 4 }")["attribute"] == 4,
           "vertex attribute error classification", failures)
    _check(classify_error("unsupported expanded render stage topology")["class"]
           == "bridge_stage_topology_refused", "topology error classification", failures)
    _check(classify_error("attachment format or write mask outside bounded class")["class"]
           == "bridge_attachment_format_surface", "format-surface error classification", failures)
    _check(classify_error("unsupported: Reflection (x); needs NotImplemented")["class"]
           == "translator_reflection_unsupported", "reflection error classification", failures)
    _check(classify_error("unsupported: Tessellation; needs NotImplemented")["class"]
           == "translator_tessellation_unsupported", "tessellation error classification", failures)

    # Resume identity: stable for an identical core, changed by a value or compiler.
    core = {"schema": SCHEMA, "kind": "compute_fc", "case": "x", "policy": "canonical",
            "compiler": {"compiler_sha256": "a"}, "initial_constants": {"kernel": [can]}}
    id1 = _finalise_variant(core, {"family": "FunctionConstant"})["id"]
    id2 = _finalise_variant(json.loads(json.dumps(core)), {"family": "FunctionConstant"})["id"]
    core2 = json.loads(json.dumps(core))
    core2["initial_constants"]["kernel"][0]["value_hex"] = "0200"
    id3 = _finalise_variant(core2, {"family": "FunctionConstant"})["id"]
    core3 = json.loads(json.dumps(core))
    core3["compiler"]["compiler_sha256"] = "b"
    id4 = _finalise_variant(core3, {"family": "FunctionConstant"})["id"]
    _check(id1 == id2 and id1 != id3 and id1 != id4,
           "candidate identity covers value and compiler", failures)
    done = {id1: {"variant_sha256": "v", "compiler_sha256": "a", "bridge_sha256": "b"}}
    spec = {"id": id1, "variant_sha256": "v"}
    _check(_is_done(done, spec, "a", "b"), "resume accepts identical input", failures)
    _check(not _is_done(done, spec, "a", "c"), "resume rejects changed bridge", failures)
    _check(not _is_done(done, {"id": id1, "variant_sha256": "w"}, "a", "b"),
           "resume rejects changed variant", failures)

    if host:
        host_dir = out_dir / "selftest-host"
        host_dir.mkdir(parents=True, exist_ok=True)
        representative = [
            ("graphics_mrt", "graphics-00081",
             {"translated_native_compiled", "translated_native_refused",
              "translated_backend_abort"}),
            ("graphics_mrt", "graphics-00003",
             {"translated_native_compiled", "translated_native_refused",
              "translated_backend_abort", "bridge_stage_topology_refused"}),
            ("graphics_patch", "graphics-00037", {"bridge_stage_topology_refused"}),
            ("graphics_fc", "graphics-00063",
             {"translated_native_compiled", "translated_native_refused",
              "translated_backend_abort"}),
            ("graphics_fc", "graphics-00084",
             {"translation_refused", "worker_abort_before_translation"}),
            ("graphics_mrt", "graphics-01566",
             {"translated_native_refused", "translated_native_compiled"}),
            ("graphics_mrt", "graphics-01316",
             {"translated_native_refused", "translated_native_compiled"}),
            ("compute_fc", "3ed15613d27361700342b64b6529010fa725fa97711c2679c8572aa41ead9ddb",
             {"translated_backend_abort", "translated_native_refused",
              "translated_native_compiled"}),
            ("compute_fc", "b3e13d96d391f0a5eff89de320419af19300a8af3d16087f4c68252cd77eb9d7",
             {"translated_native_compiled", "translated_native_refused"}),
        ]
        source_kwargs = {}
        if current:
            source_kwargs = {
                "compiler_path": current_source_paths()["compiler"],
                "bridge_path": current_source_paths()["bridge"],
                "profile_path": current_source_paths()["profile"],
                "expected_compiler_sha256": CURRENT_COMPILER_SHA256,
                "compute_entry": "v2",
            }
        for family, case_id, allowed in representative:
            fam_dir = host_dir / family / re.sub(r"[^A-Za-z0-9_.-]+", "_", case_id)
            fam_dir.mkdir(parents=True, exist_ok=True)
            plan = plan_variants(corpus, fam_dir, family, 0, case_id, False, False, None,
                                 **source_kwargs)
            if not plan["variants"]:
                failures.append(f"host plan produced no variants for {case_id}")
                continue
            run_variants(corpus, fam_dir, 1, 1, False, False)
            summary = summarize(corpus, fam_dir)
            rows = list(read_jsonl(fam_dir / "results.jsonl"))
            _check(len(rows) == 1, f"host run count for {case_id}", failures)
            if rows:
                _check(rows[0]["outcome_class"] in allowed,
                       f"host outcome for {case_id}: {rows[0]['outcome_class']}", failures)
                _check(rows[0]["translation_status"] in ("translated", "refused"),
                       f"host translation status for {case_id}", failures)

    if failures:
        print("SELFTEST FAIL")
        for item in failures:
            print(" -", item)
        return 1
    print(f"SELFTEST PASS ({'pure+host' if host else 'pure'})")
    return 0


# ---------------------------------------------------------------------------
# CLI.
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
    parser.add_argument("--output", type=Path, default=None)
    sub = parser.add_subparsers(dest="command", required=True)

    ledger = sub.add_parser("ledger")
    ledger.add_argument("--output", type=Path, required=True)

    plan = sub.add_parser("plan")
    plan.add_argument("--output", type=Path, required=True)
    plan.add_argument("--family", choices=["graphics_mrt", "graphics_patch", "graphics_fc",
                                           "compute_fc", "all"], required=True)
    plan.add_argument("--limit", type=int, default=0)
    plan.add_argument("--ids", default=None)
    plan.add_argument("--representative", action="store_true")
    plan.add_argument("--explore-alternates", action="store_true")
    plan.add_argument("--include-optional", action="store_true")
    plan.add_argument("--captured-constants", type=Path, default=None)
    plan.add_argument("--allow-compiler-change", action="store_true")
    plan.add_argument("--current", action="store_true",
                      help="use the takeover compiler/bridge snapshot")
    plan.add_argument("--compiler", type=Path, default=None)
    plan.add_argument("--bridge", type=Path, default=None)
    plan.add_argument("--profile", type=Path, default=None)
    plan.add_argument("--expected-compiler-sha256", default=None)
    plan.add_argument("--expected-bridge-sha256", default=None)
    plan.add_argument("--priority-manifest", type=Path, default=None)
    plan.add_argument("--compute-entry", choices=["auto", "v1", "v2"], default="auto")

    run = sub.add_parser("run")
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--limit", type=int, default=0)
    run.add_argument("--workers", type=int, default=1)
    run.add_argument("--no-resume", action="store_true")
    run.add_argument("--allow-compiler-change", action="store_true")
    run.add_argument("--cpu-limit", type=int, default=12)

    summary = sub.add_parser("summarize")
    summary.add_argument("--output", type=Path, required=True)

    classify = sub.add_parser("classify")
    classify.add_argument("--output", type=Path, required=True)
    classify.add_argument("--family", choices=["graphics_mrt", "graphics_patch", "graphics_fc",
                                               "compute_fc", "all"], required=True)
    classify.add_argument("--limit", type=int, default=0)
    classify.add_argument("--ids", default=None)
    classify.add_argument("--representative", action="store_true")

    priority = sub.add_parser("priority")
    priority.add_argument("--output", type=Path, required=True)
    priority.add_argument("--worker-evidence", type=Path, default=DEFAULT_WORKER / "evidence")
    priority.add_argument("--compiler", type=Path, default=None)
    priority.add_argument("--bridge", type=Path, default=None)
    priority.add_argument("--springboard-critical", type=Path, default=None)

    test = sub.add_parser("selftest")
    test.add_argument("--host", action="store_true")
    test.add_argument("--current", action="store_true")
    test.add_argument("--output", type=Path, default=None)

    worker = sub.add_parser("worker")
    worker.add_argument("spec", type=Path)
    args = parser.parse_args(argv)
    corpus = args.corpus.resolve()
    if args.command == "worker":
        if args.output is None:
            return 2
        return worker_main(args.spec, corpus, args.output.resolve())
    if args.command == "ledger":
        result = build_ledger(corpus, args.output.resolve())
        print(json.dumps({k: v for k, v in result.items() if k != "entry_status_counts"},
                         indent=2))
        return 0
    if args.command == "plan":
        compiler = args.compiler
        bridge = args.bridge
        profile = args.profile
        expected_compiler = args.expected_compiler_sha256
        expected_bridge = args.expected_bridge_sha256
        if args.current:
            current = current_source_paths()
            compiler = compiler or current["compiler"]
            bridge = bridge or current["bridge"]
            profile = profile or current["profile"]
            expected_compiler = expected_compiler or CURRENT_COMPILER_SHA256
        result = plan_variants(
            corpus, args.output.resolve(), args.family, args.limit, args.ids,
            args.representative, args.explore_alternates, args.captured_constants,
            args.include_optional, args.allow_compiler_change,
            compiler_path=compiler, bridge_path=bridge, profile_path=profile,
            expected_compiler_sha256=expected_compiler,
            expected_bridge_sha256=expected_bridge,
            priority_manifest=args.priority_manifest,
            compute_entry=("v2" if (args.compute_entry == "auto" and args.current)
                           else "v1" if args.compute_entry == "auto"
                           else args.compute_entry))
        print(json.dumps(result, indent=2))
        return 0
    if args.command == "run":
        result = run_variants(corpus, args.output.resolve(), args.limit, args.workers,
                              not args.no_resume, args.allow_compiler_change,
                              args.cpu_limit)
        print(json.dumps(result, indent=2))
        return 0
    if args.command == "summarize":
        result = summarize(corpus, args.output.resolve())
        print(json.dumps({k: v for k, v in result.items()
                          if k in ("results", "unique_cases",
                                   "unique_case_outcomes",
                                   "counts_by_family_and_outcome",
                                   "descriptor_materialised_cases")}, indent=2))
        return 0
    if args.command == "classify":
        result = classify_inputs(corpus, args.output.resolve(), args.family, args.limit,
                                 args.ids, args.representative)
        print(json.dumps({k: v for k, v in result.items()
                          if k in ("cases", "status_counts", "blocked_reasons")}, indent=2))
        return 0
    if args.command == "priority":
        compiler = (args.compiler or
                    ROOT / "gpu" / "build" / "mesa-build" / "src" / "panfrost" / "clc" /
                    "libnord2_mali_runtime.so")
        bridge = (args.bridge or
                  ROOT / "gpu" / "build" / "deepseek-parallel-20261025" / "workers" /
                  "compute" / "cargo-target" / "release" / "libtegu_air_bridge.so")
        result = build_priority_manifest(corpus, args.worker_evidence.resolve(),
                                         args.output.resolve(),
                                         compiler.resolve(), bridge.resolve(),
                                         args.springboard_critical)
        print(json.dumps({k: v for k, v in result.items()
                          if k in ("remaining_total", "remaining_graphics_fc",
                                   "remaining_compute_fc", "priority_tiers",
                                   "origin_ranks", "quartzcore_cases",
                                   "observed_p0_rows_remaining",
                                   "observed_entries_total",
                                   "observed_outside_remaining",
                                   "springboard_or_backboard_cases")}, indent=2))
        return 0
    if args.command == "selftest":
        out = (args.output or (DEFAULT_WORKER / "selftest" / time.strftime("%Y%m%d-%H%M%S")))
        return selftest(args.host, corpus, out.resolve(), args.current)
    return 2


if __name__ == "__main__":
    sys.exit(main())
