#!/bin/bash
# after the q10br run resets into fastboot: rescue + snapshot (mirror sync) + extract the CommCenter log, then boot q10br again
set -e
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; D=$B/autocycle; X=$B/cc-fetch-$1; mkdir -p $X
$D/to_fastboot.sh 1800; $D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done
python3 $T/snapshot_current_root_fast.py --main $MAIN --mirror $B/mirror/rootfs-mirror.img --output $X/snapshot --jobs 4 > $X/snapshot.log 2>&1
cd $X; 7z e -y -o$X $B/mirror/rootfs-mirror.img "iOSRoot/private/var/wireless/n2-cc.log" > $X/7z.log 2>&1 || true
cp -f $X/n2-cc.log $D/runs/$1/n2-cc.log 2>/dev/null || true
ls -la $X/n2-cc.log
