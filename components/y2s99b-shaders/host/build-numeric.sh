#!/bin/bash
# y2s99b: shader99b_numeric_host_test (and y2s42's translator_numeric_host_test) of SRC against the translator objects that
# build-harness.sh made in OUTDIR (run that first, same SRC/OUTDIR/extra flags).   bash build-numeric.sh SRC OUTDIR [extra cflags]
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; HERE=$(cd "$(dirname "$0")" && pwd); T=$HERE; H=$T/hostmesa
SRC=$(cd "$1" && pwd); O=$(cd "$2" && pwd); EXTRA="${3:-}"
export TMPDIR=$G/build/y2s99b-shaders/tmp
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
PAN="src/panfrost/compiler/libpanfrost_bifrost.a src/panfrost/compiler/libpanfrost_bifrost_disasm.a src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a src/panfrost/lib/libpanfrost_lib.a src/panfrost/util/libpanfrost_util.a src/panfrost/shared/libpanfrost_shared.a"
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
source $HERE/defs.inc
A=$(for l in $LIBS; do printf "$H/$l "; done); P=$(for l in $PAN; do printf "$H/$l "; done)
mkdir -p $O/rd
sed '/^      default:$/{N;/unsupported module record/s|^|      case 20: break;\n|}' $M/src/compiler/air/air_module.c > $O/rd/air_module.c
grep -q 'case 20: break;' $O/rd/air_module.c
C=$(python3 -I -c "import json,sys
for e in json.load(open('$H/compile_commands.json')):
    if e['file'].endswith('compiler/air/air_module.c'): print(e['command']); break")
(cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $O/rd/air_module.o|; s| -c [^ ]*| -c $O/rd/air_module.c|; s|^cc |cc -w $EXTRA -I$M/src/compiler/air |")")
AIRO=""
for o in $(ar t $H/src/compiler/air/libair.a); do
  b=$(basename $o)
  case $b in air_to_nir.c.o|air_intrinsics.c.o) AIRO="$AIRO $O/air/$b";; air_module.c.o) AIRO="$AIRO $O/rd/air_module.o";; *) AIRO="$AIRO $o";; esac
done
for t in shader99b_numeric translator_numeric; do
  llvm-as $SRC/$t.ll -o $O/$t.bc
  mkdir -p $O/n-$t && cp $SRC/${t}_host_test.c $O/n-$t/test.c
  cc -std=gnu11 -O1 -g $EXTRA -ffp-contract=off -include $T/hostfix.h -include $T/hostfix2.h $DEF -I$SRC $INC -c $O/n-$t/test.c -o $O/n-$t/test.o
  c++ $EXTRA -o $O/${t}_host_test $O/n-$t/test.o $AIRO -Wl,--start-group $A $P -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
done
# y2s99b: CoreAnimation's blur functions evaluated (blur_nir_host_test)
mkdir -p $O/n-blur_nir && cp $SRC/blur_nir_host_test.c $O/n-blur_nir/test.c
cc -std=gnu11 -O1 -g $EXTRA -ffp-contract=off -include $T/hostfix.h -include $T/hostfix2.h $DEF -I$SRC $INC -c $O/n-blur_nir/test.c -o $O/n-blur_nir/test.o
c++ $EXTRA -o $O/blur_nir_host_test $O/n-blur_nir/test.o $AIRO -Wl,--start-group $A $P -Wl,--end-group -lm -lz -lpthread -ldl -ldrm
echo NUMERIC_BUILT
