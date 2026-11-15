#!/bin/bash
# Boot-only cycle (no root change): phone in LK fastboot -> flash IMG to boot -> reboot -> wait for the autotest channel.
# usage: flash_q10at.sh IMG TAG       Logs the flash in autocycle/flash.log like every other cycle.
set -e
W=$NORD2_WORK
D=$W/gpu/build/autocycle; A=$W/gpu/build/autotest; IMG=$1; R=$A/runs/$2; mkdir -p $R
echo "$(date -Is) start $IMG" | tee -a $R/events
$D/to_fastboot.sh ${FB_WAIT:-120}
$D/flash_boot.sh $IMG | tee -a $R/events
echo "$(date -Is) flashed $(sha256sum $IMG | cut -c1-16)" | tee -a $R/events
python3 $A/phone.py wait 240 > $R/status.json 2>&1 && echo "$(date -Is) autotest channel up" | tee -a $R/events || { echo "$(date -Is) NO autotest channel"; cat $R/status.json; exit 1; }
