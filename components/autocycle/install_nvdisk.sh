#!/bin/bash
# Boot the rescue, write n2nvdisk.bin raw at userdata+64GiB (unused; iOS root HFSX is the first 16 GiB), verify, return to fastboot.
D=$(dirname "$(readlink -f "$0")"); A=$HOME/Android/Sdk/platform-tools/adb
EXP=$(sha256sum $D/n2nvdisk.bin | cut -c1-64)
$D/to_fastboot.sh 600 || exit 1
$D/flash_boot.sh $NORD2_REPO/research/nord2/rescue/nord2-adb-rescue.img || exit 1
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done
timeout 30 $A push $D/n2nvdisk.bin /tmp/n2nvdisk.bin || exit 2
timeout 60 $A shell 'grep -q " /data " /proc/mounts && { echo userdata mounted; exit 1; }; dd if=/tmp/n2nvdisk.bin of=/dev/block/by-name/userdata bs=1048576 seek=65536 count=4 conv=notrunc,fsync && sync' || exit 3
GOT=$(timeout 60 $A shell 'dd if=/dev/block/by-name/userdata bs=1048576 skip=65536 count=4 2>/dev/null | sha256sum' | cut -c1-64)
echo "expected $EXP got $GOT"; [ "$EXP" = "$GOT" ] || exit 4
echo "$(date -Is) nvdisk written userdata+64GiB $EXP" >> $D/flash.log
$D/to_fastboot.sh 120
