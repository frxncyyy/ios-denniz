#!/bin/bash
# NOT EXECUTED. ONE transaction: stock apps (stock1) + PosterBoard home (poster1) + wallpaper/extension points (wallpaper1)
# (+ optional HFS grow), then flash KERNEL_IMG.  Same steps as stock-20261114/txn_stock.sh (which this replaces: do not run both).
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   txn_stockwall.sh KERNEL_IMG TAG                -> stock1 + poster1 + wallpaper1, then flash KERNEL_IMG
#   GROW=1 txn_stockwall.sh KERNEL_IMG TAG         -> same, then grow the HFS root to ~107 GiB before flashing
#   ONLY=wallpaper1 txn_stockwall.sh KERNEL_IMG TAG -> wallpaper1 alone (stock1/poster1 already on the phone, or not wanted)
#   UNDO=1 txn_stockwall.sh KERNEL_IMG TAG         -> the undo of the same set (review owned-*-undo.json first)
# rescue -> snapshot -> offline VM (journal replay + guest) -> catalog check -> verified delta commit
#   -> [GROW=1: commit_hfs_grow.py plan + --commit in the same rescue session] -> flash KERNEL_IMG -> USB capture
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/wallpaper-20261114; G=$B/stock-20261114; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2; mkdir -p $X
P=${ONLY:-stockwall}; [ "$P" = stockwall ] || [ "$P" = wallpaper1 ] || { echo "ONLY must be wallpaper1"; exit 2; }
if [ "${UNDO:-0}" = 1 ]; then P=$P-undo; fi
if [ "${ONLY:-}" = wallpaper1 ]; then O=$S/owned-wallpaper1.json; elif [ "${UNDO:-0}" = 1 ]; then O=$S/owned-stockwall-undo.json; else O=$S/owned-stockwall.json; fi
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P grow=${GROW:-0}; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $S/guest-$P.sh --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "STOCK1_REFUSED\|POSTER1_REFUSED\|WALLPAPER1_REFUSED\|Traceback" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a "STOCK1_\|POSTER1_\|WALLPAPER1_" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
cp $O $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
if [ "${GROW:-0}" = 1 ]; then
  python3 $G/commit_hfs_grow.py --main $MAIN --output $X/grow > $X/grow-plan.log 2>&1; log grow planned "$(grep HFSGROW_PLAN $X/grow-plan.log | cut -c1-160)"
  python3 $G/commit_hfs_grow.py --main $MAIN --output $X/grow --commit > $X/grow.log 2>&1; log grow committed "$(grep HFSGROW_COMMITTED $X/grow.log)"
fi
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds ${READS:-600} > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( ${READS:-600} / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
