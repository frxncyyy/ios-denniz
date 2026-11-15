#!/bin/bash
# read-only: rescue -> copy the MTK camera libraries out of the vendor partition (never writes the phone's storage)
W=$NORD2_WORK
MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$W/gpu/build/autocycle; X=$W/gpu/build/cam-vendor-20261109; mkdir -p $X
$D/to_fastboot.sh 1800 > $X/fb.log 2>&1 || exit 1
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img >> $X/fb.log 2>&1
for i in $(seq 1 60); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done
$A shell 'ls /dev/block/mapper 2>&1; ls -l /dev/block/by-name | grep -iE "super|vendor"; cat /proc/mounts | grep -iE "vendor|system"' > $X/blocks.txt 2>&1
echo READY > $X/READY
