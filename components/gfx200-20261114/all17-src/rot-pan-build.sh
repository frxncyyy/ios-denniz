#!/bin/bash
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; T=$G/build/y2s99b-shaders/host; H=$T/hostmesa
SRC=$G/build/gfx200-20261114/all17-src; O=$1
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
PAN="src/panfrost/compiler/libpanfrost_bifrost.a src/panfrost/compiler/libpanfrost_bifrost_disasm.a src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a src/panfrost/lib/libpanfrost_lib.a src/panfrost/util/libpanfrost_util.a src/panfrost/shared/libpanfrost_shared.a"
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
source $T/defs.inc
A=$(for l in $LIBS; do printf "$H/$l "; done); P=$(for l in $PAN; do printf "$H/$l "; done)
AIRO=""
for o in $(ar t $H/src/compiler/air/libair.a); do b=$(basename $o); case $b in air_to_nir.c.o|air_intrinsics.c.o) AIRO="$AIRO $O/air/$b";; *) AIRO="$AIRO $o";; esac; done
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
$CC -DRWA_HOST_WIDEN16 -I$SRC -c $SRC/pan_compile_host_test.c -o $O/pan.o
$CC -c $SRC/rwa_host_widen16.c -o $O/rwa_host_widen16.o
c++ -o $O/pan_compile_host_test $O/pan.o $O/rwa_host_widen16.o $AIRO -Wl,--start-group $A $P -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
echo PAN_BUILT
