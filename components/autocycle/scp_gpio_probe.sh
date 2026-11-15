#!/bin/bash
# read-only: rescue -> GPIO modes as Android's kernel sets them (mt_gpio), pinctrl debug
W=$NORD2_WORK
MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$W/gpu/build/autocycle; X=$W/gpu/build/scp-probe-20261108
until grep -q 'cycle done' $D/runs/q10ew/events 2>/dev/null; do sleep 10; done
$D/to_fastboot.sh 1800 > $X/fb2.log 2>&1 || exit 1
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img >> $X/fb2.log 2>&1
for i in $(seq 1 60); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done
sleep 5
$A shell 'for f in $(find /sys -name mt_gpio 2>/dev/null | head -3); do echo "== $f"; cat $f; done' > $X/mt_gpio.txt 2>&1
$A shell 'ls /sys/kernel/debug/pinctrl/ 2>/dev/null; cat /sys/kernel/debug/pinctrl/*/pinmux-pins 2>/dev/null | head -400' > $X/pinmux.txt 2>&1
$A shell 'dmesg | grep -i -E "tcs37|alsps|ps_|als_|eint" | tail -60' > $X/dmesg-als.txt 2>&1
echo done > $X/DONE2
