#!/bin/bash
# restore pristine partitions, then boot the modem with the ORIGINAL vendor daemons under the tracer (reference trace from a clean state)
ADB=$HOME/Android/Sdk/platform-tools/adb
cd "$(dirname "$0")"
$ADB shell 'for d in /proc/[0-9]*; do c=$(cat $d/comm 2>/dev/null); case $c in ccci_mdinit|nvram_daemon|ccci_rpcd) kill -9 $(basename $d);; esac; done; sleep 3; sync; for m in nvdata nvcfg protect_f protect_s persist; do umount /mnt/vendor/$m 2>/dev/null; done'
for p in nvdata nvcfg protect1 protect2 persist; do
  $ADB push partitions/$p.img /tmp/$p.img >/dev/null 2>&1
  $ADB shell "dd if=/tmp/$p.img of=/dev/block/by-name/$p bs=1048576 2>/dev/null; sync; rm /tmp/$p.img"
done
$ADB shell 'for m in "nvdata nvdata" "nvcfg nvcfg" "protect1 protect_f" "protect2 protect_s" "persist persist"; do set -- $m; mount -t ext4 -o rw,nosuid,nodev,noatime /dev/block/by-name/$1 /mnt/vendor/$2; done; mkdir -p /data/vendor_de/md /data/vendor/mdlpm /data/nvram /data/vendor/nvram
rm -f /tmp/ccci_trace.log
(setsid nohup env LD_LIBRARY_PATH=/vendor/lib64:/vendor/vndk31 /vendor/bin/nvram_daemon > /tmp/nvram_daemon.out 2>&1 &)
sleep 6
(setsid nohup env LD_PRELOAD=/vendor/vndk31/libtrace.so LD_LIBRARY_PATH=/vendor/lib64:/vendor/vndk31 /vendor/bin/ccci_rpcd 0 > /tmp/rpcd.out 2>&1 &)
(setsid nohup env LD_PRELOAD=/vendor/vndk31/libtrace.so LD_LIBRARY_PATH=/vendor/lib64:/vendor/vndk31 /vendor/bin/ccci_mdinit 0 > /tmp/mdinit.out 2>&1 &)
for i in $(seq 1 24); do sleep 5; s=$(cat /sys/kernel/ccci/boot | cut -d"|" -f1); echo "t=$((i*5)) $s"; case "$s" in "md1:4 "|"md1:5 ") sleep 6; break;; esac; done'
$ADB pull /tmp/ccci_trace.log ccci_trace_pristine.log >/dev/null 2>&1
ls -la ccci_trace_pristine.log
