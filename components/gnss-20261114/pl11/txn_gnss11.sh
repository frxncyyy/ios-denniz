#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR. ONE transaction of gnss-20261114/pl11 (0589, kernel q10jgdc): VARIANT=gnssfix11r (K=37 quiet pipe + n2mnld replay) | gnssfix11g (K=38, + GIC trace) | gnssfix11l (K=37, libmnl boots). Older pl10 doc: VARIANT=gnssfix10r (q10jgdb, K=35 + n2mnld pipe replay boot) | gnssfix10d (q10jgg, K=31 deferred while libmnl runs). Older pl9 doc: VARIANT=gnssfix9a (K=34) | gnssfix9b (K=35) | gnssfix9c (K=36); pl8 doc follows: VARIANT=gnssfix8 (default: n2mnld gnssd8, ladder K=32 = libmnl
# boots the DSP itself, writes passed, read poll 1 ms = pacing fix H1) or VARIANT=gnssfix8k (ladder K=33 = 0587 S13 in-kernel pacing
# probe: kernel download with a 200 ms idle gap after fragment 1, then HOLD; libmnl writes dropped), or with UNDO=1 its undo.
# Then flash KERNEL_IMG (pl9: q10jgdb; pl8: q10jgd = q10jcz + 0583..0587; gnssfix8 also runs on q10jgo, gnssfix8k needs q10jgd).
# (rescue -> snapshot -> offline VM (journal replay + guest) -> catalog check -> verified delta commit -> flash KERNEL_IMG -> USB capture).
# It writes the phone: run it only with the user's go.
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   [VARIANT=gnssfix8k] READS=600 txn_gnss11.sh KERNEL_IMG TAG
#   UNDO=1 [VARIANT=...] txn_gnss11.sh KERNEL_IMG TAG      (logs dumped first, then both jobs/files removed; flash q10jcz for the safe state)
#   READS=N  seconds of USB log after the flash (1..600)
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/gnss-20261114/pl11; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_gnss11.sh KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600 (nord2-usb-console.py limit)"; exit 2; }
V=${VARIANT:-gnssfix11r}; case $V in gnssfix11r|gnssfix11g|gnssfix11l) ;; *) echo "VARIANT must be gnssfix11r, gnssfix11g or gnssfix11l"; exit 2;; esac; VU=${V^^}
P=$V; O=$S/owned-$V.json
if [ "${UNDO:-0}" = 1 ]; then P=$V-undo; O=$S/owned-$V-undo.json; fi
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $S/guest-$P.sh --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "_REFUSED\|${VU}_SKIPPED\|Traceback" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a -q "${VU}_OK\|${VU}_UNDO_OK" $X/offline/vm.log || { log "no ${VU}_OK/${VU}_UNDO_OK in offline/vm.log; nothing committed"; exit 3; }
grep -a "$VU" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
cp $O $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
