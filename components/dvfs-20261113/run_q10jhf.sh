#!/bin/bash
# q10jhf = q10jgs chain (0394..0401, 0403..0411, 0413..0418, 5088 MiB window) + 0460 0461 0462 (dvfs-20261113).  usage: run_q10jhf.sh [preflight]
set -u
B=$NORD2_WORK/gpu/build
export NORD2_PHYS_BASE=0x100000000 NORD2_MEM_SIZE=0x13e000000 WARN_BUDGET=8
export PATCHES="0394 0395 0396 0397 0398 0399 0400 0401 0403 0404 0405 0406 0407 0408 0409 0410 0411 0413 0414 0415 0416 0417 0418 0460 0461 0462"
export EXTRA_PATCH_DIRS="$(cat $B/memfix-20261113b/extra_dirs.txt) $B/dvfs-20261113"
O=$B/device-mmu-q10jhf-candidate
if [ "${1:-}" = preflight ]; then PREFLIGHT_ONLY=1 $B/build_q10jfi.sh q10jhf $O; exit $?; fi
$B/build_q10jfi.sh q10jhf $O > $B/build_q10jhf.run.log 2>&1; echo "BUILD_EXIT $?" >> $B/build_q10jhf.run.log
