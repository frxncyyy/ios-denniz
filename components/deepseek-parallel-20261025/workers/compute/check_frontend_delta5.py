#!/usr/bin/env python3
"""Host-only delta5 compute frontend tests. No cargo, phone, build lease or GPU."""
import os
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GPU = HERE.parents[3]
OUT = HERE / "runtime"
OUT.mkdir(exist_ok=True)
bridge = HERE / "cargo-target/release/libtegu_air_bridge.so"
mali = GPU / "build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so"
if not bridge.exists():
    raise SystemExit(f"missing producer bridge: {bridge}")
if not mali.exists():
    mali = GPU / "build/ui-corpus-20261025/baseline/libnord2_mali_runtime.so"
if not mali.exists():
    raise SystemExit("missing Mali runtime")

def bridge_has_delta11_symbols():
    nm = subprocess.run(["nm", "-D", str(bridge)], capture_output=True, text=True)
    return "tegu_air_pipeline_native_dispatch_tg" in nm.stdout and \
        "tegu_air_pipeline_native_dispatch_v2_tg" in nm.stdout

def run(name, extra, sanitize=False):
    with tempfile.TemporaryDirectory(prefix="tegu-"+name+"-") as td:
        exe = Path(td) / name
        sources = [str(HERE / "tests/check_frontend_delta5.m")]
        if not bridge_has_delta11_symbols():
            sources.append(str(HERE / "tests/planner_stubs.c"))
        sanitizer = ["-fsanitize=address,undefined"] if sanitize else []
        cmd = [
            "clang", "-fblocks", "-pthread", "-O1", "-g", "-Wall", "-Wextra",
            "-Werror", *sanitizer,
            "-fconstant-string-class=NXConstantString",
            "-DTEGU_AIR_BRIDGE", "-DTEGU_MALI_BRIDGE", "-DTEGU_METAL_BASE",
            *extra,
            "-I" + str(GPU / "metal/tegu"),
            "-I" + str(GPU / "build/tegu-frontend"),
            *sources,
            str(bridge), str(mali), "-lobjc", "-lBlocksRuntime", "-ldl", "-o", str(exe),
        ]
        subprocess.run(cmd, check=True)
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = os.pathsep.join([str(bridge.parent), str(mali.parent)])
        if sanitize:
            env["ASAN_OPTIONS"] = "detect_leaks=0"
        env["TEGU_PIPELINE_CACHE_DIR"] = str(Path(td) / "cache")
        Path(env["TEGU_PIPELINE_CACHE_DIR"]).mkdir()
        result = subprocess.run([str(exe)], check=True, capture_output=True,
                                text=True, env=env, timeout=120)
        text = result.stdout + result.stderr
        (OUT / (name + ".log")).write_text(text)
        lines = text.splitlines()
        assert "PASS frontend delta5: bounded NOTREADY retry, compositor gate states, pipeline-library refusal" in lines
        assert "TEGU_COMPUTE_REFUSED" not in text
        assert text.count("TEGU_SELECTOR_REFUSED") == 4
        assert "TEGU_INERT_COMPUTE_DISPATCH" in text
        if name.startswith("delta11_") and "inert" in name:
            assert "TEGU_INERT_COMPUTE_PIPELINE" in text
        else:
            assert "TEGU_INERT_COMPUTE_PIPELINE" not in text
        assert text.count("TEGU_COMPOSITOR_GATE enabled=0") >= 1
        assert text.count("TEGU_COMPOSITOR_GATE enabled=1") >= 1
        devq=[line for line in lines if line.startswith("TEGU_DEVQ ")]
        assert len(devq)==64, f"TEGU_DEVQ trace is not capped at 64: {len(devq)}"
        for selector in ("supportsFeatureSet","supportsFamily","supportsTextureSampleCount",
                         "isDepth24Stencil8PixelFormatSupported","supportsSampleCount"):
            assert any(f"sel={selector} " in line for line in devq), selector
        assert "TEGU_DEVQ sel=supportsFeatureSet arg=0 ret=0" in devq
        print(f"PASS {name}: bounded retry/gate/pipeline-library/MTLFeatureSet/DEVQ")
        print(f"log={OUT/(name + '.log')}")

# Release first (thermal-limited; no sanitizer), then one sanitizer pass at the
# end.  The test itself is identical; only codegen/sanitizer state differs.
run("delta10_default_off", [])
run("delta10_default_on", ["-DTEGU_COMPOSITOR_DEFAULT_ON"])
run("delta11_inert_on", ["-DTEGU_INERT_PIPELINES"])
run("delta11_compositor_inert", ["-DTEGU_INERT_PIPELINES", "-DTEGU_COMPOSITOR_DEFAULT_ON"])
run("delta11_inert_asan", ["-DTEGU_INERT_PIPELINES"], sanitize=True)
