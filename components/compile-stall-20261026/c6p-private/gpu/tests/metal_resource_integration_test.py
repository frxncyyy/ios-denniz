#!/usr/bin/env python3
"""End-to-end Metal/compute resource integration host test.

Runs, with no device or GPU execution:
  1. generated C-vs-Rust ABI oracle for the compute v2 structs;
  2. the production TGP2 header/manifest parser against required and
     explicitly pending contract boundaries;
  3. the active n2mb selector-5/7 transport through a fake JM kernel model,
     decoding COMPUTE_JOB, SHADER_PROGRAM, LOCAL_STORAGE, FAU, SRT tables and
     plane descriptors, with pinned Mesa v10 texture/plane/sampler goldens;
  4. the real Rust planner cdylib on the real AIR fixtures, including
     descriptor/native-binding/region/specialization assertions and refusals.

Exit status is nonzero for any required failure and, unless --allow-pending is
given, for explicitly open cross-owner contract questions.  Pending failures
are printed and recorded; they are never treated as supported behaviour.

Usage:
  python3 gpu/tests/metal_resource_integration_test.py
  python3 gpu/tests/metal_resource_integration_test.py --allow-pending
  python3 gpu/tests/metal_resource_integration_test.py --skip-sanitize
  python3 gpu/tests/metal_resource_integration_test.py --candidate
  TEGU_AIR_BRIDGE_SO=/path/libtegu_air_bridge.so ...
  TEGU_COMPUTE_BUILD_RECEIPT=/path/result.json ...
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

GPU = Path(__file__).resolve().parents[1]
WORK = GPU.parent
WORKER = WORK / "gpu/build/deepseek-parallel-20261025/workers/integration_tests"
# Root may integrate the worker helpers next to the test; accept either layout.
HELPER_DIRS = [WORKER, GPU / "tests", GPU / "tests" / "support"]
BUILD = WORKER / "build"
REPORT = WORKER / "report.json"
CC = os.environ.get("CC", "cc")
CLANG = os.environ.get("CLANG", "clang")


def _pipeline_define(name: str) -> int:
    text = (GPU / "metal/tegu/pipeline_format.h").read_text()
    match = re.search(rf"#define\s+{name}\s+(0x[0-9a-fA-F]+|\d+)", text)
    if not match:
        raise RuntimeError(f"pipeline_format.h does not define {name}")
    return int(match.group(1), 0)


TEGU_PIPELINE2_MAGIC = _pipeline_define("TEGU_PIPELINE2_MAGIC")
TEGU_PIPELINE2_VERSION = _pipeline_define("TEGU_PIPELINE2_VERSION")
TEGU_PIPELINE2_HEADER_BYTES = _pipeline_define("TEGU_PIPELINE2_HEADER_BYTES")

SOURCES = [
    "metal/tegu/air_bridge.rs",
    "metal/tegu/air_bridge.h",
    "metal/tegu/pipeline_format.h",
    "metal/nord2_metal_bridge.c",
    "metal/nord2_metal_bridge.h",
    "metal/nord2_metal_render.c",
    "metal/nord2_metal_render.h",
    "metal/nord2_render_resources.inc",
    "metal/nord2_render_pass.inc",
    "metal/tegu/vertex_fetch.c",
    "metal/tegu/vertex_fetch.h",
    "metal/tegu/generic_job_ui.h",
    "metal/tegu/sampler_state.inc",
    "metal/nord2_texture_resources.h",
    "src/nord2_gpu_jm.c",
    "src/nord2_gpu_desc.c",
    "include/nord2_gpu_jm.h",
    "include/nord2_gpu_desc.h",
    "include/nord2_gpu_transport.h",
]
CONTEXT_SOURCES = [
    "metal/tegu/runtime_pipeline.inc",
    "metal/tegu/compute_frontend.m",
    "metal/tegu/render_pipeline.inc",
    "metal/tegu/render_encoder.inc",
    "metal/tegu/compositor_submit.inc",
    "metal/tegu/ws_selector_compat.inc",
    "metal/tegu/device_limits.h",
    "metal/tegu/compositor_process.h",
    "metal/tegu/mali_compute_compiler.c",
    "metal/tegu/mali_compute_compiler.h",
    "metal/tegu/mali_graphics_compiler.c",
    "metal/tegu/mali_graphics_compiler.h",
]

COMMON = [
    "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
    "-Wno-unused-parameter", "-Wno-misleading-indentation", "-Wno-array-parameter",
    "-I" + str(GPU / "include"),
    "-I" + str(GPU / "metal"),
    "-I" + str(GPU / "metal/tegu"),
    "-I" + str(GPU / "build/tegu-frontend"),
]
MESA = [
    "-DUTIL_ARCH_LITTLE_ENDIAN=1", "-DUTIL_ARCH_BIG_ENDIAN=0",
    "-I" + str(GPU / "build/mesa-build/src/panfrost"),
    "-I" + str(GPU / "build/mesa-src/src/panfrost"),
    "-I" + str(GPU / "build/mesa-build/src"),
    "-I" + str(GPU / "build/mesa-src/src"),
    "-I" + str(GPU / "build/mesa-build/include"),
    "-I" + str(GPU / "build/mesa-src/include"),
]
def helper(name: str) -> Path:
    for directory in HELPER_DIRS:
        candidate = directory / name
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        f"{name} not found in any of: " + ", ".join(str(d) for d in HELPER_DIRS)
    )


JOB_SOURCES = [
    str(helper("n2mb_v2_job_test.c")),
    str(GPU / "metal/nord2_metal_bridge.c"),
    str(GPU / "src/nord2_gpu_jm.c"),
    str(GPU / "src/nord2_gpu_desc.c"),
    str(GPU / "metal/nord2_metal_render.c"),
    str(GPU / "metal/tegu/blend_state.c"),
    str(GPU / "metal/tegu/texture_state_mesa_golden.c"),
    str(helper("n2mb_v9_golden.c")),
]
DRAW_SOURCES = [
    str(helper("n2mb_draw_v2_test.c")),
    str(GPU / "metal/nord2_metal_bridge.c"),
    str(GPU / "metal/nord2_metal_render.c"),
    str(GPU / "metal/tegu/blend_state.c"),
    str(GPU / "src/nord2_gpu_jm.c"),
    str(GPU / "src/nord2_gpu_desc.c"),
]
V83_SOURCES = [
    str(helper("format83_vertex_fetch_test.c")),
    str(GPU / "metal/tegu/vertex_fetch.c"),
    str(GPU / "metal/nord2_metal_render.c"),
    str(GPU / "metal/tegu/blend_state.c"),
    str(helper("n2mb_v9_golden.c")),
]
SUMMARY_RE = re.compile(
    r"(?P<checks>\d+) checks, (?P<required>\d+) required failures, "
    r"(?P<pending>\d+) pending failures"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hash_tree(root: Path) -> dict[str, str]:
    """SHA256 every production source file under root, excluding caches."""
    out: dict[str, str] = {}
    if not root.exists():
        return out
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if "__pycache__" in path.parts or path.suffix in (".pyc", ".pyo"):
            continue
        out[str(path.relative_to(WORK))] = sha256(path)
    return out


def find_cdylib() -> Path:
    env = os.environ.get("TEGU_AIR_BRIDGE_SO")
    candidates = []
    if env:
        candidates.append(Path(env))
    candidates += [
        WORKER / "cargo-target/release/libtegu_air_bridge.so",
        WORK /
        "gpu/build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so",
        GPU / "build/ui-corpus-20261025/baseline/libtegu_air_bridge.so",
    ]
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError("no libtegu_air_bridge.so found")


def candidate_gate() -> dict:
    """Release gate: source tree, cdylib and compiler provenance must agree."""
    errors: list[str] = []
    warnings: list[str] = []
    metal = hash_tree(GPU / "metal")
    src = hash_tree(GPU / "src")
    include = hash_tree(GPU / "include")
    rust_path = GPU / "metal/tegu/air_bridge.rs"
    rust_hash = sha256(rust_path) if rust_path.exists() else None
    try:
        cdylib = find_cdylib()
        cdylib_hash = sha256(cdylib)
    except Exception as exc:
        cdylib, cdylib_hash = None, None
        errors.append(f"host cdylib unavailable: {exc}")
    compiler = GPU / "build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so"
    compiler_hash = sha256(compiler) if compiler.exists() else None
    if not compiler_hash:
        errors.append(f"compiler library missing: {compiler}")

    receipt_paths: list[Path] = []
    env_receipt = os.environ.get("TEGU_COMPUTE_BUILD_RECEIPT")
    if env_receipt:
        receipt_paths.append(Path(env_receipt))
    receipt_paths += [
        Path(p) for p in sorted(glob.glob(str(
            WORK / "gpu/build/compute-v2-producer-*/result.json")))
    ]
    if not receipt_paths:
        errors.append("no compute build receipt (compute-v2-producer-*/result.json)")
    receipts = []
    chosen = None
    for path in receipt_paths:
        try:
            data = json.loads(path.read_text())
        except Exception as exc:
            errors.append(f"receipt unreadable {path}: {exc}")
            continue
        receipts.append({"path": str(path), "sha256": sha256(path), "data": data})
        if (data.get("producer_sha256") == cdylib_hash and
                data.get("producer_rust_sha256") == rust_hash):
            chosen = data
    if chosen is None:
        errors.append(
            "no build receipt matches current air_bridge.rs and host cdylib")
    else:
        if chosen.get("compiler_sha256") != compiler_hash:
            warnings.append(
                "compiler hash differs from compute build receipt (newer "
                f"compiler is allowed): receipt={chosen.get('compiler_sha256')} "
                f"actual={compiler_hash}")
        receipt_rust = chosen.get("producer_rust_source")
        if receipt_rust and Path(receipt_rust).name != "air_bridge.rs":
            errors.append(f"receipt producer source is not air_bridge.rs: {receipt_rust}")
    manifest_sha = hashlib.sha256()
    for path, digest in sorted(metal.items()):
        manifest_sha.update(f"{path}\0{digest}\n".encode())
    return {
        "ok": not errors,
        "errors": errors,
        "warnings": warnings,
        "metal_manifest_sha256": manifest_sha.hexdigest(),
        "metal_file_count": len(metal),
        "metal_sources": metal,
        "src_sources": src,
        "include_sources": include,
        "cdylib": str(cdylib) if cdylib else None,
        "cdylib_sha256": cdylib_hash,
        "compiler": str(compiler),
        "compiler_sha256": compiler_hash,
        "air_bridge_rs_sha256": rust_hash,
        "receipts": receipts,
        "selected_receipt": chosen,
    }


def source_hashes(names: list[str] | None = None) -> dict[str, str]:
    return {name: sha256(GPU / name) for name in (names or SOURCES) if (GPU / name).exists()}


def worker_hashes() -> dict[str, str]:
    out: dict[str, str] = {}
    for directory in HELPER_DIRS:
        if not directory.exists():
            continue
        for path in sorted(directory.iterdir()):
            if path.is_file() and path.suffix in (".c", ".m", ".inc", ".py", ".md"):
                out[str(path)] = sha256(path)
    return out


def run(cmd: list[str], *, env=None, timeout=120) -> dict:
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                          env=env)
    return {
        "command": cmd,
        "returncode": proc.returncode,
        "stdout": proc.stdout,
        "stderr": proc.stderr,
    }


def compile_and_run(name: str, sources: list[str], extra: list[str],
                    *, sanitize: bool = False, timeout: int = 120) -> dict:
    BUILD.mkdir(parents=True, exist_ok=True)
    exe = BUILD / (name + ("_asan" if sanitize else ""))
    args = [CC] + COMMON + MESA + extra + sources + ["-o", str(exe), "-lm"]
    if sanitize:
        args[1:1] = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    built = run(args, timeout=240)
    if built["returncode"] != 0:
        return {"ok": False, "build": built, "run": None, "summary": None}
    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    env.setdefault("UBSAN_OPTIONS", "halt_on_error=1")
    ran = run([str(exe)], env=env, timeout=timeout)
    summary = None
    match = SUMMARY_RE.search(ran["stdout"] + ran["stderr"])
    if match:
        summary = {k: int(v) for k, v in match.groupdict().items()}
    return {
        "ok": ran["returncode"] == 0,
        "build": built,
        "run": ran,
        "summary": summary,
        "binary_sha256": sha256(exe),
    }


def abi_test() -> dict:
    for directory in HELPER_DIRS:
        if (directory / "abi_oracle.py").exists():
            sys.path.insert(0, str(directory))
            break
    import abi_oracle

    try:
        result = abi_oracle.check(GPU)
    except Exception as exc:  # source/parse/compiler failure, never silent
        return {"ok": False, "errors": [f"{type(exc).__name__}: {exc}"],
                "expected": {}, "actual": {}}
    errors = list(result.get("errors", []))
    actual = result.get("actual", {})

    # Do not pin a handwritten size table here: abi_oracle derives every
    # struct/field size and offset from the actual Rust #[repr(C)] source and
    # compares it with a C probe built against the production header.  The
    # gates below add only semantic facts that the generated layout cannot
    # express by itself.
    resource = "TeguAIRComputeResource"
    if resource not in actual:
        errors.append(f"{resource} missing from generated C probe")
    level_counts = {
        key.rsplit(".", 2)[-2]: value
        for key, value in actual.items()
        if key.startswith(resource + ".level_") and key.endswith(".count")
    }
    if not level_counts:
        errors.append(f"{resource} has no per-level array fields in Rust/C layout")
    else:
        max_levels = result.get("rust_constants", {}).get(
            "TEGU_AIR_COMPUTE_MAX_LEVELS"
        )
        if max_levels is None:
            errors.append("Rust source does not declare TEGU_AIR_COMPUTE_MAX_LEVELS")
        elif len(set(level_counts.values())) != 1:
            errors.append(f"per-level array lengths differ: {level_counts}")
        elif next(iter(level_counts.values())) != max_levels:
            errors.append(
                f"per-level arrays have {next(iter(level_counts.values()))} entries; "
                f"Rust TEGU_AIR_COMPUTE_MAX_LEVELS is {max_levels}"
            )
        for field in level_counts:
            off_key = f"{resource}.{field}"
            if off_key not in actual:
                errors.append(f"{off_key} missing from generated C probe")

    result["ok"] = not errors
    result["errors"] = errors
    result["derived"] = {
        "resource_size": actual.get(resource),
        "per_level_array_counts": level_counts,
        "rust_max_levels": result.get("rust_constants", {}).get(
            "TEGU_AIR_COMPUTE_MAX_LEVELS"
        ),
    }
    return result


def d8f18h_packet_provenance() -> dict:
    """Check the embedded D8F18H fixture against its recorded provenance.

    The packet itself is regenerated (and the C header rewritten) only by
    regenerate_d8f18h_packet.py with compiler 742cdf3d.  This gate verifies the
    checked-in bytes are exactly those recorded, not a substituted packet.
    """
    header_path = helper("d8f18h_pkg.h")
    meta_path = helper("d8f18h_pkg.json")
    try:
        text = header_path.read_text()
        meta = json.loads(meta_path.read_text())
    except Exception as exc:
        return {"ok": False, "error": f"{type(exc).__name__}: {exc}"}
    match = re.search(
        r"static const unsigned char d8f18h_pkg\[\]\s*=\s*\{(.*?)\};",
        text, re.S,
    )
    if not match:
        return {"ok": False, "error": "generated array not found"}
    try:
        values = bytes(int(v) for v in re.findall(r"\b\d+\b", match.group(1)))
    except ValueError as exc:
        return {"ok": False, "error": f"bad generated byte: {exc}"}
    errors = []
    digest = hashlib.sha256(values).hexdigest()
    if digest != meta.get("package_sha256"):
        errors.append(f"embedded package {digest} != recorded "
                      f"{meta.get('package_sha256')}")
    if len(values) != meta.get("package_size"):
        errors.append(f"embedded size {len(values)} != recorded "
                      f"{meta.get('package_size')}")
    if len(values) < TEGU_PIPELINE2_HEADER_BYTES:
        errors.append("embedded packet shorter than TGP2 header")
    else:
        words = struct.unpack_from("<19I", values)
        if words[0] != TEGU_PIPELINE2_MAGIC or words[1] != TEGU_PIPELINE2_VERSION:
            errors.append("embedded packet is not TGP2 v2")
        if words[17] != meta.get("tls_bytes") or words[18] != meta.get("wls_bytes"):
            errors.append(f"embedded TLS/WLS {words[17]}/{words[18]} != recorded "
                          f"{meta.get('tls_bytes')}/{meta.get('wls_bytes')}")
    if hashlib.sha256(text.encode()).hexdigest() != meta.get("header_sha256"):
        errors.append("header file hash differs from recorded header_sha256")
    spv = GPU / meta.get("source", "")
    if not meta.get("source") or not spv.exists():
        errors.append("recorded SPIR-V source missing")
    elif sha256(spv) != meta.get("spv_sha256"):
        errors.append(f"SPIR-V hash differs from recorded {meta.get('spv_sha256')}")
    return {
        "ok": not errors,
        "errors": errors,
        "package_sha256": digest,
        "package_size": len(values),
        "tls_bytes": meta.get("tls_bytes"),
        "wls_bytes": meta.get("wls_bytes"),
        "spv_sha256": meta.get("spv_sha256"),
        "compiler_sha256": meta.get("compiler_sha256"),
        "header_sha256": meta.get("header_sha256"),
    }


def corpus_crash_regression() -> dict:
    """Run the three materialised translate_graphics SIGSEGV cases isolated."""
    try:
        bridge = find_cdylib()
    except Exception as exc:
        return {"ok": False, "cases": [], "error": f"cdylib unavailable: {exc}"}
    script = helper("corpus_graphics_regression.py")
    outdir = WORKER / "runs"
    outdir.mkdir(parents=True, exist_ok=True)
    results = []
    ok = True
    for case in ("graphics-00086", "graphics-00498", "graphics-01278"):
        out = outdir / f"corpus-{case}.json"
        if out.exists():
            out.unlink()
        proc = run([sys.executable, str(script), "--case", case,
                    "--bridge", str(bridge), "--json", str(out)], timeout=30)
        row = {"case": case, "returncode": proc["returncode"],
               "stdout": proc["stdout"], "stderr": proc["stderr"]}
        if out.exists():
            try:
                row.update(json.loads(out.read_text()))
            except json.JSONDecodeError as exc:
                row["error"] = f"invalid JSON: {exc}"
        passed = (proc["returncode"] == 0 and
                  row.get("status") in ("compiled", "refused"))
        row["passed"] = passed
        if not passed:
            ok = False
        results.append(row)
    return {"ok": ok, "cases": results, "bridge": str(bridge),
            "bridge_sha256": sha256(bridge)}


def render_cache_regression() -> dict:
    """Run render's real frontend harness for per-draw specialized variants."""
    harness = WORK / "gpu/build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py"
    if not harness.exists():
        return {"ok": False, "error": "render host harness missing", "path": str(harness)}
    before = sha256(harness)
    # Thermal-limited single-core hosts may need >6 minutes for the two
    # ASan render-harness compiles.  This is a timeout for the same tests,
    # not a relaxed assertion.
    proc = run([sys.executable, str(harness)], timeout=900)
    after = sha256(harness)
    text = proc["stdout"] + proc["stderr"]
    contract_errors = []
    if "PASS real frontend" not in text:
        contract_errors.append("render harness did not report PASS")
    # Current render contract (DELTA8): path_blit is admitted far enough to
    # reach the true-MRT/fetch decision, then its missing attachment snapshot
    # makes it an inert PSO in backboardd only.  Garbage attachment masks are
    # preserved without implicit clears.  Do not accept the old status4 or a
    # process-unscoped inert result.
    if "PATH_PRESERVE_CMD invalid=0 count=2" not in text:
        contract_errors.append("path preserve/no-clear contract missing")
    if "PATH_BLIT_PIPE" not in text or not re.search(
            r"PATH_BLIT_PIPE .*\binert=1\b.*\btrue=1\b", text):
        contract_errors.append("path_blit MULTI_TARGET/inert backboardd PSO missing")
    if "PATH_BLIT_NONBBD" not in text or not re.search(
            r"PATH_BLIT_NONBBD ptr=(?:\(nil\)|0x0) err=0x", text):
        contract_errors.append("path_blit non-backboardd nil/error scope missing")
    if ("TEGU_INERT_PIPELINE v=path_blit_vert_lpf "
            "f=path_blit_frag_lpf fmt=80,80,10 "
            "reason=true-MRT framebuffer fetch unsupported") not in text:
        contract_errors.append("path_blit inert diagnostic missing")
    if text.count("TEGU_INERT_PIPELINE") != 128:
        contract_errors.append("inert pipeline trace is not bounded at 128")
    if text.count("TEGU_INERT_DRAW_REFUSED") != 32:
        contract_errors.append("inert draw refusal count is not 32")
    if "MRT_PROBE status=4" not in text:
        contract_errors.append("authored true-MRT v4 draw missing")
    if "TLS_PIPE" not in text or "TLS_DRAW status=4" not in text:
        contract_errors.append("graphics TLS16 draw missing")
    if "TEGU_PASS_REFUSED" not in text:
        contract_errors.append("inert pass refusal trace missing")
    return {
        "ok": proc["returncode"] == 0 and not contract_errors,
        "contract_errors": contract_errors,
        "returncode": proc["returncode"],
        "stdout": proc["stdout"], "stderr": proc["stderr"],
        "path": str(harness), "sha256_before": before, "sha256_after": after,
    }


