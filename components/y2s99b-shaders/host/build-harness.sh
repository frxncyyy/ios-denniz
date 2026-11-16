#!/bin/bash
# y2s99b: the metallib harness (metallib_compile_host_test.c of SRC) against the host Mesa build in host/hostmesa,
# with SRC's air_to_nir.c / air_intrinsics.c / air_metal.c.   bash build-harness.sh SRC OUTDIR [extra cflags]
# (the recipe of src/host-tests.sh, METALLIB part, written for one source tree at a time)
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; HERE=$(cd "$(dirname "$0")" && pwd); T=$HERE; H=$T/hostmesa
SRC=$(cd "$1" && pwd); mkdir -p "$2"; O=$(cd "$2" && pwd); EXTRA="${3:-}"
export TMPDIR=$G/build/y2s99b-shaders/tmp
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
PAN="src/panfrost/compiler/libpanfrost_bifrost.a src/panfrost/compiler/libpanfrost_bifrost_disasm.a src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a src/panfrost/lib/libpanfrost_lib.a src/panfrost/util/libpanfrost_util.a src/panfrost/shared/libpanfrost_shared.a"
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
source $HERE/defs.inc
A=$(for l in $LIBS; do printf "$H/$l "; done); P=$(for l in $PAN; do printf "$H/$l "; done)
# libair.a with SRC's two translator files (a private copy of the thin archive's members)
mkdir -p $O/air
for f in air_to_nir air_intrinsics; do
  C=$(python3 -I -c "import json,sys
for e in json.load(open('$H/compile_commands.json')):
    if e['file'].endswith('compiler/air/$f.c'): print(e['command']); break")
  (cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $O/air/$f.c.o|; s| -c [^ ]*| -c $SRC/$f.c|; s|^cc |cc $EXTRA -I$M/src/compiler/air |")")
done
AIRO=""
for o in $(ar t $H/src/compiler/air/libair.a); do
  b=$(basename $o)
  case $b in air_to_nir.c.o|air_intrinsics.c.o) AIRO="$AIRO $O/air/$b";; *) case $o in /*) AIRO="$AIRO $o";; *) AIRO="$AIRO $H/src/compiler/air/$o";; esac;; esac
done
mkdir -p $O/t && cp $SRC/metallib_compile_host_test.c $O/t/test.c
cc -std=gnu11 -O1 -g $EXTRA -include $T/hostfix.h -include $T/hostfix2.h $DEF -DRWA_REFUSED_TEST -DRWA_REFUSED_DIR="\"$O/refused-test\"" -DRWA_KEPT_DIR="\"$O/kept-test\"" -I$SRC $INC -c $O/t/test.c -o $O/metallib.o
X="-I/usr/include/libdrm -I$M/include/drm-uapi -I$M/src/panfrost/shared -I$M/src/panfrost/midgard -I$H/src/panfrost/midgard -I$H/src/panfrost/lib -I$H/src/panfrost"
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
for f in pan_nir_lower_sysvals pan_nir_lower_res_indices; do [ -f $T/$f.o ] || $CC $X -c $M/src/gallium/drivers/panfrost/$f.c -o $T/$f.o; done
c++ $EXTRA -o $O/metallib_compile_host_test $O/metallib.o $T/pan_nir_lower_sysvals.o $T/pan_nir_lower_res_indices.o $AIRO -Wl,--start-group $A $P -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
echo HARNESS_BUILT $O/metallib_compile_host_test
