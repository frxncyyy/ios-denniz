#!/bin/bash
# y2s75 libmerge4: isp_bench_host_test (this directory's) built against the air_metal.c of SRC (default: this directory) and an EXISTING host
# Mesa build (softpipe), READ ONLY, in the scratch directory T. Before/after:
#   T=<scratch>/b3 bash bench-host-test.sh ../../y2s72-libmerge3/src libmerge3
#   T=<scratch>/b4 bash bench-host-test.sh . libmerge4
# BENCH_ITERS=n timed renders a case (default 3); BENCH_CASES=mask of the four cases (default all).
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; HERE=$(cd "$(dirname "$0")" && pwd); T=${T:?set T to an empty scratch directory}
SRC=$(cd "${1:-$HERE}" && pwd); LABEL=${2:-$(basename "$(dirname "$SRC")")}
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
  (cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $T/$f.c.o|; s| -c [^ ]*| -c $SRC/$f.c|; s|^cc |cc -I$M/src/compiler/air |")")
done
$CC -c $SRC/air_metal.c -o air_metal_host.o
cc -std=gnu11 -O1 -g -Wall -I$SRC -c $HERE/isp_bench_host_test.c -o bench.o
c++ -o isp_bench_host_test bench.o air_metal_host.o air_to_nir.c.o air_intrinsics.c.o -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
./isp_bench_host_test "$LABEL"
