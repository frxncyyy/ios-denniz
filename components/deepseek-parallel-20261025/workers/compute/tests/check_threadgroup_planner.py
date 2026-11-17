#!/usr/bin/env python3
"""Host-only DELTA11 dynamic-WLS bridge test.

Plans the real QuartzCore compute_apl AIR with setThreadgroupMemoryLength
index0=1024 through the rebuilt *_tg planner, verifies the per-region TgLength
constant, and compiles that SPIR-V through the real TGP2 compiler to assert
header word18 (WLS) is exactly 1024. No GPU execution, phone or publication.
"""
import ctypes as C
import hashlib
import importlib.util
import json
import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GPU = HERE.parents[4]
WORK = HERE.parents[5]
INTEGRATION = WORK / "gpu/build/deepseek-parallel-20261025/workers/integration_tests"
sys.path.insert(0, str(INTEGRATION))
sys.path.insert(0, str(GPU / "metal/tegu"))
import producer_plan_test as p  # noqa: E402
import abi_oracle  # noqa: E402
import check_compute_v2_compiler as compiler_mod  # noqa: E402


def main():
    air = GPU / "build/ui-corpus-20261025/air/15854dc200247469278849df9a285688278a7cfc05a13ca0af22d7be4c712576.air"
    if not air.exists():
        raise SystemExit(f"missing local QuartzCore compute_apl AIR: {air}")
    library = HERE.parent / "cargo-target/release/libtegu_air_bridge.so"
    if not library.exists():
        raise SystemExit(f"missing rebuilt bridge: {library}")
    profile = HERE.parent / "air-work/fixtures/profile.json"
    if not profile.exists():
        raise SystemExit(f"missing profile: {profile}")
    compiler = GPU / "build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so"
    if not compiler.exists():
        raise SystemExit(f"missing compiler: {compiler}")
    with tempfile.TemporaryDirectory(prefix="tegu-tg-") as td:
        fx = Path(td); shutil.copy2(air, fx / "reduce_buffers_nd_mode_argmax_f16.air"); shutil.copy2(profile, fx / "profile.json")
        structs = abi_oracle.parse_rust(GPU / "metal/tegu/air_bridge.rs")
        built = p.build_ctypes(structs)
        p.built = built
        R = built["ComputeResource"]
        lib = C.CDLL(str(library))
        lib.tegu_air_pipeline_native_dispatch_v2_tg.restype = C.c_int32
        lib.tegu_air_pipeline_native_dispatch_v2_tg.argtypes = [
            C.c_void_p, C.POINTER(R), C.c_size_t, C.POINTER(C.c_uint32), C.c_uint32,
            C.POINTER(C.c_uint32), C.POINTER(C.c_uint32), C.POINTER(p.Blob),
            C.c_void_p, C.c_char_p, C.c_size_t]
        _, lib_handle, pipeline, base_spirv = p.open_pipeline(lib, fx, "reduce_buffers_nd_mode_argmax_f16.air", "reduce_buffers_nd_mode_argmax_f16")
        resources = [
            p.buffer(R, 0, p.READ, length=4096),
            p.buffer(R, 1, p.WRITE, length=4096),
            p.buffer(R, 2, p.READ, length=4096),
            p.buffer(R, 4, p.READ, length=4096),
        ]
        array = (R * len(resources))(*resources)
        lengths = (C.c_uint32 * 31)(); lengths[0] = 1024; lengths[1] = 256
        extents = (C.c_uint32 * 3)(1, 1, 1); local = (C.c_uint32 * 3)(1, 1, 1)
        output = C.create_string_buffer(C.sizeof(built["NativeDispatchV2"]))
        variant = p.Blob(); error = C.create_string_buffer(1024)
        rc = lib.tegu_air_pipeline_native_dispatch_v2_tg(
            pipeline, array, len(resources), lengths, 0, extents, local,
            C.byref(variant), output, error, len(error))
        assert rc == 0, f"planner refused: {error.value!r}"
        plan = C.cast(output, C.POINTER(built["NativeDispatchV2"])).contents
        assert plan.region_count >= 1
        tg = []
        for ri in range(plan.region_count):
            region = plan.regions[ri]
            constants = [(region.constants[k].id, bytes(region.constants[k].data)[:region.constants[k].size])
                         for k in range(region.constant_count)]
            entry0 = next((v for i, v in constants if i == 4), None)
            entry1 = next((v for i, v in constants if i == 5), None)
            assert entry0 is not None and entry1 is not None, f"region {ri} lacks TgLength SpecIds4/5"
            assert int.from_bytes(entry0, "little") == 1024, entry0.hex()
            assert int.from_bytes(entry1, "little") == 256, entry1.hex()
            tg.extend([entry0, entry1])
        spv = variant if variant.length else base_spirv
        spv_path = fx / "reduce_buffers_nd_mode_argmax_f16.spv"; spv_path.write_bytes(C.string_at(spv.bytes, spv.length))
        spec = [(i, v.hex()) for i, v in constants]
        result = compiler_mod.invoke(compiler, spv_path, fx, spec, packet=fx / "reduce_buffers_nd_mode_argmax_f16.tgp2")
        assert result.get("rc") == 0, result
        assert result.get("wls_bytes") == 1280, result.get("wls_bytes")
        assert result.get("tls_bytes") == 0, result.get("tls_bytes")
        # The same planner must refuse a malformed Metal length before emitting
        # a packet (the frontend also rejects it; defense in depth).
        lengths[0] = 17; variant = p.Blob(); output2 = C.create_string_buffer(C.sizeof(built["NativeDispatchV2"]))
        rc2 = lib.tegu_air_pipeline_native_dispatch_v2_tg(
            pipeline, array, len(resources), lengths, 0, extents, local,
            C.byref(variant), output2, error, len(error))
        assert rc2 != 0, "malformed 17-byte threadgroup length was not refused"
        print(json.dumps({
            "planner_rc": rc, "regions": plan.region_count,
            "tg_length_hex": [v.hex() for v in tg],
            "compiler_rc": result.get("rc"), "wls_bytes": result.get("wls_bytes"),
            "tls_bytes": result.get("tls_bytes"),
            "bridge_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
            "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
            "air_sha256": hashlib.sha256(air.read_bytes()).hexdigest(),
            "gpu_executed": False,
        }, sort_keys=True))
        lib.tegu_air_pipeline_close(pipeline); lib.tegu_air_library_close(lib_handle)


if __name__ == "__main__":
    main()