def legacy_sampler_guard_test() -> dict:
    script = helper("legacy_sampler_guard_test.py")
    proc = run([sys.executable, str(script)], timeout=240)
    return {
        "ok": proc["returncode"] == 0 and "PASS legacy sampler word guard" in (proc["stdout"] + proc["stderr"]),
        "returncode": proc["returncode"], "stdout": proc["stdout"],
        "stderr": proc["stderr"], "path": str(script), "sha256": sha256(script),
    }



def inert_render_pass_test() -> dict:
    """Compile the real C3 render-pass admission code against a minimal fake
    Objective-C frontend and exercise refused descriptors directly.

    This is independent of render's large generated harness: only the
    production nord2_render_pass.inc is under test, with fake attachment and
    command objects.  Both release and ASan/UBSan runs must return a non-nil
    inert encoder, poison the command, and emit exactly the 16 bounded
    TEGU_PASS_REFUSED diagnostics.
    """
    source = helper("inert_render_pass_test.m")
    results = []
    checks = required = 0
    errors = []
    for sanitize in (False, True):
        suffix = "_asan" if sanitize else ""
        exe = BUILD / f"inert_render_pass_test{suffix}"
        cmd = [
            CLANG, "-x", "objective-c", "-std=gnu11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
            "-Wno-unused-function", "-Wno-objc-root-class",
            "-I" + str(GPU / "metal"), "-I" + str(GPU / "include"),
        ]
        if sanitize:
            cmd += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        cmd += [str(source), "-lobjc", "-o", str(exe)]
        build = run(cmd, timeout=180)
        if build["returncode"]:
            errors.append(f"{'ASan' if sanitize else 'release'} build: "
                          + build["stderr"].strip()[-2000:])
            results.append({"sanitize": sanitize, "build": build,
                            "returncode": build["returncode"],
                            "stdout": "", "stderr": ""})
            continue
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0",
                   UBSAN_OPTIONS="halt_on_error=1")
        proc = run([str(exe)], timeout=120, env=env)
        text = proc["stdout"] + proc["stderr"]
        match = re.search(
            r"inert render pass: (\d+) checks, (\d+) required failures", text)
        c = int(match.group(1)) if match else 0
        r = int(match.group(2)) if match else 0
        checks += c
        required += r
        refused = text.count("TEGU_PASS_REFUSED")
        reasons = [
            "missing color attachments",
            "multisample color attachment outside exact resolve class",
            "single-sample color attachment outside exact JM class",
            "more than four color attachments",
            "same color texture bound to multiple attachments",
            "color attachment outside exact JM class",
        ]
        missing = [reason for reason in reasons if f"reason={reason}" not in text]
        if proc["returncode"]:
            errors.append(f"{'ASan' if sanitize else 'release'} run rc="
                          f"{proc['returncode']}")
        if not match or c < 40 or r:
            errors.append(f"{'ASan' if sanitize else 'release'} summary "
                          f"checks={c} failures={r}")
        if refused != 16:
            errors.append(f"{'ASan' if sanitize else 'release'} diagnostics="
                          f"{refused}, expected 16")
        if missing:
            errors.append(f"{'ASan' if sanitize else 'release'} missing reasons: "
                          + ", ".join(missing))
        results.append({"sanitize": sanitize, "build": build, "run": proc,
                        "checks": c, "required": r, "refused": refused,
                        "reasons_missing": missing})
    return {
        "ok": not errors,
        "errors": errors,
        "runs": results,
        "path": str(source), "sha256": sha256(source),
        "summary": {"checks": checks, "required": required, "pending": 0},
    }

