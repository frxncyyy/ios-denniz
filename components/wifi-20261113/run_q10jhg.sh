#!/bin/bash
# q10jhg = q10jha chain (q10jgs + heap 0440/0441) + 0430 v2 (Wi-Fi join decoder + scan admission; transport/rx stay the CI members). usage: run_q10jhg.sh [preflight]
set -u
export NORD2_PHYS_BASE=0x100000000 NORD2_MEM_SIZE=0x13e000000 WARN_BUDGET=8
export PATCHES="0394 0395 0396 0397 0398 0399 0400 0401 0403 0404 0405 0406 0407 0408 0409 0410 0411 0413 0414 0415 0416 0417 0418 0430 0440 0441"
export EXTRA_PATCH_DIRS="$NORD2_WORK/gpu/build/colorfix-20261113 $NORD2_WORK/gpu/build/fbpace-20261112 $NORD2_WORK/gpu/build/fpsmeter-20261114-on0403 $NORD2_WORK/gpu/build/gov-20261113 $NORD2_WORK/gpu/build/gpulat-20261112 $NORD2_WORK/gpu/build/hwcolor-20261112-on0398 $NORD2_WORK/gpu/build/hwfix-20261113-on0403 $NORD2_WORK/gpu/build/hwmulti4l-20261113 $NORD2_WORK/gpu/build/iomapfix-20261113 $NORD2_WORK/gpu/build/iommu-20261112 $NORD2_WORK/gpu/build/iommu-20261113b $NORD2_WORK/gpu/build/iommuboot-20261113 $NORD2_WORK/gpu/build/iommuinit-20261113 $NORD2_WORK/gpu/build/iommusub-20261113 $NORD2_WORK/gpu/build/memfix-20261113/jgn $NORD2_WORK/gpu/build/memfix-20261113b $NORD2_WORK/gpu/build/nocpu-20261113 $NORD2_WORK/gpu/build/nocpu2-20261113 $NORD2_WORK/gpu/build/poolreclaim-20261114 $NORD2_WORK/gpu/build/usecount-20261112-on0397 $NORD2_WORK/gpu/build/gfxheap-20261113/heap-0440 $NORD2_WORK/gpu/build/gfxheap-20261113/gpuheap-0441 $NORD2_WORK/gpu/build/wifi-20261113"
O=$NORD2_WORK/gpu/build/device-mmu-q10jhg-candidate
if [ "${1:-}" = preflight ]; then PREFLIGHT_ONLY=1 $NORD2_WORK/gpu/build/wifi-20261113/build_q10jgw.sh q10jhg $O; exit $?; fi
$NORD2_WORK/gpu/build/wifi-20261113/build_q10jgw.sh q10jhg $O > $NORD2_WORK/gpu/build/build_q10jhg.run.log 2>&1; echo "BUILD_EXIT $?" >> $NORD2_WORK/gpu/build/build_q10jhg.run.log
python3 $NORD2_WORK/gpu/build/wifi-20261113/check_scan_path.py $O $NORD2_WORK/gpu/build/device-mmu-q10jha-candidate >> $NORD2_WORK/gpu/build/build_q10jhg.run.log 2>&1; echo "SCANPATH_EXIT $?" >> $NORD2_WORK/gpu/build/build_q10jhg.run.log
