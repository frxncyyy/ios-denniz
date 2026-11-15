#!/bin/bash
set -e
W=$NORD2_WORK
B=$W/gpu/build; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle
NV=/tmp/claude-1000/-home-user-Documents-Codex-2026-10-19-vedi-questa-repo-https-github-com-work-ios-sweet-research-nord2-gpu-overnight-20261024-work/7eab3ca4-47cd-4507-982a-63f3d778de8f/scratchpad/nvbuild/n2nvdisk-android-20261106.bin
$D/to_fastboot.sh 900; $D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 60); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; echo rescue up
[ "$($A shell readlink -f /dev/block/by-name/userdata | tr -d '\r')" = /dev/block/sdc57 ]
$A push $NV /tmp/n2nvdisk.bin
$A shell 'dd if=/tmp/n2nvdisk.bin of=/dev/block/by-name/userdata bs=1048576 seek=65536 count=4 conv=notrunc,fsync && sync'
$A shell 'dd if=/dev/block/by-name/userdata bs=1048576 skip=65536 count=4 2>/dev/null | sha256sum'; sha256sum $NV
echo NV_DONE
