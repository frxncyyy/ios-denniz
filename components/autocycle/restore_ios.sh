#!/bin/bash
set -e
W=$NORD2_WORK
B=$W/gpu/build; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; F=$HOME/Android/Sdk/platform-tools/fastboot; D=$B/autocycle; X=$B/IOS-BACKUP-20261106
$A reboot bootloader || true
for i in $(seq 1 60); do timeout 3 $F devices | grep -q fastboot && break; sleep 2; done
$D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 60); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; echo rescue up
U=$($A shell readlink -f /dev/block/by-name/userdata | tr -d '\r'); echo userdata=$U; [ "$U" = /dev/block/sdc57 ]
echo "writing 16 GiB rootfs"; date +%T
$A exec-in "dd of=/dev/block/by-name/userdata bs=4194304 conv=fsync" < $B/mirror/rootfs-mirror.img
date +%T; echo "writing NVRAM blob region"
$A exec-in "dd of=/dev/block/by-name/userdata bs=1048576 seek=65536 conv=fsync" < $X/userdata-64GiB-64MiB.bin
echo verify; $A shell 'dd if=/dev/block/by-name/userdata bs=1048576 count=64 status=none | sha256sum'; dd if=$B/mirror/rootfs-mirror.img bs=1048576 count=64 status=none | sha256sum
$A shell 'dd if=/dev/block/by-name/userdata bs=1048576 skip=65536 count=64 status=none | sha256sum'; cat $X/nv.sha256
echo RESTORE_DONE
