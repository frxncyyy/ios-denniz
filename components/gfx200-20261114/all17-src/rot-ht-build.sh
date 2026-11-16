#!/bin/bash
# gfxall17 rot host test: all17-src air_metal.c (+ translator files) on the y2s99b host Mesa (softpipe). OUT=$1
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; T=$G/build/y2s99b-shaders/host; H=$T/hostmesa
SRC=$G/build/gfx200-20261114/all17-src; O=$1; mkdir -p $O/air
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
source $T/defs.inc
A=$(for l in $LIBS; do printf "$H/$l "; done)
for f in air_to_nir air_intrinsics; do
  C=$(python3 -I -c "import json
for e in json.load(open('$H/compile_commands.json')):
    if e['file'].endswith('compiler/air/$f.c'): print(e['command']); break")
  (cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $O/air/$f.c.o|; s| -c [^ ]*| -c $SRC/$f.c|; s|^cc |cc -I$M/src/compiler/air |")")
done
AIRO=""
for o in $(ar t $H/src/compiler/air/libair.a); do
  b=$(basename $o)
  case $b in air_to_nir.c.o|air_intrinsics.c.o) AIRO="$AIRO $O/air/$b";; *) AIRO="$AIRO $o";; esac
done
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
$CC -DRWA_HOST_WIDEN16 -c $SRC/air_metal.c -o $O/air_metal_host.o
$CC -c $SRC/rwa_host_widen16.c -o $O/rwa_host_widen16.o
cc -std=gnu11 -O1 -g -Wall -I$SRC -c $SRC/rot_host_test.c -o $O/rot_test.o
cc -std=gnu11 -O1 -g -I$SRC -c $SRC/ycbcr_gpu_host_test.c -o $O/ycbcr_test.o
for t in rot ycbcr; do
  c++ -o $O/${t}_host_test $O/${t}_test.o $O/air_metal_host.o $O/rwa_host_widen16.o $AIRO -Wl,--start-group $A -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
done
echo ROT_HT_BUILT
