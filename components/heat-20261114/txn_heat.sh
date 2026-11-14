#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR. ONE transaction of heat-20261114, then flash KERNEL_IMG (q10jck or later: it must contain 0530, the clock
# floor -- with an older kernel dasd stores a 1970 date again on the next boot and the fix is gone). Same steps as
# hotfix-20261114/txn_hotfix.sh (rescue -> snapshot -> offline VM (journal replay + guest) -> catalog check -> verified delta commit ->
# flash KERNEL_IMG -> USB capture). It writes the phone: run it only with the user's go.
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   txn_heat.sh KERNEL_IMG TAG                 -> heat1     (dasd widget budgets: the stored 1970 lastModulation removed)
#   P=heat1full txn_heat.sh KERNEL_IMG TAG     -> heat1 + the bluetoothd / fairplayd.H2 / wifip2pd launchd jobs removed (optional, see block-respawn1.py)
#   UNDO=1 [P=...] txn_heat.sh KERNEL_IMG TAG  -> the undo of the same set
#   READS=1800                                 -> seconds of USB log after the flash (default 1800: the heat needs a long idle stretch)
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/heat-20261114; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2; mkdir -p $X
P=${P:-heat1}; [ "$P" = heat1 ] || [ "$P" = heat1full ] || { echo "P must be heat1 or heat1full"; exit 2; }
O=$S/owned-$P.json
if [ "${UNDO:-0}" = 1 ]; then P=$P-undo; fi
strings -a "$IMG" > /dev/null; READS=${READS:-1800}
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $S/guest-$P.sh --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "DASDMOD1_REFUSED\|RESPAWN1_REFUSED\|Traceback" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a "DASDMOD1_\|RESPAWN1_" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
cp $O $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
