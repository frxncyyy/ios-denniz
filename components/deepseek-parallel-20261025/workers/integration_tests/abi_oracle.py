#!/usr/bin/env python3
"""Generate the compute v2 C/Rust ABI oracle from the actual Rust source.

The layout is computed from the parsed #[repr(C)] declarations in
gpu/metal/tegu/air_bridge.rs (never from a handwritten size table), checked
against the Rust compiler's own const asserts, then compared with a C probe
compiled against the production header.  A missing C field is a compile error.
"""
from __future__ import annotations

import re
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

RUST_TYPES = {
    "u8": (1, 1),
    "u16": (2, 2),
    "u32": (4, 4),
    "i32": (4, 4),
    "f32": (4, 4),
    "u64": (8, 8),
    "usize": (8, 8),
}

# Rust struct -> production C typedef.
C_NAMES = {
    "ComputeResource": "TeguAIRComputeResource",
    "ComputeDescriptor": "TeguAIRComputeDescriptor",
    "ComputeRegion": "TeguAIRComputeRegion",
    "NativeDispatchV2": "TeguAIRNativeDispatchV2",
    "GraphicsSpecConstant": "TeguAIRGraphicsSpecConstant",
    "GraphicsSpecializationBinding": "TeguAIRGraphicsSpecializationBinding",
    "GraphicsSpecializationPlan": "TeguAIRGraphicsSpecializationPlan",
}


@dataclass
class Field:
    name: str
    type: str
    count: int
    size: int
    align: int
    nested: "Struct | None" = None

    @property
    def total(self) -> int:
        return (self.nested.size if self.nested else self.size) * self.count

    @property
    def field_align(self) -> int:
        return self.nested.align if self.nested else self.align


@dataclass
class Struct:
    name: str
    fields: list[Field]

    @property
    def align(self) -> int:
        return max((f.field_align for f in self.fields), default=1)

    def offsets(self) -> dict[str, int]:
        out: dict[str, int] = {}
        at = 0
        for f in self.fields:
            al = f.field_align
            at = (at + al - 1) // al * al
            out[f.name] = at
            at += f.total
        return out

    @property
    def size(self) -> int:
        at = max(self.offsets().values(), default=0)
        last = self.fields[-1] if self.fields else None
        if last:
            at += last.total
        return (at + self.align - 1) // self.align * self.align


class OracleError(RuntimeError):
    pass


