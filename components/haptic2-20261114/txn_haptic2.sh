#!/bin/bash
# NOT EXECUTED BY ITS AUTHOR. haptic2-20261114: flash a 0611 kernel (q10hapd default / q10haps strong / q10hapw soft) and capture the
# USB log.  Kernel only: no guest payload (haptic1 + hapticpref from final3 are already on the phone and stay as they are).
# Optional PULL_ODM=1 first boots the tested RAM rescue and streams the Android odm logical partition READ-ONLY out of super
# (tools/nord2-read-vendor.py: no phone writes, no mounts), then takes /firmware/aw8697_haptic_170.bin out of it on the host
# (erofs_get.py) for the vendor-waveform build (make_vendor_header.py -> 0612 vendor mode).  It writes the boot partition: run it only
# with the user's go.
# usage (phone already in fastboot via `phone.py reset fastboot`, or in iOS: to_fastboot.sh waits for it):
#   txn_haptic2.sh KERNEL_IMG TAG
#   PULL_ODM=1 txn_haptic2.sh KERNEL_IMG TAG
#   READS=N  seconds of USB log after the flash (default and maximum 600)
set -e
trap 'echo "$(date -Is) FAILED at line $LINENO" | tee -a $X/events' ERR
W=$NORD2_WORK
B=$W/gpu/build; S=$B/haptic2-20261114; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; IMG=$1; X=$B/cc-txn-$2
[ -n "$2" ] && [ -f "$IMG" ] || { echo "usage: txn_haptic2.sh KERNEL_IMG TAG"; exit 2; }
[ ! -e "$X" ] || { echo "$X exists: pick a new TAG"; exit 2; }
READS=${READS:-600}; [ "$READS" -gt 0 ] && [ "$READS" -le 600 ] || { echo "READS must be 1..600"; exit 2; }
mkdir -p $X
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
log start haptic2 $(sha256sum $IMG | cut -c1-16) pull_odm=${PULL_ODM:-0}
$D/to_fastboot.sh ${FB_WAIT:-1800}
if [ "${PULL_ODM:-0}" = 1 ]; then
  $D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
  for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; log rescue up
  SERIAL=$(timeout 3 $A devices | awk '$2=="recovery"{print $1; exit}')
  python3 $MAIN/tools/nord2-read-vendor.py --adb $A --serial "$SERIAL" --partition odm --out $X/odm > $X/odm.log 2>&1 && log odm read \
    || log "odm read FAILED (see odm.log); continuing with the kernel flash"
  if [ -f $X/odm/odm.img ]; then
    python3 -I $S/erofs_get.py $X/odm/odm.img --find aw8697_haptic_170.bin > $X/odm-find.txt 2>&1 || true
    P=$(head -1 $X/odm-find.txt); log "aw8697_haptic_170.bin in odm: ${P:-not found}"
    [ -n "$P" ] && [ "${P#/}" != "$P" ] && python3 -I $S/erofs_get.py $X/odm/odm.img --get "$P" $X/aw8697_haptic_170.bin >> $X/events 2>&1 \
      && python3 -I $S/make_vendor_header.py $X/aw8697_haptic_170.bin $X/nord2_haptic_vendor.h > $X/vendor-analysis.txt 2>&1 || true
    [ -f $X/vendor-analysis.txt ] && tee -a $X/events < $X/vendor-analysis.txt
  fi
  $D/to_fastboot.sh 300
fi
$D/flash_boot.sh $IMG; log kernel flashed $(sha256sum $IMG | cut -c1-16)
mkdir -p $D/runs/$2; python3 $W/tools/nord2-usb-console.py $D/runs/$2/ui-usb --wait 240 --read-seconds $READS > $D/runs/$2/usb.log 2>&1 &
CP=$!; for i in $(seq 1 $(( READS / 3 + 100 ))); do sleep 3; kill -0 $CP 2>/dev/null || break; done; wait $CP 2>/dev/null || true
log capture done
grep -a 'NORD2 HAPTIC' $D/runs/$2/ui-usb/xnu-live.log | cut -c1-300 > $X/haptic-lines.txt || true
log "haptic lines: $(wc -l < $X/haptic-lines.txt) (RAM: $(grep -c 'HAPTIC RAM' $X/haptic-lines.txt), clicks: $(grep -c 'click (iOS)' $X/haptic-lines.txt))"
