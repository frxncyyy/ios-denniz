#!/bin/bash
# y2s67-libmerge1: the always-run part of src/host-tests.sh (ycbcr, isp, pan_compile, translator_numeric, image_slots, texture_choice,
# half_store) against the EXISTING host Mesa build (WP08), READ ONLY: the translator objects of SRC are linked ahead of its libair.a
# (as y2s65's stats-host-test.sh does), never copied into it. The optional METALLIB/GNRLIB/KEPT parts are not run (no input files at hand).
#   T=<empty scratch dir> [H=<host mesa build>] [QUICK=1] bash host-tests-ro.sh SRC
set -euo pipefail
G=$NORD2_WORK/gpu
M=$G/build/y2s-stage0/mesa-25.0.7; HERE=$(cd "$1" && pwd); T=${T:?set T}; H=${H:-$G/video/build/WP08/hostmesa}
mkdir -p $T; cd $T
printf '#ifndef __ASSEMBLER__\n#include <pthread.h>\n#define __once_flag_defined 1\n#define once_flag pthread_once_t\n#endif\n' > hostfix.h
printf '#include <stdint.h>\n#include <stdbool.h>\n#include "frontend/winsys_handle.h"\n#include "drm-uapi/drm_fourcc.h"\n' > hostfix2.h
LIBS="src/gallium/auxiliary/libgallium.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a src/gallium/auxiliary/libgalliumvl_stub.a src/gallium/winsys/sw/null/libws_null.a src/compiler/air/libair.a src/util/libxmlconfig.a src/gallium/drivers/softpipe/libsoftpipe.a"
PAN="src/panfrost/compiler/libpanfrost_bifrost.a src/panfrost/compiler/libpanfrost_bifrost_disasm.a src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a src/panfrost/lib/libpanfrost_lib.a src/panfrost/util/libpanfrost_util.a src/panfrost/shared/libpanfrost_shared.a"
for l in $LIBS $PAN; do [ -f $H/$l ] || { echo "missing $H/$l"; exit 1; }; done
INC=$(for d in src/gallium/drivers/panfrost include src src/mapi src/mesa src/gallium/include src/gallium/auxiliary src/gallium/winsys/sw src/gallium/winsys src/gallium/drivers src/compiler/air src/compiler/nir src/compiler src/panfrost src/panfrost/lib src/panfrost/util src/panfrost/compiler; do printf -- "-I$H/$d -I$M/$d "; done)
DEF=$(grep -o '^DEF="[^"]*"' $HERE/host-tests.sh | sed 's/^DEF="//; s/"$//')
CC="cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF $INC"
A=$(for l in $LIBS; do printf "$H/$l "; done); P=$(for l in $PAN; do printf "$H/$l "; done)
for f in air_to_nir air_intrinsics; do
  C=$(python3 -I -c "import json,sys
for e in json.load(open('$H/compile_commands.json')):
    if e['file'].endswith('compiler/air/$f.c'): print(e['command']); break")
  (cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $T/$f.c.o|; s| -c [^ ]*| -c $HERE/$f.c|; s|^cc |cc -I$M/src/compiler/air |")")
done
TR="air_to_nir.c.o air_intrinsics.c.o"
LINK() { out=$1; shift; c++ -o $out "$@" $TR -Wl,--start-group $A ${WITHPAN:+$P} -Wl,--end-group -lm -lz -lpthread -ldl -ldrm; }
$CC -Wall -c $HERE/air_metal.c -o air_metal_host.o
cc -std=gnu11 -O1 -g -I$HERE -c $HERE/ycbcr_gpu_host_test.c -o test.o
LINK ycbcr_gpu_host_test test.o air_metal_host.o; ./ycbcr_gpu_host_test
C=$G/build/camera-userland-20261109
cc -std=gnu11 -O2 -g -I$C/src -c $C/src/camera_isp.c -o camera_isp.o
cc -std=gnu11 -O1 -g -Wall -I$HERE -I$C/src -c $HERE/isp_gpu_host_test.c -o isp_test.o
LINK isp_gpu_host_test isp_test.o camera_isp.o air_metal_host.o; ./isp_gpu_host_test
$CC -I$HERE -c $HERE/pan_compile_host_test.c -o pan.o
WITHPAN=1 LINK pan_compile_host_test pan.o; ./pan_compile_host_test
mkdir -p rd t2 t3
sed '/^      default:$/{N;/unsupported module record/s|^|      case 20: break;\n|}' $M/src/compiler/air/air_module.c > rd/air_module.c
grep -q 'case 20: break;' rd/air_module.c
C=$(python3 -I -c "import json,sys
for e in json.load(open('$H/compile_commands.json')):
    if e['file'].endswith('compiler/air/air_module.c'): print(e['command']); break")
(cd $H && eval "$(echo "$C" | sed "s| -MD -MQ [^ ]* -MF [^ ]*||; s| -o [^ ]*| -o $T/rd/air_module.o|; s| -c [^ ]*| -c $T/rd/air_module.c|; s|^cc |cc -w -I$M/src/compiler/air |")")
llvm-as $HERE/translator_numeric.ll -o numeric.bc
cp $HERE/translator_numeric_host_test.c t2/test.c
cc -std=gnu11 -O1 -g -ffp-contract=off -include $T/hostfix.h -include $T/hostfix2.h $DEF -I$HERE $INC -c t2/test.c -o numeric_test.o
WITHPAN=1 LINK translator_numeric_host_test numeric_test.o rd/air_module.o; ./translator_numeric_host_test numeric.bc
llvm-as $HERE/image_slots_2.ll -o slots2.bc && llvm-as $HERE/image_slots_1.ll -o slots1.bc
cc -std=gnu11 -O1 -g -Wall -I$HERE -c $HERE/image_slots_host_test.c -o slots_test.o
LINK image_slots_host_test slots_test.o air_metal_host.o rd/air_module.o; ./image_slots_host_test slots2.bc slots1.bc
llvm-as $HERE/texture_choice.ll -o choice.bc
cc -std=gnu11 -O1 -g -Wall -I$HERE -c $HERE/texture_choice_host_test.c -o choice_test.o
LINK texture_choice_host_test choice_test.o air_metal_host.o rd/air_module.o; ./texture_choice_host_test choice.bc
llvm-as $HERE/half_store.ll -o half.bc
cp $HERE/half_store_host_test.c t3/test.c
cc -std=gnu11 -O1 -g -include $T/hostfix.h -include $T/hostfix2.h $DEF -I$HERE $INC -c t3/test.c -o half_test.o
WITHPAN=1 LINK half_store_host_test half_test.o rd/air_module.o; ./half_store_host_test half.bc
echo HOST_TESTS_RO_DONE
