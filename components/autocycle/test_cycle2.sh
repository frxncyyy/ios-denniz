#!/bin/bash
# flash a TEST image, then capture the XNU USB log in passes (re-attaching after USB errors) until the 7-minute reset
W=$NORD2_WORK
D=$W/gpu/build/autocycle; IMG=$1; TAG=$2; R=$D/runs/$TAG; mkdir -p $R/ui-usb
$D/to_fastboot.sh ${FB_WAIT:-1800} > $R/fb.log 2>&1 || { echo "no fastboot" >> $R/events; exit 1; }
$D/flash_boot.sh "$IMG" >> $R/fb.log 2>&1 || { echo FLASHFAILED >> $R/events; exit 1; }
echo "$(date -Is) booted $TAG" >> $R/events
start=$(date +%s)
for i in $(seq 1 20); do
  [ $(( $(date +%s) - start )) -gt 470 ] && break
  python3 $W/tools/nord2-usb-console.py $R/ui-usb/p$i --wait 60 --read-seconds 500 > $R/usb$i.log 2>&1
  echo "$(date -Is) pass $i: $(tail -1 $R/usb$i.log)" >> $R/events
  timeout 3 $HOME/Android/Sdk/platform-tools/fastboot devices | grep -q fastboot && break
done
cat $R/ui-usb/p*/xnu-live.log > $R/ui-usb/xnu-live.log 2>/dev/null
echo "$(date -Is) cycle done" >> $R/events
