#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR ON THE PHONE (rehearsed end to end on a copy: storage-20261114/rehearse_pipeline.py). Grows the iOS root HFSX volume
# from 16 GiB to 28049408 blocks (107.0 GiB), leaving the last 32760 blocks (128 MiB, includes SWEETLOG) of userdata outside HFS, then flashes KERNEL_IMG.
# It writes the phone: run it only with the user's go.
# usage (phone already in fastboot via `phone.py reset fastboot`):
#   txn_grow.sh KERNEL_IMG TAG        KERNEL_IMG must be q10jcz (sha e2d151f8...) unless ALLOW_OTHER_KERNEL=1;  READS=1..600 (default 600)
# Steps: rescue -> snapshot (volume still 16 GiB: identical to the pre-grow tool) -> offline VM 'growprep' (journal replay + fsck -y + rw mount/umount:
# makes the volume clean with an empty journal, which commit_hfs_grow.py requires) -> catalog check (no record may change) -> verified delta commit
# -> grow plan -> grow commit (same rescue session; 3 verified batches, undo preimages in $X/grow/undo) -> post-grow snapshot (read-only; validates the
# grown-volume pipeline on the real phone and resizes the sparse host mirror; non-fatal) -> flash KERNEL_IMG -> USB capture (<= 600 s, after the flash).
# Exit 3 = refused before any grow write (kernel flashed back). Exit 4 = grow commit failed: the phone is LEFT IN RESCUE, see events.
# Exact undo (ONLY before iOS has booted on the grown volume, i.e. from the same rescue session, before the flash):
#   python3 $B/stock-20261114/commit_hfs_grow.py --main $MAIN --output $X/grow-undo --undo $X/grow
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; S=$B/storage-20261114; G=$B/stock-20261114; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_grow.sh KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
Z=$(ls $B/device-mmu-q10jcz-candidate/ui-sched/*.img); [ "$(sha256sum < "$IMG")" = "$(sha256sum < "$Z")" ] || [ "${ALLOW_OTHER_KERNEL:-0}" = 1 ] || { echo "KERNEL_IMG is not q10jcz ($Z); set ALLOW_OTHER_KERNEL=1 to override"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600 (nord2-usb-console.py limit)"; exit 2; }
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
flash_back(){ $D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16); }
log start grow; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; log snapshot done
if python3 -c "import json,sys;sys.exit(0 if json.load(open('$X/snapshot/verified.json'))['bytes']==16<<30 else 1)"; then :; else
  log "volume is not 16 GiB (already grown?): nothing to do, nothing written"; flash_back; exit 3; fi
python3 $B/apps-20261104/generic_update_offline.py --snapshot-receipt $X/snapshot/verified.json --payload $S/payload-growprep --guest $S/guest-growprep.sh --output $X/offline --initramfs $MAIN/research/nord2/rootfs-vm/initramfs > $X/offline.log 2>&1; log offline done
grep -a "GROWPREP_" $X/offline/vm.log | cut -c1-400 | tee -a $X/events
grep -a -q "GROWPREP_READY" $X/offline/vm.log || { log "growprep not READY: nothing committed"; flash_back; exit 3; }
cp $S/owned-growprep.json $X/owned.json
python3 $T/check_update_catalog_fast.py --hfsx-read $MAIN/research/nord2/display-capabilities/hfsx-read.py --snapshot-receipt $X/snapshot/verified.json --update $X/offline --owned-json $X/owned.json --output $X/catalog.json > $X/catalog.log 2>&1; log catalog ok
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed growprep delta
# grow plan: reads ~6 MiB, writes nothing; refuses unless the volume is clean, journal empty, 4194304 blocks
if ! python3 $G/commit_hfs_grow.py --main $MAIN --output $X/grow > $X/grow-plan.log 2>&1; then
  log "grow plan refused/failed (nothing written by the grow): $(grep -a 'HFSGROW_REFUSED\|Error\|Refusal' $X/grow-plan.log | tail -2)"; flash_back; exit 3; fi
grep -a -q '"new_blocks": 28049408' $X/grow-plan.log || { log "unexpected grow plan: $(grep -a HFSGROW_PLAN $X/grow-plan.log | cut -c1-300)"; flash_back; exit 3; }
log grow planned "$(grep -a HFSGROW_PLAN $X/grow-plan.log | cut -c1-200)"
if ! python3 $G/commit_hfs_grow.py --main $MAIN --output $X/grow --commit > $X/grow.log 2>&1; then
  log "GROW COMMIT FAILED: phone LEFT IN RESCUE. Inspect $X/grow.log; exact undo: python3 $G/commit_hfs_grow.py --main $MAIN --output $X/grow-undo --undo $X/grow"
  exit 4; fi
log grow committed "$(grep -a HFSGROW_COMMITTED $X/grow.log)"
# read-only validation of the grown-volume pipeline on the real phone (+ host mirror resized to the grown volume, sparse)
if python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot-grown --jobs 4 > $X/snapshot-grown.log 2>&1; then
  log "post-grow snapshot OK: $(python3 -c "import json;d=json.load(open('$X/snapshot-grown/verified.json'));print('bytes',d['bytes'],'verified',d.get('verified_chunks'),'unverified',len(d.get('unverified_chunks',[])),'downloaded',d['downloaded_bytes'],'writes',d['phone_partition_writes'])")"
else
  log "post-grow snapshot FAILED (non-fatal: the phone is grown and was not written by it; see snapshot-grown.log)"
fi
flash_back
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