def coreanimation_render_pass_test() -> dict:
    """Independent CA-shaped pass/JM test.

    This is a separate harness from render's generated frontend test.  It
    compiles the real nord2_render_pass.inc with a small fake ObjC/JM model,
    exercises natural display strides, IOSurface targets, RGBA16F, MRT
    slot1-mask0, all three load actions and the deferred Unknown store, and
    requires fake-JM completion plus independently calculated target bytes.
    It is intentionally red until the production CoreAnimation admission fix
    lands.
    """
    script = helper("coreanimation_render_pass_test.py")
    proc = run([sys.executable, str(script)], timeout=300)
    text = proc["stdout"] + proc["stderr"]
    summaries = re.findall(
        r"COREANIMATION_PASS checks=(\d+) failures=(\d+)", text)
    checks = sum(int(c) for c, _ in summaries)
    failures = sum(int(f) for _, f in summaries)
    ok = (proc["returncode"] == 0 and len(summaries) == 2 and failures == 0
          and "COREANIMATION_PASS_RESULT PASS" in text)
    errors = []
    if not summaries:
        errors.append("missing COREANIMATION_PASS summary")
    elif len(summaries) != 2:
        errors.append(f"expected release+ASan summaries, got {len(summaries)}")
    if failures:
        errors.append(f"checks={checks} failures={failures}")
    if proc["returncode"] != 0:
        errors.append(f"runner rc={proc['returncode']}")
    return {
        "ok": ok,
        "errors": errors,
        "run": None,  # avoid duplicating the many per-case FAIL lines globally
        "stdout": proc["stdout"],
        "stderr": proc["stderr"],
        "path": str(script),
        "sha256": sha256(script),
        "summary": {"checks": checks, "required": 0 if ok else 1, "pending": 0},
    }


