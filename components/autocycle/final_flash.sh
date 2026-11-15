#!/bin/bash
# 07:35 insurance: put the daily modem image on the phone unless FINAL_DONE already exists.
D=$(dirname "$(readlink -f "$0")"); W=$NORD2_WORK
while [ "$(date +%H%M)" -lt 0735 ]; do sleep 30; done
[ -f $D/FINAL_DONE ] && exit 0
touch $D/FINAL_LOCK
$D/to_fastboot.sh 900 || exit 1
$D/flash_boot.sh $(ls $D/q10bl-DAILY-image/*.img) || exit 2
date -Is > $D/FINAL_DONE
mkdir -p $D/runs/final; python3 $W/tools/nord2-usb-console.py $D/runs/final/ui-usb --wait 240 --read-seconds 400 > $D/runs/final/usb.log 2>&1
