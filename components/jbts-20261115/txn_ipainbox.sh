#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR (host-only agent). ONE transaction of ipainbox1 (gpu/build/jbts-20261115): offline-install IPAs through
# TrollStore without network/UI. Needs jbts1 on the phone. Steps inside ONE commit: [apps undo of Terraria/SubwaySurf if the payload
# was made with --apps-undo] + ipainbox.sh + job com.nord2.jb.ipainbox + IPAs into /var/mobile/Documents/ipa-inbox; then flash
# KERNEL_IMG (0730 kernel recommended, not required for the inbox) and capture. After the boot the job installs each IPA (~2 min after
# boot, then up to a few min per IPA): result in /var/mobile/Documents/ipa-inbox/result.log (next txn snapshot or ssh).
# It writes the phone: run it only with the user's go.
# usage: python3 -I jbts-20261115/make_ipainbox.py NAME [--apps-undo] A.ipa B.ipa      (once, makes payload-ipainbox-NAME)
#        jbts-20261115/txn_ipainbox.sh NAME KERNEL_IMG TAG
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/jbts-20261115; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; NAME=$1; shift; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_ipainbox.sh NAME KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600"; exit 2; }
P=ipainbox-$NAME; G=$S/guest-ipainbox1.sh; OK='IPAINBOX1_OK'
[ -f $S/payload-$P/plan.json ] || { echo "no $S/payload-$P: run make_ipainbox.py $NAME ..."; exit 2; }
APPS=$(python3 -c "import json;print(','.join(a['bundle'] for a in json.load(open('$S/payload-$P/plan.json'))['apps_undo']))")
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $G --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "_REFUSED\|Traceback\|AssertionError" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a -q "$OK" $X/offline/vm.log || { log "no $OK in offline/vm.log; nothing committed"; exit 3; }
grep -a "IPAINBOX1" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
python3 $S/tools/undo_owned.py $X/offline/vm.log ${APPS:+--apps $APPS} > $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