def _constants(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for m in re.finditer(r"pub const\s+(\w+)\s*:\s*usize\s*=\s*(\d+)\s*;", text):
        out[m.group(1)] = int(m.group(2))
    return out


def _split_fields(body: str) -> list[str]:
    parts, depth, start = [], 0, 0
    for i, ch in enumerate(body):
        if ch in "[(":
            depth += 1
        elif ch in "])":
            depth -= 1
        elif ch == "," and depth == 0:
            parts.append(body[start:i])
            start = i + 1
    tail = body[start:]
    if tail.strip():
        parts.append(tail)
    return [p.strip() for p in parts if p.strip()]


def _field(text: str, consts: dict[str, int], known: dict[str, "Struct"]) -> Field:
    m = re.match(r"(?:pub\s+)?(\w+)\s*:\s*(.+)", text, re.S)
    if not m:
        raise OracleError(f"cannot parse field: {text!r}")
    name, ty = m.group(1), m.group(2).strip()
    count = 1
    arr = re.match(r"\[\s*(\w+)\s*(?:;\s*(\w+))?\s*\]$", ty)
    if arr:
        ty, n = arr.group(1), arr.group(2)
        if not n:
            count = 1
        elif n.isdigit():
            count = int(n)
        elif n in consts:
            count = consts[n]
        else:
            raise OracleError(f"unknown array constant {n}")
    if ty in RUST_TYPES:
        size, align = RUST_TYPES[ty]
        return Field(name, ty, count, size, align)
    if ty in known:
        return Field(name, ty, count, 0, 0, known[ty])
    raise OracleError(f"unsupported field type {ty} in {text!r}")


def parse_rust(path: Path) -> dict[str, Struct]:
    text = path.read_text()
    consts = _constants(text)
    raw: list[tuple[str, str]] = []
    for m in re.finditer(
        r"#\[repr\(C\)\]\s*(?:#\[derive\([^]]*\)\]\s*)?pub struct\s+(\w+)\s*\{(.*?)\}",
        text,
        re.S,
    ):
        if m.group(1) in set(C_NAMES) | {"NativeConstant"}:
            raw.append((m.group(1), m.group(2)))
    missing = set(C_NAMES) - {name for name, _ in raw}
    if missing:
        raise OracleError(f"missing Rust structs: {sorted(missing)}")
    # Resolve nested struct arrays regardless of declaration order.
    pending = dict(raw)
    structs: dict[str, Struct] = {}
    progress = True
    while pending and progress:
        progress = False
        for name, body in list(pending.items()):
            try:
                fields = [_field(p, consts, structs) for p in _split_fields(body)]
            except OracleError:
                continue
            structs[name] = Struct(name, fields)
            del pending[name]
            progress = True
    if pending:
        raise OracleError(f"unresolved nested structs: {sorted(pending)}")
    return structs


def rust_declared_sizes(path: Path) -> dict[str, int]:
    text = path.read_text()
    out: dict[str, int] = {}
    for m in re.finditer(r"size_of::<(\w+)>\(\)\s*==\s*(\d+)", text):
        out[m.group(1)] = int(m.group(2))
    return out


def c_probe(structs: dict[str, Struct]) -> str:
    lines = [
        "#include <stddef.h>",
        "#include <stdio.h>",
        '#include "tegu/air_bridge.h"',
        "int main(void) {",
    ]
    for rust_name, c_name in C_NAMES.items():
        struct = structs[rust_name]
        lines.append(f'  printf("{c_name} %zu\\n", sizeof({c_name}));')
        for field in struct.fields:
            lines.append(
                f'  printf("{c_name}.{field.name} %zu\\n", '
                f"offsetof({c_name}, {field.name}));"
            )
            lines.append(
                f'  printf("{c_name}.{field.name}.bytes %zu\\n", '
                f"sizeof((({c_name} *)0)->{field.name}));"
            )
            if field.count > 1:
                lines.append(
                    f'  printf("{c_name}.{field.name}.count %zu\\n", '
                    f"sizeof((({c_name} *)0)->{field.name}) / "
                    f"sizeof((({c_name} *)0)->{field.name}[0]));"
                )
    lines.append("  return 0;")
    lines.append("}")
    return "\n".join(lines) + "\n"


def run_c_probe(gpu_root: Path, structs: dict[str, Struct], out_dir: Path) -> tuple[bool, dict[str, int], str]:
    src = out_dir / "abi_probe_generated.c"
    exe = out_dir / "abi_probe_generated"
    src.write_text(c_probe(structs))
    cmd = [
        "cc", "-std=c11", "-O0", "-g", "-Wall", "-Wextra", "-Werror",
        "-I" + str(gpu_root / "include"),
        "-I" + str(gpu_root / "metal"),
        "-I" + str(gpu_root / "metal/tegu"),
        "-I" + str(gpu_root / "build/tegu-frontend"),
        str(src), "-o", str(exe),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode:
        return False, {}, proc.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    if run.returncode:
        return False, {}, run.stderr
    values: dict[str, int] = {}
    for line in run.stdout.splitlines():
        key, value = line.rsplit(" ", 1)
        values[key] = int(value)
    return True, values, ""


def check(gpu_root: Path | None = None) -> dict:
    """Return a JSON-friendly ABI result. Raises OracleError on source faults."""
    gpu_root = Path(gpu_root or Path(__file__).resolve().parents[4])
    rust_path = gpu_root / "metal/tegu/air_bridge.rs"
    structs = parse_rust(rust_path)
    declared = rust_declared_sizes(rust_path)
    errors: list[str] = []
    expected: dict[str, int] = {}
    for name, struct in structs.items():
        if name not in C_NAMES:
            continue
        if name in declared and declared[name] != struct.size:
            errors.append(
                f"{name}: Rust source layout computes {struct.size}, "
                f"but the Rust const assert declares {declared[name]}"
            )
        for fname, off in struct.offsets().items():
            expected[f"{C_NAMES[name]}.{fname}"] = off
            field = next(f for f in struct.fields if f.name == fname)
            expected[f"{C_NAMES[name]}.{fname}.bytes"] = field.total
            if field.count > 1:
                expected[f"{C_NAMES[name]}.{fname}.count"] = field.count
        expected[C_NAMES[name]] = struct.size
    with tempfile.TemporaryDirectory(prefix="nord2-abi-") as tmp:
        ok, actual, stderr = run_c_probe(gpu_root, structs, Path(tmp))
    if not ok:
        errors.append("C probe did not compile against the production header:\n" + stderr.strip())
        return {"ok": False, "errors": errors, "expected": expected, "actual": {}}
    mismatches = []
    for key, want in expected.items():
        got = actual.get(key)
        if got is None:
            mismatches.append(f"{key}: missing from C probe")
        elif got != want:
            mismatches.append(f"{key}: C={got}, Rust-layout={want}")
    if mismatches:
        errors.extend(mismatches)
    return {
        "ok": not errors,
        "errors": errors,
        "expected": expected,
        "actual": actual,
        "rust_constants": _constants(rust_path.read_text()),
        "rust_source": str(rust_path),
        "rust_sha256": _sha256(rust_path),
    }


def _sha256(path: Path) -> str:
    import hashlib

    return hashlib.sha256(path.read_bytes()).hexdigest()


if __name__ == "__main__":
    import json
    import sys

    result = check()
    print(json.dumps(result, indent=2))
    sys.exit(0 if result["ok"] else 1)
