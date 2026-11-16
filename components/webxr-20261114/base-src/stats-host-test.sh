#!/bin/bash
# y2s65: build and run isp_stats_host_test (and isp_gpu_host_test: the pictures unchanged) against an EXISTING host Mesa build of the same
# Mesa tree, READ ONLY (this directory's translator objects are linked ahead of its libair.a, never copied into it).
#   T=<empty scratch dir> [H=<host mesa build dir>] [QUICK=1] bash stats-host-test.sh
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; HERE=$(cd "$(dirname "$0")" && pwd); T=${T:?set T to an empty scratch directory}
H=${H:-$G/video/build/WP08/hostmesa}
mkdir -p $T; cd $T
printf '#ifndef __ASSEMBLER__\n#include <pthread.h>\n#define __once_flag_defined 1\n#define once_flag pthread_once_t\n#endif\n' > hostfix.h
printf '#include <stdint.h>\n#include <stdbool.h>\n#include "frontend/winsys_handle.h"\n#include "drm-uapi/drm_fourcc.h"\n' > hostfix2.h
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/compiler/air/libair.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
for l in $LIBS; do [ -f $H/$l ] || { echo "missing $H/$l"; exit 1; }; done
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
DEF=$(grep -o '^DEF="[^"]*"' $G/build/y2s63-vt23/src/host-tests.sh | sed 's/^DEF="//; s/"$//')
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
A=$(for l in $LIBS; do printf "$H/$l "; done)
for f in air_to_nir air_intrinsics; do
  C=$(python3 -I -c "import json,sys
for e in json.load(open('$H/compile_commands.json')):
    if e['file'].endswith('compiler/air/$f.c'): print(e['command']); break")
  (cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $T/$f.c.o|; s| -c [^ ]*| -c $HERE/$f.c|; s|^cc |cc -I$M/src/compiler/air |")")
done
$CC -c $HERE/air_metal.c -o air_metal_host.o
C=$G/build/camera-userland-20261109
cc -std=gnu11 -O2 -g -I$C/src -c $C/src/camera_isp.c -o camera_isp.o
cc -std=gnu11 -O1 -g -Wall -I$HERE -I$C/src -c $HERE/isp_stats_host_test.c -o stats_test.o
c++ -o isp_stats_host_test stats_test.o camera_isp.o air_metal_host.o air_to_nir.c.o air_intrinsics.c.o -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
./isp_stats_host_test
cc -std=gnu11 -O1 -g -Wall -I$HERE -I$C/src -c $HERE/isp_gpu_host_test.c -o isp_test.o
c++ -o isp_gpu_host_test isp_test.o camera_isp.o air_metal_host.o air_to_nir.c.o air_intrinsics.c.o -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
QUICK=1 ./isp_gpu_host_test
