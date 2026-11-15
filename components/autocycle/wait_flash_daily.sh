#!/bin/bash
# usage: wait_flash_daily.sh TAG IMG [SECONDS] -- wait for the user to put the phone in fastboot by hand (a DAILY build
# does not go there by itself), flash IMG, then capture the kernel's USB log for SECONDS while it boots and is used.
W=$NORD2_WORK
D=$W/gpu/build/autocycle; TAG=$1; IMG=$2; T=${3:-1800}
F=$HOME/Android/Sdk/platform-tools/fastboot
R=$D/runs/$TAG
mkdir -p $R
echo "waiting for fastboot (up to 4 h)" > $R/events
for ((t=0; t<14400; t+=5)); do
  timeout 5 $F devices 2>/dev/null | grep -q fastboot && break
  sleep 5
done
timeout 5 $F devices 2>/dev/null | grep -q fastboot || { echo "TIMEOUT: no fastboot" >> $R/events; echo timeout > $R/DONE; exit 1; }
echo "$(date -Is) fastboot seen" >> $R/events
$D/flash_boot.sh "$IMG" || { echo "FLASHFAILED" >> $R/events; echo flashfailed > $R/DONE; exit 1; }
echo "$(date -Is) booted $TAG" >> $R/events
python3 $W/tools/nord2-usb-console.py $R/ui-usb --wait 240 --read-seconds $T > $R/usb-capture.log 2>&1
echo "$(date -Is) capture ended: $(tail -1 $R/usb-capture.log)" >> $R/events
echo "$(date -Is) cycle done" >> $R/events
echo done > $R/DONE