def native_compute_frontend_test() -> dict:
    script = helper("native_compute_frontend_test.py")
    proc = run([sys.executable, str(script)], timeout=600)
    text = proc["stdout"] + proc["stderr"]
    selector_ok = ("SELECTOR_AUDIT checks=" in text and
                   "missing=0" in text.split("SELECTOR_AUDIT", 1)[-1])
    quartz_ok = "QUARTZCORE_LIBRARY_OK size=1014944" in text
    errors = []
    if proc["returncode"]:
        errors.append(f"native frontend rc={proc['returncode']}")
    if "PASS native compute frontend" not in text:
        errors.append("native frontend PASS line missing")
    if not quartz_ok:
        errors.append("real QuartzCore library/descriptor test did not pass")
    if not selector_ok:
        errors.append("selector audit did not report missing=0")
    return {
        "ok": not errors and proc["returncode"] == 0,
        "errors": errors,
        "quartzcore_library_ok": quartz_ok,
        "selector_audit_ok": selector_ok,
        "returncode": proc["returncode"], "stdout": proc["stdout"],
        "stderr": proc["stderr"], "path": str(script), "sha256": sha256(script),
    }


def render_variant_threads_test() -> dict:
    script = helper("render_variant_threads_test.py")
    proc = run([sys.executable, str(script)], timeout=300)
    return {
        "ok": proc["returncode"] == 0 and "PASS ASan two-thread" in (proc["stdout"] + proc["stderr"]),
        "returncode": proc["returncode"], "stdout": proc["stdout"],
        "stderr": proc["stderr"], "path": str(script), "sha256": sha256(script),
    }


