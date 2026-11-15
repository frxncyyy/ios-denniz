#!/bin/bash
# usage: run_cycle.sh IMG TAG  -> flash, boot, capture XNU USB log, wait until the phone is back in fastboot
W=$NORD2_WORK
D=$W/gpu/build/autocycle; IMG=$1; TAG=$2; R=$D/runs/$TAG
mkdir -p $R
$D/to_fastboot.sh ${FB_WAIT:-600} || exit 1
$D/flash_boot.sh "$IMG" || exit 1
echo "$(date -Is) booted $TAG" >> $R/events
python3 $W/tools/nord2-usb-console.py $R/ui-usb --wait 240 --read-seconds 600 > $R/usb-capture.log 2>&1
echo "$(date -Is) capture ended: $(tail -1 $R/usb-capture.log)" >> $R/events
$D/to_fastboot.sh 900 >> $R/events 2>&1
echo "$(date -Is) cycle done" >> $R/events
grep -a "NORD2 \(MODEM\|AUTOFB\)" $R/ui-usb/xnu-live.log | grep -av "early\|late " > $R/modem.txt
