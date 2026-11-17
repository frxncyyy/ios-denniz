#!/usr/bin/env python3
"""Legacy sampler-word guard regression.

Builds the production sampler state helper into the render host frontend
prefix, then calls tegu_sampler_legacy_word_ok directly for every field the old
two-bit word cannot express.  No GPU/device work.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[4]
GPU = WORK / "gpu"
RENDER = WORK / "gpu/build/deepseek-parallel-20261025/workers/render"
HARNESS = RENDER / "vs_texture_host_test.m"
OUT = HERE / "build/legacy_sampler_guard_test"


def main() -> int:
    if not HARNESS.exists():
        print(f"FAIL missing generated render harness {HARNESS}; run render host test first")
        return 2
    text = HARNESS.read_text()
    marker = "int main(int argc,char **argv){"
    if marker not in text:
        print("FAIL render harness main marker missing")
        return 2
    prefix = text.split(marker, 1)[0]
    body = r'''
int main(void){
 TeguSampler *s=[[TeguSampler alloc] init];
 struct n2t_sampler_state base;
 memset(&base,0,sizeof(base));base.version=N2T_TEXTURE_RESOURCES_VERSION;
 base.min_filter=N2T_FILTER_NEAREST;base.mag_filter=N2T_FILTER_NEAREST;
 base.mip_filter=N2T_MIP_NOT_MIPMAPPED;
 base.address_s=base.address_t=base.address_r=N2T_ADDRESS_CLAMP_EDGE;
 base.normalized=1;base.compare_enabled=0;base.compare_function=N2T_COMPARE_NEVER;
 base.max_anisotropy=1;base.reduction=N2T_REDUCTION_AVERAGE;
 base.lod_algorithm=N2T_LOD_ISOTROPIC;base.clamp_integer_coords=0;
 base.clamp_integer_array=1;base.srgb_override=0;base.seamless_cube=1;
 base.magnify_cutoff=0;base.border_color_kind=N2T_BORDER_TRANSPARENT_BLACK;
 base.lod_min=0.0f;base.lod_max=0.0f;base.lod_bias=0.0f;
 s->state=base;
 assert(tegu_sampler_legacy_word_ok(s)==1);
 assert(tegu_sampler_legacy_word_ok(0)==0);
#define REJECT(field,value) do { s->state=base; s->state.field=(value); \
 assert(tegu_sampler_legacy_word_ok(s)==0); } while(0)
 REJECT(min_filter,N2T_FILTER_LINEAR+1);
 REJECT(mag_filter,N2T_FILTER_LINEAR+1);
 REJECT(mip_filter,N2T_MIP_NEAREST);
 REJECT(address_s,N2T_ADDRESS_REPEAT);
 REJECT(address_t,N2T_ADDRESS_REPEAT);
 REJECT(address_r,N2T_ADDRESS_REPEAT);
 REJECT(normalized,0);
 REJECT(compare_enabled,1);
 REJECT(compare_function,N2T_COMPARE_LESS);
 REJECT(max_anisotropy,2);
 REJECT(reduction,N2T_REDUCTION_MIN);
 REJECT(lod_algorithm,N2T_LOD_ANISOTROPIC);
 REJECT(clamp_integer_coords,1);
 REJECT(clamp_integer_array,0);
 REJECT(srgb_override,1);
 REJECT(seamless_cube,0);
 REJECT(magnify_cutoff,1);
 REJECT(border_color_kind,N2T_BORDER_OPAQUE_WHITE);
 REJECT(lod_min,1.0f);
 REJECT(lod_max,1.0f);
 REJECT(lod_bias,0.5f);
#undef REJECT
 [s release];
 puts("PASS legacy sampler word guard: accepted exact state, refused every inexpressible field");
 return 0;
}
'''
    source = OUT.with_suffix(".m")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    runtime = (GPU / 'metal/tegu/runtime_frontend_host_test.m').read_text()
    helper = runtime.split('NSUInteger tegu_host_constant_data_size(NSUInteger type)',1)[1]
    source.write_text(prefix + 'NSUInteger tegu_host_constant_data_size(NSUInteger type)' + helper.split('static ',1)[0] + body)
    libs = [
        GPU / "build/deepseek-parallel-20261025/workers/compute/cargo-target/release",
        GPU / "build/mesa-build/src/panfrost/clc",
    ]
    cmd = [
        "clang", "-fblocks", "-pthread", "-O1", "-g",
        "-Wno-incompatible-pointer-types", "-Wno-incompatible-function-pointer-types",
        "-fconstant-string-class=NXConstantString",
        "-D" + "TEGU_AIR_BRIDGE", "-D" + "TEGU_MALI_BRIDGE",
        "-D" + "TEGU_GRAPHICS_BRIDGE", "-D" + "NORD2_JM_TRANSPORT",
    ]
    for path in (GPU / "metal/tegu", GPU / "metal", GPU / "include", GPU / "build/tegu-frontend"):
        cmd.append("-I" + str(path))
    cmd += [
        str(source),
        str(GPU / "metal/nord2_metal_bridge.c"),
        str(GPU / "metal/nord2_metal_render.c"),
        str(GPU / "src/nord2_gpu_desc.c"),
        str(GPU / "src/nord2_gpu_jm.c"),
        "-lobjc", "-ldl",
    ]
    for path in libs:
        cmd += ["-L" + str(path)]
    cmd += ["-ltegu_air_bridge", "-lnord2_mali_runtime", "-o", str(OUT)]
    built = subprocess.run(cmd, capture_output=True, text=True)
    if built.returncode:
        print(built.stderr)
        return 1
    env = dict(os.environ, LD_LIBRARY_PATH=":".join(map(str, libs)),
               ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    run = subprocess.run([str(OUT)], capture_output=True, text=True, env=env)
    print(run.stdout.strip())
    if run.returncode:
        print(run.stderr.strip())
    return run.returncode


if __name__ == "__main__":
    sys.exit(main())