def producer_test() -> dict:
    BUILD.mkdir(parents=True, exist_ok=True)
    out = BUILD / "producer.json"
    if out.exists():
        out.unlink()
    proc = run([sys.executable, str(helper("producer_plan_test.py")),
                "--json", str(out)], timeout=240)
    result = {
        "ok": False,
        "returncode": proc["returncode"],
        "stdout": proc["stdout"],
        "stderr": proc["stderr"],
    }
    if out.exists():
        try:
            result.update(json.loads(out.read_text()))
        except json.JSONDecodeError as exc:
            result["error"] = f"producer JSON invalid: {exc}"
    else:
        result["error"] = "producer test produced no JSON (library unavailable or child crashed)"
    result["ok"] = bool(result.get("ok")) and proc["returncode"] == 0
    return result


def summarize_checks(result: dict) -> tuple[int, int, int]:
    if result.get("summary"):
        s = result["summary"]
        return s["checks"], s["required"], s["pending"]
    checks = result.get("checks") or []
    failed = sum(1 for c in checks if not c.get("ok"))
    return len(checks), failed, 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--allow-pending", action="store_true",
                        help="do not fail on explicitly recorded pending contract gaps")
    parser.add_argument("--skip-sanitize", action="store_true")
    parser.add_argument("--candidate", action="store_true",
                        help="release gate: hash all gpu/metal sources, cdylib and "
                             "compiler; fail unless the build receipt matches")
    parser.add_argument("--json", type=Path, default=REPORT)
    args = parser.parse_args()

    compiler = GPU / "build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so"
    report: dict = {
        "gpu_executed": False,
        "phone_accessed": False,
        "native_build": False,
        "sources": source_hashes(),
        "context_sources": source_hashes(CONTEXT_SOURCES),
        "test_sources": worker_hashes(),
        "compiler": {
            "path": str(compiler),
            "sha256": sha256(compiler) if compiler.exists() else None,
        },
        "tests": {},
    }

    candidate = None
    if args.candidate:
        print("== candidate release gate ==")
        candidate = candidate_gate()
        report["candidate"] = candidate
        if candidate["ok"]:
            print(f"PASS candidate provenance metal_files={candidate['metal_file_count']} "
                  f"manifest={candidate['metal_manifest_sha256'][:16]} "
                  f"cdylib={candidate['cdylib_sha256'][:16] if candidate['cdylib_sha256'] else 'none'}")
        else:
            print("FAIL candidate provenance")
            for error in candidate["errors"]:
                print("  " + error)

    print("== ABI oracle ==")
    abi = abi_test()
    report["tests"]["abi"] = abi
    if abi.get("ok"):
        sizes = {k: v for k, v in abi.get("actual", {}).items() if "." not in k}
        print("PASS ABI", sizes)
        print("  derived per-level", abi.get("derived", {}).get(
            "per_level_array_counts"))
    else:
        print("FAIL ABI")
        for error in abi.get("errors", []):
            print("  " + error)

    print("== D8F18H real-packet fixture provenance ==")
    d8_packet = d8f18h_packet_provenance()
    report["tests"]["d8f18h_packet"] = d8_packet
    if d8_packet.get("ok"):
        print(f"PASS D8F18H packet {d8_packet['package_sha256'][:16]} "
              f"TLS={d8_packet['tls_bytes']} size={d8_packet['package_size']}")
    else:
        print("FAIL D8F18H packet provenance")
        for error in d8_packet.get("errors", []):
            print("  " + error)

    print("== TGP2 header/manifest ==")
    tgp2 = compile_and_run(
        "tgp2_contract_test",
        [str(helper("tgp2_contract_test.c"))],
        [],
    )
    report["tests"]["tgp2"] = tgp2
    if tgp2["run"]:
        print(tgp2["run"]["stdout"].strip() or tgp2["run"]["stderr"].strip())
    else:
        print("FAIL TGP2 build")
        print((tgp2.get("build") or {}).get("stderr", ""))

    print("== n2mb v2 complete job + fake JM ==")
    job = compile_and_run("n2mb_v2_job_test", JOB_SOURCES, [])
    report["tests"]["job"] = job
    if job["run"]:
        print(job["run"]["stdout"].strip())
        if job["run"]["stderr"].strip():
            print(job["run"]["stderr"].strip())
    else:
        print("FAIL job build")
        print((job.get("build") or {}).get("stderr", ""))

    if not args.skip_sanitize:
        print("== sanitizer pass (TGP2 + job) ==")
        report["tests"]["tgp2_asan"] = compile_and_run(
            "tgp2_contract_test", [str(helper("tgp2_contract_test.c"))], [],
            sanitize=True,
        )
        report["tests"]["job_asan"] = compile_and_run(
            "n2mb_v2_job_test", JOB_SOURCES, [], sanitize=True,
        )
        report["tests"]["draw_v2_asan"] = compile_and_run(
            "n2mb_draw_v2_test", DRAW_SOURCES, [], sanitize=True,
        )
        report["tests"]["format83_asan"] = compile_and_run(
            "format83_vertex_fetch_test", V83_SOURCES, [], sanitize=True,
        )
        for key in ("tgp2_asan", "job_asan", "draw_v2_asan", "format83_asan"):
            result = report["tests"][key]
            status = "PASS" if result["ok"] else "FAIL"
            print(f"{status} {key}")
            if result["run"] and result["run"]["stderr"].strip():
                print(result["run"]["stderr"].strip()[:2000])

    print("== n2mb_draw_v2 sidecar retention ==")
    draw = compile_and_run("n2mb_draw_v2_test", DRAW_SOURCES, [])
    report["tests"]["draw_v2"] = draw
    if draw["run"]:
        print(draw["run"]["stdout"].strip())
        if draw["run"]["stderr"].strip():
            print(draw["run"]["stderr"].strip())
    else:
        print("FAIL draw_v2 build")
        print((draw.get("build") or {}).get("stderr", ""))

    print("== VkFormat83 vertex fetch ==")
    v83 = compile_and_run("format83_vertex_fetch_test", V83_SOURCES, [])
    report["tests"]["format83"] = v83
    if v83["run"]:
        print(v83["run"]["stdout"].strip())
        if v83["run"]["stderr"].strip():
            print(v83["run"]["stderr"].strip())
    else:
        print("FAIL format83 build")
        print((v83.get("build") or {}).get("stderr", ""))

    print("== corpus translate_graphics SIGSEGV regressions ==")
    corpus = corpus_crash_regression()
    report["tests"]["corpus_graphics"] = corpus
    for row in corpus["cases"]:
        print(f"{'PASS' if row['passed'] else 'FAIL'} {row['case']} "
              f"status={row.get('status')} rc={row['returncode']} "
              f"error={row.get('error')}")
    if corpus.get("error"):
        print("  " + corpus["error"])

    print("== render specialized per-draw variant cache ==")
    render_cache = render_cache_regression()
    report["tests"]["render_cache"] = render_cache
    if render_cache.get("ok"):
        print("PASS render nearest/linear variant cache harness")
    else:
        print(f"FAIL render variant cache rc={render_cache.get('returncode')} "
              f"error={render_cache.get('error')}")
        text = (render_cache.get("stdout") or "") + (render_cache.get("stderr") or "")
        print("\n".join(text.splitlines()[-8:]))

    print("== legacy sampler word guard ==")
    legacy = legacy_sampler_guard_test()
    report["tests"]["legacy_sampler"] = legacy
    if legacy["ok"]:
        print("PASS legacy sampler guard refuses every inexpressible field")
    else:
        print(f"FAIL legacy sampler guard rc={legacy['returncode']}")
        print("\n".join(((legacy.get("stdout") or "") + (legacy.get("stderr") or "")).splitlines()[-8:]))

    print("== inert render pass encoder safety (C3) ==")
    inert = inert_render_pass_test()
    report["tests"]["inert_render_pass"] = inert
    if inert.get("ok"):
        print(f"PASS inert render pass {inert['summary']['checks']} checks, "
              "16 bounded refusal diagnostics, release+ASan")
    else:
        print("FAIL inert render pass")
        for error in inert.get("errors", []):
            print("  " + error)

    print("== CoreAnimation-shaped render pass (independent) ==")
    coreanimation = coreanimation_render_pass_test()
    report["tests"]["coreanimation_render_pass"] = coreanimation
    if coreanimation.get("ok"):
        print(f"PASS CoreAnimation pass {coreanimation['summary']['checks']} checks, "
              "release+ASan")
    else:
        print("FAIL CoreAnimation pass")
        for error in coreanimation.get("errors", []):
            print("  " + error)

    print("== native-macro compute frontend ==")
    native_compute = native_compute_frontend_test()
    report["tests"]["native_compute"] = native_compute
    if native_compute["ok"]:
        print("PASS buffer-only TDG1 and used+unused-texture TDG2 through real bridge")
    else:
        print(f"FAIL native compute frontend rc={native_compute['returncode']}")
        print("\n".join(((native_compute.get("stdout") or "") + (native_compute.get("stderr") or "")).splitlines()[-12:]))

    print("== ASan two-thread specialized variant cache ==")
    variant_threads = render_variant_threads_test()
    report["tests"]["variant_threads"] = variant_threads
    if variant_threads["ok"]:
        print("PASS variant growth/read under ASan two threads")
    else:
        print(f"FAIL variant threads rc={variant_threads['returncode']}")
        print("\n".join(((variant_threads.get("stdout") or "") + (variant_threads.get("stderr") or "")).splitlines()[-10:]))

    print("== real Rust planner + real AIR ==")
    producer = producer_test()
    report["tests"]["producer"] = producer
    if producer.get("checks"):
        for check in producer["checks"]:
            if not check["ok"]:
                print(f"FAIL {check['name']}: {check['detail']}")
        print(f"{sum(1 for c in producer['checks'] if c['ok'])}/{len(producer['checks'])} producer checks pass")
    elif producer.get("error"):
        print("FAIL producer: " + producer["error"])
    else:
        print("FAIL producer: no checks executed")
        print(producer.get("stderr", ""))

    checks = 0
    required = 0
    pending = 0
    required_labels: set[str] = set()
    pending_labels: set[str] = set()
    for key in ("tgp2", "tgp2_asan", "job", "job_asan", "draw_v2",
                "draw_v2_asan", "format83", "format83_asan",
                "inert_render_pass", "coreanimation_render_pass"):
        result = report["tests"].get(key)
        if not result:
            continue
        c, r, p = summarize_checks(result)
        checks += c
        required += r
        pending += p
        text = ""
        if result.get("run"):
            text = result["run"]["stdout"] + result["run"]["stderr"]
        for line in text.splitlines():
            if line.startswith("FAIL [required]"):
                required_labels.add(line)
            elif line.startswith("FAIL [pending]"):
                pending_labels.add(line)
    checks += 1
    if not d8_packet.get("ok"):
        required += 1
        required_labels.add("D8F18H real-packet fixture provenance")
    if not abi.get("ok"):
        required += 1
        required_labels.add("ABI oracle")
    if not producer.get("ok"):
        required += 1
        required_labels.add("real Rust producer")
    if candidate is not None and not candidate["ok"]:
        required += 1
        required_labels.add("candidate provenance gate")
    if not corpus.get("ok"):
        required += 1
        required_labels.add("corpus translate_graphics crash regression")
    if not render_cache.get("ok"):
        required += 1
        required_labels.add("render specialized variant cache")
    if not legacy.get("ok"):
        required += 1
        required_labels.add("legacy sampler word guard")
    if not inert.get("ok"):
        required += 1
        required_labels.add("inert render pass encoder safety")
    if not coreanimation.get("ok"):
        required += 1
        required_labels.add("CoreAnimation render pass acceptance")
    if not native_compute.get("ok"):
        required += 1
        required_labels.add("native compute frontend")
    if not variant_threads.get("ok"):
        required += 1
        required_labels.add("ASan two-thread variant cache")

    report["summary"] = {
        "checks": checks,
        "required_failures": required,
        "pending_failures": pending,
        "required_distinct": len(required_labels),
        "pending_distinct": len(pending_labels),
        "required_labels": sorted(required_labels),
        "pending_labels": sorted(pending_labels),
        "abi_ok": bool(abi.get("ok")),
        "d8f18h_packet_ok": bool(d8_packet.get("ok")),
        "producer_ok": bool(producer.get("ok")),
        "candidate_ok": None if candidate is None else bool(candidate["ok"]),
        "corpus_graphics_ok": bool(corpus.get("ok")),
        "render_cache_ok": bool(render_cache.get("ok")),
        "legacy_sampler_ok": bool(legacy.get("ok")),
        "inert_render_pass_ok": bool(inert.get("ok")),
        "coreanimation_render_pass_ok": bool(coreanimation.get("ok")),
        "native_compute_ok": bool(native_compute.get("ok")),
        "variant_threads_ok": bool(variant_threads.get("ok")),
    }
    report["ok"] = required == 0 and (pending == 0 or args.allow_pending)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(report, indent=2) + "\n")

    print("== summary ==")
    print(f"checks={checks} required_failures={required} pending_failures={pending} "
          f"distinct_required={report['summary']['required_distinct']} "
          f"distinct_pending={report['summary']['pending_distinct']} "
          f"abi_ok={abi.get('ok')} producer_ok={producer.get('ok')} "
          f"candidate_ok={report['summary']['candidate_ok']} "
          f"corpus_graphics_ok={report['summary']['corpus_graphics_ok']} "
          f"render_cache_ok={report['summary']['render_cache_ok']} "
          f"legacy_sampler_ok={report['summary']['legacy_sampler_ok']} "
          f"coreanimation_render_pass_ok={report['summary']['coreanimation_render_pass_ok']} "
          f"native_compute_ok={report['summary']['native_compute_ok']} "
          f"variant_threads_ok={report['summary']['variant_threads_ok']}")
    for label in report["summary"]["required_labels"]:
        print("  required: " + label)
    print(f"report: {args.json}")
    print("NOT hardware verified: no GPU pixels, no phone, no native build")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
