#!/bin/bash
# Read-only validation of the (grown-volume capable) snapshot tool on the real phone: rescue -> snapshot -> flash KERNEL_IMG back.
# Writes NO phone partition (the tool proves it: userdata write/discard counters + boot id identical before/after, 'phone_partition_writes': 0).
# It DOES refresh the host mirror ($B/mirror/rootfs-mirror.img + manifest), as every transaction's snapshot does.
# usage (phone in fastboot via `phone.py reset fastboot`):  snapshot_only.sh TAG [KERNEL_IMG]      (default KERNEL_IMG = q10jcz)
#   READS=N (1..600): also capture N s of USB log after the flash (default 0 = no capture)
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; X=$B/cc-txn-$1
IMG=${2:-$(ls $B/device-mmu-q10jcz-candidate/ui-sched/*.img)}
[ -n "$1" ] && [ -f "$IMG" ] || { echo "usage: snapshot_only.sh TAG [KERNEL_IMG]"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-0}; [ "$READS" -ge 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 0..600"; exit 2; }
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start snapshot-only; $D/to_fastboot.sh ${FB_WAIT:-1800}
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
if python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; then
  log "SNAPSHOT_OK $(python3 -c "
import json;d=json.load(open('$X/snapshot/verified.json'))
print('bytes',d['bytes'],'grown',d.get('volume_grown',False),'verified',d.get('verified_chunks',d['bytes']//(16<<20)),'unverified',len(d.get('unverified_chunks',[])),
      'changed_mib',d['changed_mib'],'downloaded',d['downloaded_bytes'],'resized',d.get('mirror_resized'),'writes',d['phone_partition_writes'],'s',d['timings']['total_s'])")"
else
  log "SNAPSHOT_FAILED (read-only: nothing written on the phone); see $X/snapshot.log"
fi
$D/to_fastboot.sh 300; $D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
if [ "$READS" -gt 0 ]; then
  mkdir -p $D/runs/$1; python3 $W/tools/nord2-usb-console.py $D/runs/$1/ui-usb --wait 240 --read-seconds $READS > $D/runs/$1/usb.log 2>&1 &
  CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
  log capture done
fi
