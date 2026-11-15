#!/bin/bash
# read-only: rescue -> SCP facts (reserved-memory, iomem, dmesg, scp partition copy). Never writes the phone's storage.
W=$NORD2_WORK
MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$W/gpu/build/autocycle; X=$W/gpu/build/scp-probe-20261108; mkdir -p $X
$D/to_fastboot.sh 14400 > $X/fb.log 2>&1 || exit 1
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img >> $X/fb.log 2>&1
for i in $(seq 1 60); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done
$A shell 'for d in /sys/firmware/devicetree/base/reserved-memory/*; do n=$(basename $d); r=$(od -An -tx4 -v $d/reg 2>/dev/null | tr -s " " | tr "\n" " "); s=$(od -An -tx4 -v $d/size 2>/dev/null|tr -s " "|tr "\n" " "); echo "$n reg[$r] size[$s]"; done' > $X/reserved.txt 2>&1
$A shell 'cat /proc/iomem' > $X/iomem.txt 2>&1
$A shell 'dmesg | grep -i -E "scp|sensorhub|nanohub|reserve-memory|reserved mem" | head -300' > $X/dmesg-scp.txt 2>&1
$A shell 'ls -l /dev/block/by-name/ | grep -i -E "scp|sspm|tee|md1"' > $X/parts.txt 2>&1
$A shell 'cat /proc/cmdline; uname -a' > $X/cmdline.txt 2>&1
for p in scp_a scp_b; do $A exec-out "dd if=/dev/block/by-name/$p bs=1M 2>/dev/null" > $X/$p.img; done
ls -la $X > $X/ls.txt
echo done > $X/DONE
