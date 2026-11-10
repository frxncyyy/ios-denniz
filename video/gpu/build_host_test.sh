#!/bin/bash
# WP08 GPU host test: gpu/air_video.c's passes on the host Mesa softpipe rasteriser,
# compared with kern/n2vk_ref.c bit for bit, and every shader through Panfrost's Valhall
# compiler.  A native Mesa build is made under gpu/video/build/WP08/hostmesa from the
# shared, READ-ONLY source tree gpu/build/y2s-stage0/mesa-25.0.7 (nothing in that tree or
# in gpu/build/y2s4x is written).  Everything else goes to gpu/video/build/WP08/.
#
#   bash gpu/video/gpu/build_host_test.sh [outdir]
#
# Last line on success: N2TEST WP08-GPU PASS <n>/<n>
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
VIDEO=$(cd "$HERE/.." && pwd)
WORK=$(cd "$VIDEO/../.." && pwd)
OUT=${1:-$VIDEO/build/WP08}
CC=${CC:-cc}
CXX=${CXX:-c++}
SRC=$OUT/src
HOST=$OUT/hostmesa
MESA=$WORK/gpu/build/y2s-stage0/mesa-25.0.7
mkdir -p "$OUT/tmp" "$OUT/logs" "$SRC"

if [ ! -d "$MESA" ]; then
	echo "N2TEST WP08-GPU FAIL the Mesa source tree $MESA is missing" >&2
	exit 1
fi

# The hostfix headers are host-tests.sh's, for glibc >= 2.41's once_flag and the two
# headers a softpipe-only build does not include for itself.
printf '#ifndef __ASSEMBLER__\n#include <pthread.h>\n#define __once_flag_defined 1\n#define once_flag pthread_once_t\n#endif\n' > "$OUT/tmp/hostfix.h"
printf '#include <stdint.h>\n#include <stdbool.h>\n#include "frontend/winsys_handle.h"\n#include "drm-uapi/drm_fourcc.h"\n' > "$OUT/tmp/hostfix2.h"

# A native softpipe + Panfrost-compiler build, from the shared source, in our own directory.
if [ ! -f "$HOST/build.ninja" ]; then
	echo "== WP08: setting up the host Mesa build (read-only source tree) =="
	export PATH=$WORK/gpu/build/venv/bin:$PATH
	meson setup "$HOST" "$MESA" --wrap-mode=nofallback --buildtype=debugoptimized -Db_ndebug=false \
		-Dc_args="-g0 -include $OUT/tmp/hostfix.h" -Dcpp_args="-g0 -include $OUT/tmp/hostfix.h" \
		-Dplatforms= -Dgallium-drivers=softpipe,panfrost -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
		-Dllvm=disabled -Dshared-glapi=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true -Dzstd=disabled \
		-Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled -Dvalgrind=disabled -Dlibunwind=disabled \
		-Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled -Dgallium-va=disabled -Dgallium-xa=disabled \
		-Dlmsensors=disabled -Dbuild-tests=false > "$OUT/logs/meson-setup.log" 2>&1
fi
export PATH=$WORK/gpu/build/venv/bin:$PATH
ninja -C "$HOST" -j2 \
	src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a \
	src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a \
	src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a \
	src/gallium/winsys/sw/null/libws_null.a src/compiler/air/libair.a src/util/libxmlconfig.a \
	src/gallium/drivers/softpipe/libsoftpipe.a src/panfrost/compiler/libpanfrost_bifrost.a \
	src/panfrost/compiler/libpanfrost_bifrost_disasm.a src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a \
	src/panfrost/lib/libpanfrost_lib.a src/panfrost/util/libpanfrost_util.a \
	src/panfrost/shared/libpanfrost_shared.a > "$OUT/logs/ninja-hostmesa.log" 2>&1

# air_metal.c is READ ONLY in y2s46-vt6; a copy, with the include of air_video.c the lead
# will add to the library's own copy, is what the host test compiles.
cp "$WORK/gpu/build/y2s46-vt6/src/air_metal.c" "$SRC/air_metal.c"
cp "$WORK/gpu/build/y2s46-vt6/src/air_metal.h" "$SRC/air_metal.h"
cp "$WORK/gpu/build/y2s46-vt6/src/air_isp.c" "$SRC/air_isp.c"
cp "$WORK/gpu/build/y2s46-vt6/src/rwa_isp_api.h" "$SRC/rwa_isp_api.h"

