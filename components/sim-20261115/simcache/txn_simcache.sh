#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR (host-only agent). ONE transaction of gpu/build/sim-20261115/simcache (userland helper n2simcache: "Carrier
# Very 54.0" with no SIM), copy of the jbts/gfxall txn pattern: rescue -> snapshot -> offline VM (guest) -> catalog check vs owned list
# -> verified delta commit -> flash KERNEL_IMG -> USB capture. Needs a kernel with 0710 (kern.n2sim_state) and 0713 (kern.n2bb_link,
# kern.n2sim_helper): q10jfinal16sim2 or later; on an older kernel the helper only logs "kernel without 0710" and does nothing.
# It writes the phone: run it only with the user's go.  Build first: simcache/build_ios.sh && python3 -I simcache/make_payloads.py
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   txn_simcache.sh KERNEL_IMG TAG                         install (VARIANT=simcache1; also simcache1-norestart, simcache1-dryrun)
#   VARIANT=simcache1-off|simcache1-on|simcache1-sw-norestart|simcache1-sw-keepkey txn_simcache.sh KERNEL_IMG TAG   switches only
#   UNDO=1 txn_simcache.sh KERNEL_IMG TAG                  remove job, binary, state dir
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/sim-20261115/simcache; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_simcache.sh KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600"; exit 2; }
P=${VARIANT:-simcache1}
case $P in
  simcache1|simcache1-norestart|simcache1-dryrun|simcache1-off|simcache1-on|simcache1-sw-norestart|simcache1-sw-keepkey) G=$S/guest-simcache1.sh; OK='SIMCACHE1_OK' ;;
  *) echo "bad VARIANT $P"; exit 2 ;;
esac
O=$S/owned-simcache1.json
if [ "${UNDO:-0}" = 1 ]; then P=simcache1-undo; G=$S/guest-simcache1-undo.sh; OK='SIMCACHE1_UNDONE'; fi
[ -f $S/payload-$P/plan.json ] || { echo "no $S/payload-$P: run make_payloads.py"; exit 2; }
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start $P; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-$P --guest $G --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a -q "_REFUSED\|Traceback\|AssertionError" $X/offline/vm.log && { log "guest refused or failed: see offline/vm.log; nothing committed"; exit 3; }
grep -a -q "$OK" $X/offline/vm.log || { log "no $OK in offline/vm.log; nothing committed"; exit 3; }
grep -a "SIMCACHE1" $X/offline/vm.log | cut -c1-240 | tee -a $X/events
cp $O $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
