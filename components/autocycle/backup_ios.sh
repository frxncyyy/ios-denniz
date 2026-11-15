#!/bin/bash
# backup_ios.sh: rescue -> refresh 16 GiB rootfs mirror (snapshot) -> dd 64 MiB at userdata+64GiB (modem NVRAM blob) -> copy mirror to a frozen backup
set -e
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; X=$B/IOS-BACKUP-20261106; mkdir -p $X
$D/to_fastboot.sh 1800; $D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done; echo rescue up
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1; echo snapshot done
$A shell 'dd if=/dev/block/by-name/userdata bs=1048576 skip=65536 count=64 status=none' > $X/userdata-64GiB-64MiB.bin
ls -la $X/userdata-64GiB-64MiB.bin; sha256sum $X/userdata-64GiB-64MiB.bin > $X/nv.sha256
$A shell 'readlink -f /dev/block/by-name/userdata; cat /proc/partitions | grep -E "sdc57|sdc$"; ls -la /dev/block/by-name/ | grep -E "super|boot|vendor_boot|userdata"' > $X/partinfo.txt 2>&1
echo BACKUP_DONE
