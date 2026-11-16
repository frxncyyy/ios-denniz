#!/bin/bash
# webxr1: webxr_host_test.c of SRC on softpipe, against y2s99b-shaders/host/hostmesa (+ its h-new translator objects).
#   bash build-webxr-test.sh SRC OUTDIR
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; T=$G/build/y2s99b-shaders/host; H=$T/hostmesa; HN=$T/h-new
SRC=$(cd "$1" && pwd); mkdir -p "$2"; O=$(cd "$2" && pwd)
export TMPDIR=$O
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
source $T/defs.inc
A=$(for l in $LIBS; do printf "$H/$l "; done)
AIRO=""
for o in $(ar t $H/src/compiler/air/libair.a); do
  b=$(basename $o)
  case $b in air_to_nir.c.o|air_intrinsics.c.o) AIRO="$AIRO $HN/air/$b";; air_module.c.o) AIRO="$AIRO $HN/rd/air_module.o";; *) case $o in /*) AIRO="$AIRO $o";; *) AIRO="$AIRO $H/src/compiler/air/$o";; esac;; esac
done
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
$CC -DRWA_HOST_WIDEN16 -c $SRC/air_metal.c -o $O/air_metal_host.o
$CC -c $SRC/rwa_host_widen16.c -o $O/rwa_host_widen16.o
$CC -Wall -I$SRC -c $SRC/webxr_host_test.c -o $O/webxr_test.o
c++ -o $O/webxr_host_test $O/webxr_test.o $O/air_metal_host.o $O/rwa_host_widen16.o $AIRO -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
echo WEBXR_TEST_BUILT