INC="-I$SRC -I$VIDEO/gpu -I$VIDEO/include"
for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary \
         src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler \
         src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do
	INC="$INC -I$HOST/$d -I$MESA/$d"
done
DEF="-D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -DHAVE_SOFTPIPE -DGALLIUM_SOFTPIPE \
-DHAVE_SWRAST -DHAVE_PTHREAD -DHAVE_STRUCT_TIMESPEC -DHAVE_ENDIAN_H -DHAVE_DLFCN_H -DHAVE_SECURE_GETENV \
-DHAVE___BUILTIN_BSWAP32 -DHAVE___BUILTIN_BSWAP64 -DHAVE___BUILTIN_CLZ -DHAVE___BUILTIN_CLZLL -DHAVE___BUILTIN_CTZ \
-DHAVE___BUILTIN_EXPECT -DHAVE___BUILTIN_FFS -DHAVE___BUILTIN_FFSLL -DHAVE___BUILTIN_POPCOUNT \
-DHAVE___BUILTIN_POPCOUNTLL -DHAVE___BUILTIN_UNREACHABLE -DHAVE___BUILTIN_TYPES_COMPATIBLE_P \
-DHAVE_FUNC_ATTRIBUTE_CONST -DHAVE_FUNC_ATTRIBUTE_FLATTEN -DHAVE_FUNC_ATTRIBUTE_MALLOC -DHAVE_FUNC_ATTRIBUTE_PURE \
-DHAVE_FUNC_ATTRIBUTE_UNUSED -DHAVE_FUNC_ATTRIBUTE_WARN_UNUSED_RESULT -DHAVE_FUNC_ATTRIBUTE_WEAK \
-DHAVE_FUNC_ATTRIBUTE_FORMAT -DHAVE_FUNC_ATTRIBUTE_PACKED -DHAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL \
-DHAVE_FUNC_ATTRIBUTE_NORETURN -DHAVE_FUNC_ATTRIBUTE_VISIBILITY -DHAVE_UINT128 -DUSE_GCC_ATOMIC_BUILTINS \
-DHAVE_TIMESPEC_GET -DMESA_DEBUG=0 -DLLVM_AVAILABLE=0 -DDRAW_LLVM_AVAILABLE=0"
CFLAGS=(-std=gnu11 -O2 -g -w -include "$OUT/tmp/hostfix.h" -include "$OUT/tmp/hostfix2.h" $DEF $INC
        -I"$SRC" -I"$VIDEO/gpu" -I"$VIDEO/include")

echo "== WP08-GPU: build =="
"$CC" "${CFLAGS[@]}" -c "$VIDEO/gpu/vid_gpu_host_test.c" -o "$OUT/tmp/vid_gpu_host_test.o"
"$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -I "$VIDEO/include" -c "$VIDEO/kern/n2vk_ref.c" -o "$OUT/tmp/n2vk_ref.o"
"$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -I "$VIDEO/include" -c "$VIDEO/kern/n2vk_dispatch.c" -o "$OUT/tmp/n2vk_dispatch.o"

LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a \
src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a \
src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/compiler/air/libair.a \
src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
PAN="src/panfrost/compiler/libpanfrost_bifrost.a src/panfrost/compiler/libpanfrost_bifrost_disasm.a \
src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a src/panfrost/lib/libpanfrost_lib.a \
src/panfrost/util/libpanfrost_util.a src/panfrost/shared/libpanfrost_shared.a"
A=""
for l in $LIBS $PAN; do
	A="$A $HOST/$l"
done
"$CXX" -o "$OUT/vid_gpu_host_test" "$OUT/tmp/vid_gpu_host_test.o" "$OUT/tmp/n2vk_ref.o" "$OUT/tmp/n2vk_dispatch.o" \
	-Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm

echo "== WP08-GPU: run =="
set +e
RWA_SOFTWARE=1 "$OUT/vid_gpu_host_test" 2>&1 | tee "$OUT/vid_gpu_host_test.log"
RC=${PIPESTATUS[0]}
set -e
LINE=$(tail -1 "$OUT/vid_gpu_host_test.log")
if [ "$RC" -ne 0 ] || [[ "$LINE" != *" PASS "* ]]; then
	echo "N2TEST WP08-GPU FAIL ${LINE:-no output}"
	exit 1
fi
echo "$LINE"
