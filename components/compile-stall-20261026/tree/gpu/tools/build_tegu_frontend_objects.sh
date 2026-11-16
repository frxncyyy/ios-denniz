#!/usr/bin/env bash
# Compile the complete reused Objective-C frontend and the new JM compute
# adapter for iOS ARM64. link_metal_bundle.py performs the strict final link.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tegu-frontend
python3 tools/generate_mesa_pack_subset.py
bash tools/generate_preload.sh
python3 - <<'PY'
from pathlib import Path
import runpy,hashlib,json,os
helpers=runpy.run_path('metal/tegu/build_compute_bundle.py')
profile=Path('build/metal2vulkan/crates/metal2vulkan-validation/profiles/nvidia-rtx5080-laptop.json').read_bytes()
profile=helpers['astc_ldr_profile'](helpers['framebuffer_fetch_profile'](profile))
# Cache identity includes both native compilers, frontend and descriptor sources.
# NORD2_PIPELINE_CACHE_SCOPE=compilers narrows it to what can change a cached
# result: the two compiler archives, every header (struct layouts of the stored
# plans) and the cache implementation/key sources (pipeline_cache.c,
# pipeline_cache_frontend.inc, library_frontend.inc).  Every other input of a
# compile (shader bytes, constants, formats, blends, profile, sizes) is already
# in the entry key, so render/encoder-only edits then keep the on-device disk
# cache and any pack valid across builds.  Default stays the whole-source hash.
h=hashlib.sha256()
scope=os.environ.get('NORD2_PIPELINE_CACHE_SCOPE','all')
archives=[Path('build/native/libNord2Mali.a'),Path('../research/nord2-native-air/target/aarch64-apple-ios/release/libtegu_air_bridge.a')]
if scope=='compilers':
    h.update(b'scope=compilers\0')
    sources=[*Path('metal').rglob('*.h'),Path('metal/tegu/pipeline_cache.c'),Path('metal/tegu/pipeline_cache_frontend.inc'),Path('metal/tegu/library_frontend.inc')]
elif scope=='all':
    sources=[*Path('metal').rglob('*.c'),*Path('metal').rglob('*.inc'),*Path('metal').rglob('*.m'),*Path('metal').rglob('*.h')]
else:
    raise SystemExit('NORD2_PIPELINE_CACHE_SCOPE must be all or compilers')
for p in sorted([*sources,*archives]):
    h.update(p.name.encode());h.update(hashlib.sha256(p.read_bytes()).digest())
Path('build/tegu-frontend/build-identity.h').write_text('#define TEGU_PIPELINE_CACHE_BUILD '+json.dumps('nord2-arch9-'+h.hexdigest())+'\n')
# Translator feature ceiling only. Native GPU features remain separately gated.
Path('build/tegu-frontend/tegu-air-profile.h').write_text(
    'static const unsigned char tegu_native_profile[]={'+','.join(map(str,profile))+'};\n')
PY
clang -fblocks --target=arm64-apple-ios16.0 -ffreestanding -O2 \
 -Wall -Wextra -Werror -fno-stack-protector -fno-objc-arc \
 -DNORD2_JM_TRANSPORT -DTEGU_AIR_BRIDGE -DTEGU_MALI_BRIDGE -DTEGU_METAL_BASE \
 -DTEGU_GRAPHICS_BRIDGE -DTEGU_METAL_FEATURE_INIT -DTEGU_METAL_PROTOCOLS ${NORD2_FRONTEND_DEFINES:-} \
 -include build/tegu-frontend/build-identity.h -Ibuild/tegu-frontend -c metal/tegu/compute_frontend.m \
 -o build/tegu-frontend/compute_frontend.o
clang --target=arm64-apple-ios16.0 -ffreestanding -O2 -Wall -Wextra -Werror \
 -Iinclude -Imetal -Ibuild/tegu-frontend -c metal/nord2_metal_bridge.c -o build/tegu-frontend/nord2_metal_bridge.o
mkdir -p build/tegu-frontend/include
cat > build/tegu-frontend/include/string.h <<'HDR'
#ifndef NORD2_FREESTANDING_STRING_H
#define NORD2_FREESTANDING_STRING_H
#include <stddef.h>
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
#endif
HDR
for name in nord2_gpu_desc nord2_gpu_jm; do
 clang --target=arm64-apple-ios16.0 -ffreestanding -O2 -Wall -Wextra -Werror \
  -Ibuild/tegu-frontend/include -Iinclude -c "src/$name.c" -o "build/tegu-frontend/$name.o"
done
clang --target=arm64-apple-ios16.0 -isysroot "$PWD/build/apple-toolchain/sdks/iPhoneOS16.5.sdk" -O2 -Wall -Wextra -Werror \
 -Imetal -Ibuild/tegu-frontend -c metal/nord2_metal_render.c -o build/tegu-frontend/nord2_metal_render.o
AR_TOOL=${LLVM_AR:-${NORD2_AIR_LLVM_BIN:+$NORD2_AIR_LLVM_BIN/}llvm-ar}
"$AR_TOOL" rcs build/tegu-frontend/libNord2MetalFrontend.a \
 build/tegu-frontend/compute_frontend.o build/tegu-frontend/nord2_metal_bridge.o \
 build/tegu-frontend/nord2_gpu_desc.o build/tegu-frontend/nord2_gpu_jm.o build/tegu-frontend/nord2_metal_render.o
printf '%s\n' 'PASS ARM64 full frontend + Nord2 compute/render transport objects (no device test)'
