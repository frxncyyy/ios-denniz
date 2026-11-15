#!/bin/bash
# usage: flash_boot.sh IMG [noreboot]   -- LK fastboot or fastbootd, boot partition only, logs sha256
F=$HOME/Android/Sdk/platform-tools/fastboot
D=$(dirname "$0"); IMG=$1
H=$(sha256sum "$IMG" | cut -c1-64)
S=$(stat -c %s "$IMG"); [ "$S" -le 33554432 ] || { echo too big; exit 1; }
$F devices | grep -q fastboot || { echo "no fastboot device"; exit 2; }
$F getvar product 2>&1 | grep -q "product: oplus6893" || { echo wrong product; exit 3; }
echo "$(date -Is) flash boot $H $IMG" >> $D/flash.log
$F flash boot "$IMG" 2>&1 | tail -2
[ "$2" = noreboot ] || $F reboot 2>&1 | tail -1
