#!/bin/bash
# q10jram8c<N> = q10jfinal1 recipe + 0652 driver piece + the 0652 osfmk objects of stage N (ram8-20261114/osfmk/objs-s<N>: arm_vm_init.o,
# pmap.o, pmap_data.o compiled with -DN2RAM_STAGE=N).   usage: build_q10jram8c.sh STAGE [NAME]
set -euo pipefail
ST=${1:?stage 0/1/2}; NAME=${2:-q10jram8c$ST}
export RAM8_OSFMK_OBJS=$NORD2_WORK/gpu/build/ram8-20261114/osfmk/objs-s$ST RAM8_STAGE=$ST
[ -f $RAM8_OSFMK_OBJS/compile-result.json ] || { echo "no objects for stage $ST"; exit 1; }
cd $NORD2_WORK/gpu/build/cpu-20261114 && GP_MORE="\"0652 $NORD2_WORK/gpu/build/ram8-20261114/piece-0652\"" EXTRA_ARGS_OVERRIDE=${EXTRA_ARGS_OVERRIDE:-n2gdv=9} bash $NORD2_WORK/gpu/build/ram8-20261114/build_q10jfinal_ram8.sh "$NAME"
