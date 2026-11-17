#!/bin/bash
# Read-only: flash rescue (fastboot), then adb read of the MCUPM-related firmware partitions. Writes nothing to the phone.
set -u
cd $NORD2_WORK
E=gpu/build/hardware-20261025/mcupm-dump; FB=$HOME/Android/Sdk/platform-tools/fastboot; ADB=$HOME/Android/Sdk/platform-tools/adb
echo "waiting for fastboot $(date +%T)"; for i in $(seq 1 400); do $FB devices | grep -q fastboot && break; sleep 3; done
python3 $E/flash.py rescue 2>&1 | tail -1
for i in $(seq 1 60); do s=$($ADB devices | awk 'NR>1&&$2{print $2}'); [ "$s" = recovery ] && break; sleep 3; done; echo "adb: $s $(date +%T)"
[ "$s" = recovery ] || exit 1
mkdir -p $E/parts
$ADB shell 'for f in /sys/class/block/*/uevent; do d=${f%/uevent}; n=$(grep ^PARTNAME= $f | cut -d= -f2); [ -n "$n" ] && echo "$(basename $d) $n $(cat $d/size)"; done' > $E/partitions.txt 2>&1
wc -l $E/partitions.txt
grep -E -i " (mcupm|sspm|spmfw|scp|tee|sec1)[_a-z0-9]* " $E/partitions.txt | tee $E/wanted.txt
while read dev name sectors; do
  case "$name" in mcupm*|sspm*|spmfw*) ;; *) continue;; esac
  bytes=$((sectors*512)); echo "dump $name ($dev, $bytes bytes)"
  $ADB exec-out "dd if=/dev/block/$dev bs=1048576 count=$(( (bytes+1048575)/1048576 )) 2>/dev/null" > $E/parts/$name.img
  ls -l $E/parts/$name.img | awk '{print $5}'
done < $E/wanted.txt
echo "dump done $(date +%T)"
