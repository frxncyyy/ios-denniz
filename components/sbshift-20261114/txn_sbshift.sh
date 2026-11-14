#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR. ONE transaction of sbshift-20261114 (copy of hotfix-20261114/txn_hotfix.sh), then flash KERNEL_IMG (q10jcq). Same steps as
# wallpaper-20261114/txn_stockwall.sh (rescue -> snapshot -> offline VM (journal replay + guest) -> catalog check ->
# verified delta commit -> flash KERNEL_IMG -> USB capture). It writes the phone: run it only with the user's go.
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   txn_sbshift.sh KERNEL_IMG TAG           -> sbshift1 (UIKitCore status bar provider patch in dyld_shared_cache_arm64.03)
#   UNDO=1 txn_sbshift.sh KERNEL_IMG TAG    -> sbshift1-undo (stock subcache bytes and page hashes back)
#   It can also be merged into a combined transaction: the guest is guest-sbshift1.sh (one python block), owned paths owned-sbshift1.json.
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/sbshift-20261114; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2; mkdir -p $X
P=sbshift1
if [ "${UNDO:-0}" = 1 ]; then P=$P-undo; fi
O=$S/owned-sbshift1.json
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $S/guest-$P.sh --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "SBSHIFT1_REFUSED\|SBSHIFT1_FAILED\|Traceback" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a "SBSHIFT1_" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
cp $O $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds ${READS:-900} > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( ${READS:-900} / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
