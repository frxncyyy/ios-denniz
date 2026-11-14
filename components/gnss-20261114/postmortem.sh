#!/bin/bash
# gnss-20261114/postmortem.sh (0591): after a hard hang + WDT reset, read what the MediaTek preloader/LK latched for the PREVIOUS boot
# (per-CPU last PC, last bus, WDT status, exception type: the AEE ram_console in SRAM, shown by the stock kernel in
# /proc/aed/reboot-reason) plus the 0583 trace, in the RESCUE (stock Android kernel), then symbolise the last PCs against the iOS kernel
# using the trace's 'kaddr' line (0591).  Read-only on the phone.  The rescue must be entered DIRECTLY from the fastboot that follows
# the WDT reset (no extra reboot in between, or the latches describe that reboot instead).
#   postmortem.sh OUTDIR KERNEL_DEVELOPMENT_FILE          (phone already in the rescue)
#   ENTER=1 postmortem.sh OUTDIR KERNEL_DEVELOPMENT_FILE  (to_fastboot + flash rescue + adb, like sweetlog_tail.sh)
set -u
W=$NORD2_WORK
B=$W/gpu/build; D=$B/autocycle; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; H=$(cd "$(dirname "$0")" && pwd)
O=${1:?usage: postmortem.sh OUTDIR KERNEL_DEVELOPMENT_FILE}; K=${2:?kernel.development file of the image that hung}; mkdir -p "$O"
if [ "${ENTER:-0}" = 1 ]; then
  $D/to_fastboot.sh ${FB_WAIT:-1800}; $D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
  for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done
fi
timeout 5 $A devices | grep -q -E "recovery$|device$" || { echo "no adb device"; exit 2; }
$A shell 'for f in /proc/aed/reboot-reason /proc/last_kmsg /sys/fs/pstore/console-ramoops-0 /proc/bootprof; do echo "=== $f"; cat $f 2>&1 | head -400; done;
  echo "=== ls /proc/aed"; ls -la /proc/aed 2>&1; echo "=== props"; getprop ro.boot.bootreason; getprop sys.boot.reason; getprop ro.boot.wdt_reason 2>/dev/null;
  echo "=== /dev/mem"; ls -l /dev/mem 2>&1; echo "=== dmesg"; dmesg 2>/dev/null | grep -i -E "wdt|watchdog|last ?pc|lastbus|last_bus|bus_tracer|devapc|mpu|violation|reboot|exp_type|hang|plat_dbg|dfd" | head -120' > "$O/postmortem.txt" 2>&1
echo "postmortem text: $O/postmortem.txt ($(wc -l < "$O/postmortem.txt") lines)"
ENTER=0 bash "$H/sweetlog_tail.sh" "$O" 60 > "$O/sweetlog_tail.out" 2>&1; tail -25 "$O/sweetlog_tail.out"
python3 -I "$H/postmortem_sym.py" "$O/postmortem.txt" "$O"/console-ramoops-0.txt "$K"
