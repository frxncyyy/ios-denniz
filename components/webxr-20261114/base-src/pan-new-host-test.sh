#!/bin/bash
# y2s72: (1) the statistics (y2s65) and noise (y2s70) shaders through Panfrost's Valhall compiler for the Mali-G77, on the host, against an
# EXISTING host Mesa build (softpipe + panfrost), READ ONLY: this directory's translator objects and air_metal.c are built in $T.
# (2) isp_warm_host_test: the bounded wait for the shaders and the failure limit (air_isp.c isp_warm, isp_account) on softpipe.
# (3) y2s75: isp_nrg_host_test: the noise's numbers on the GPU (air_isp.c isp_nrg_shader) against the processor's, on softpipe.
#   T=<empty scratch dir> [H=<host mesa build dir>] [TIMEOUT=s] bash pan-new-host-test.sh [shader index]
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; HERE=$(cd "$(dirname "$0")" && pwd); T=${T:?set T to an empty scratch directory}
H=${H:-$G/video/build/WP08/hostmesa}
mkdir -p $T; cd $T
printf '#ifndef __ASSEMBLER__\n#include <pthread.h>\n#define __once_flag_defined 1\n#define once_flag pthread_once_t\n#endif\n' > hostfix.h
printf '#include <stdint.h>\n#include <stdbool.h>\n#include "frontend/winsys_handle.h"\n#include "drm-uapi/drm_fourcc.h"\n' > hostfix2.h
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/compiler/air/libair.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
PAN="src/panfrost/compiler/libpanfrost_bifrost.a src/panfrost/compiler/libpanfrost_bifrost_disasm.a src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a src/panfrost/lib/libpanfrost_lib.a src/panfrost/util/libpanfrost_util.a src/panfrost/shared/libpanfrost_shared.a"
for l in $LIBS $PAN; do [ -f $H/$l ] || { echo "missing $H/$l"; exit 1; }; done
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
DEF=$(grep -o '^DEF="[^"]*"' $G/build/y2s63-vt23/src/host-tests.sh | sed 's/^DEF="//; s/"$//')
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
A=$(for l in $LIBS; do printf "$H/$l "; done); P=$(for l in $PAN; do printf "$H/$l "; done)
for f in air_to_nir air_intrinsics; do
  C=$(python3 -I -c "import json,sys
for e in json.load(open('$H/compile_commands.json')):
    if e['file'].endswith('compiler/air/$f.c'): print(e['command']); break")
  (cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $T/$f.c.o|; s| -c [^ ]*| -c $HERE/$f.c|; s|^cc |cc -I$M/src/compiler/air |")")
done
$CC -I$HERE -c $HERE/pan_compile_new_host_test.c -o pan_new.o
c++ -o pan_compile_new_host_test pan_new.o air_to_nir.c.o air_intrinsics.c.o -Wl,--start-group $A $P -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
timeout ${TIMEOUT:-600} ./pan_compile_new_host_test "$@"
$CC -I$HERE -c $HERE/isp_warm_host_test.c -o warm.o
c++ -o isp_warm_host_test warm.o air_to_nir.c.o air_intrinsics.c.o -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
timeout 120 ./isp_warm_host_test
# y2s75 libmerge4: with_before()'s numbers on the GPU against the processor's, every block (ties included)
$CC -I$HERE -c $HERE/isp_nrg_host_test.c -o nrg.o
c++ -o isp_nrg_host_test nrg.o air_to_nir.c.o air_intrinsics.c.o -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
timeout 1200 ./isp_nrg_host_test
