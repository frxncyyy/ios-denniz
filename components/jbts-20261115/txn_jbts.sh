#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR (host-only agent). ONE transaction of gpu/build/jbts-20261115 (Sileo + OpenSSH + TrollStore, rootless /var/jb),
# copy of the gfxall/gnss txn pattern: rescue -> snapshot -> offline VM (journal replay + guest) -> catalog check vs owned list ->
# verified delta commit -> flash KERNEL_IMG -> USB capture. Userland only: any current kernel works (no kernel piece needed).
# It writes the phone: run it only with the user's go.  Build the payload first: python3 -I build_jbts.py  (see SETUP.md)
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   txn_jbts.sh KERNEL_IMG TAG                        install (VARIANT=jbts1, default)
#   VARIANT=jbts1-off|jbts1-on|jbts1-sshoff|jbts1-sshon txn_jbts.sh KERNEL_IMG TAG   kill switches (all / SSH only)
#   UNDO=1 txn_jbts.sh KERNEL_IMG TAG                 remove everything (prefix, jobs, TrollStore bundles, pref; LS caches aside)
#   READS=N  seconds of USB log after the flash (default/max 600)
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/jbts-20261115; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_jbts.sh KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600"; exit 2; }
P=${VARIANT:-jbts1}
case $P in
  jbts1) G=$S/guest-jbts1.sh; O=$S/owned-jbts1.json; OK='JBTS1_OK' ;;
  jbts1-off|jbts1-on|jbts1-sshoff|jbts1-sshon) G=$S/guest-jbts1-flags.sh; O=$S/owned-jbts1-flags.json; OK='JBTS1_FLAGS_OK' ;;
  *) echo "bad VARIANT $P"; exit 2 ;;
esac
if [ "${UNDO:-0}" = 1 ]; then P=jbts1-undo; G=$S/guest-jbts1-undo.sh; O=; OK='JBTS1_UNDO_OK'; fi
[ -f $S/payload-$P/plan.json ] || { echo "no $S/payload-$P: run build_jbts.py"; exit 2; }
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $G --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "_REFUSED\|Traceback\|AssertionError" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a -q "$OK" $X/offline/vm.log || { log "no $OK in offline/vm.log; nothing committed"; exit 3; }
grep -a "JBTS1" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
if [ -n "$O" ]; then cp $O $X/owned.json
else python3 $S/tools/undo_owned.py $X/offline/vm.log > $X/owned.json; fi
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
