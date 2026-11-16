#!/bin/bash
# y2s91 (thumbgrid): rw_thumbgrid_host_test.c against the EXISTING host Mesa build (WP08, read only), plus the y2s38 ycbcr test again
# (the old entry point after the refactor into convert_ycbcr_impl).
#   T=<empty scratch dir> [H=<host mesa build>] bash thumbgrid-host-test.sh SRC
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; HERE=$(cd "$1" && pwd); T=${T:?set T}; H=${H:-$G/video/build/WP08/hostmesa}
mkdir -p $T; cd $T
printf '#ifndef __ASSEMBLER__\n#include <pthread.h>\n#define __once_flag_defined 1\n#define once_flag pthread_once_t\n#endif\n' > hostfix.h
printf '#include <stdint.h>\n#include <stdbool.h>\n#include "frontend/winsys_handle.h"\n#include "drm-uapi/drm_fourcc.h"\n' > hostfix2.h
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/compiler/air/libair.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
for l in $LIBS; do [ -f $H/$l ] || { echo "missing $H/$l"; exit 1; }; done
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
DEF=$(grep -o '^DEF="[^"]*"' $HERE/host-tests.sh | sed 's/^DEF="//; s/"$//')
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
A=$(for l in $LIBS; do printf "$H/$l "; done)
for f in air_to_nir air_intrinsics; do
  C=$(python3 -I -c "import json,sys
for e in json.load(open('$H/compile_commands.json')):
    if e['file'].endswith('compiler/air/$f.c'): print(e['command']); break")
  (cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $T/$f.c.o|; s| -c [^ ]*| -c $HERE/$f.c|; s|^cc |cc -I$M/src/compiler/air |")")
done
TR="air_to_nir.c.o air_intrinsics.c.o"
LINK() { out=$1; shift; c++ -o $out "$@" $TR -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm; }
$CC -Wall -c $HERE/air_metal.c -o air_metal_host.o
python3 -I - $HERE/air_metal.c ubo_tail.inc <<'PY'
import sys
s = open(sys.argv[1]).read()
a = s.index('static void\nrwa_tg_ubo_tail(')
b = s.index('\n}\n', a) + 3
open(sys.argv[2], 'w').write(s[a:b])
PY
cc -std=gnu11 -O1 -g -Wall -I$HERE -I$T -c $HERE/rw_thumbgrid_host_test.c -o tg_test.o
LINK rw_thumbgrid_host_test tg_test.o air_metal_host.o; ./rw_thumbgrid_host_test
cc -std=gnu11 -O1 -g -I$HERE -c $HERE/ycbcr_gpu_host_test.c -o test.o
LINK ycbcr_gpu_host_test test.o air_metal_host.o; ./ycbcr_gpu_host_test
echo THUMBGRID_HOST_TEST_DONE
