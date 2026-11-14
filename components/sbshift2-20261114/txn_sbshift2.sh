#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR. ONE transaction of sbshift2-20261114 (copy of sbshift-20261114/txn_sbshift.sh + the READS/TAG guards of
# gnss-20261114/pl7/txn_gnss7.sh): rescue -> snapshot -> offline VM (journal replay + guest) -> catalog check -> verified delta commit
# -> flash KERNEL_IMG (unchanged kernel q10jcz) -> USB capture. It writes the phone: run it only with the user's go.
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   txn_sbshift2.sh KERNEL_IMG TAG               -> sbshift2-22 (default: status bar 22 px = 7.33 pt lower; replaces sbshift1 in place)
#   PX=19 txn_sbshift2.sh KERNEL_IMG TAG         -> another variant: PX in 16 18 19 20 21 22 24 (retunes an installed sbshift2 in place)
#   UNDO=1 txn_sbshift2.sh KERNEL_IMG TAG        -> sbshift2-undo (back to sbshift1 byte for byte, incl. sbshift1.json)
#   READS=N (1..600, default 600)
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/sbshift2-20261114; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_sbshift2.sh KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600 (nord2-usb-console.py limit)"; exit 2; }
P=sbshift2-${PX:-22}
if [ "${UNDO:-0}" = 1 ]; then P=sbshift2-undo; fi
[ -f $S/guest-$P.sh ] || { echo "no variant $P (PX in 16 18 19 20 21 22 24)"; exit 2; }
O=$S/owned-sbshift2.json
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $S/guest-$P.sh --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "SBSHIFT2_REFUSED\|SBSHIFT2_FAILED\|Traceback" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a -q "SBSHIFT2_INSTALL\|SBSHIFT2_UNDO" $X/offline/vm.log || { log "no SBSHIFT2_INSTALL/UNDO line in offline/vm.log; nothing committed"; exit 3; }
grep -a "SBSHIFT2_" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
cp $O $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
