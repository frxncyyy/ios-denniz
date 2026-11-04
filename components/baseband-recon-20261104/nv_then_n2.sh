#!/bin/bash
# pristine partitions -> vendor nvram_daemon alone (prepares the NVRAM) -> our n2mdinit
ADB=$HOME/Android/Sdk/platform-tools/adb
cd "$(dirname "$0")"
$ADB push n2mdinit/n2mdinit /tmp/n2mdinit >/dev/null 2>&1; $ADB push n2mdinit/run.sh /tmp/run.sh >/dev/null 2>&1
$ADB shell 'for d in /proc/[0-9]*; do c=$(cat $d/comm 2>/dev/null); case $c in ccci_mdinit|nvram_daemon|ccci_rpcd) kill -9 $(basename $d);; esac; done; sleep 3; sync; for m in nvdata nvcfg protect_f protect_s persist; do umount /mnt/vendor/$m 2>/dev/null; done'
for p in nvdata nvcfg protect1 protect2 persist; do
  $ADB push partitions/$p.img /tmp/$p.img >/dev/null 2>&1
  $ADB shell "dd if=/tmp/$p.img of=/dev/block/by-name/$p bs=1048576 2>/dev/null; sync; rm /tmp/$p.img"
done
$ADB shell 'for m in "nvdata nvdata" "nvcfg nvcfg" "protect1 protect_f" "protect2 protect_s" "persist persist"; do set -- $m; mount -t ext4 -o rw,nosuid,nodev,noatime /dev/block/by-name/$1 /mnt/vendor/$2; done; mkdir -p /data/vendor_de/md /data/vendor/mdlpm /data/nvram /data/vendor/nvram
(setsid nohup env LD_LIBRARY_PATH=/vendor/lib64:/vendor/vndk31 /vendor/bin/nvram_daemon > /tmp/nvram_daemon.out 2>&1 &)
sleep 10; for d in /proc/[0-9]*; do c=$(cat $d/comm 2>/dev/null); [ "$c" = nvram_daemon ] && echo nvram_daemon still running; done; sync
sh /tmp/run.sh'
