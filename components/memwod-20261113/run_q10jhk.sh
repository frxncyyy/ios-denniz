#!/bin/bash
# q10jhk = q10jgs chain (0394..0418, ram5088) + 0480 (wire-on-demand surfaces, off) + 0481 (GPU MADVISE + purge, off) + 0482 (compressor mode 2).
# usage: run_q10jhk.sh [preflight]
set -u
B=$NORD2_WORK/gpu/build
export NORD2_PHYS_BASE=0x100000000 NORD2_MEM_SIZE=0x13e000000 WARN_BUDGET=8
export PATCHES="0394 0395 0396 0397 0398 0399 0400 0401 0403 0404 0405 0406 0407 0408 0409 0410 0411 0413 0414 0415 0416 0417 0418 0480 0481 0482"
X=""; for d in colorfix-20261113 fbpace-20261112 fpsmeter-20261114-on0403 gov-20261113 gpulat-20261112 hwcolor-20261112-on0398 hwfix-20261113-on0403 hwmulti4l-20261113 \
  iomapfix-20261113 iommu-20261112 iommu-20261113b iommuboot-20261113 iommuinit-20261113 iommusub-20261113 memfix-20261113/jgn memfix-20261113b nocpu-20261113 \
  nocpu2-20261113 poolreclaim-20261114 usecount-20261112-on0397 memwod-20261113/wod-0480 memwod-20261113/gpumadv-0481 memwod-20261113/vmcomp-0482; do X="$X $B/$d"; done
export EXTRA_PATCH_DIRS="$X"
O=$B/device-mmu-q10jhk-candidate
if [ "${1:-}" = preflight ]; then PREFLIGHT_ONLY=1 $B/build_q10jfi.sh q10jhk $O; exit $?; fi
$B/build_q10jfi.sh q10jhk $O > $B/build_q10jhk.run.log 2>&1; echo "BUILD_EXIT $?" >> $B/build_q10jhk.run.log
