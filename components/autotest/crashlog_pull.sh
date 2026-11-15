#!/bin/bash
# After a crash (the phone sits in LK fastboot): fetch what the kernel kept in the ramoops zones, then put the phone back.
#   crashlog_pull.sh NAME [reboot]
# Needs a kernel with patch 0301 for the interesting part: console-ramoops-0 = the newest 256 KiB of the kernel log before the reset.
# Steps: remember the image that was on the phone (last non-rescue line of autocycle/flash.log), flash the ADB RAM rescue to boot
# and start it, copy /sys/fs/pstore/* to runs/NAME/crash/, back to fastboot, flash the remembered image again; the phone stays in
# fastboot unless "reboot" is given. Run it BEFORE anything else boots: the rescue's kernel empties the zones when it has read them
# (one chance), and a boot of iOS starts a new log in them.
set -u
W=$NORD2_WORK
M=$NORD2_REPO
D=$W/gpu/build/autocycle; ADB=$HOME/Android/Sdk/platform-tools/adb; FB=$HOME/Android/Sdk/platform-tools/fastboot
N=${1:?usage: crashlog_pull.sh NAME [reboot]}; R=$W/gpu/build/autotest/runs/$N/crash; mkdir -p "$R"
RESCUE=$M/research/nord2/rescue/nord2-adb-rescue.img
PREV=$(grep -v "nord2-adb-rescue\|/salvage" $D/flash.log | tail -1 | awk '{print $5}')
[ -f "$PREV" ] || { echo "no previous image found in flash.log"; exit 1; }
timeout 5 $FB devices | grep -q fastboot || { echo "the phone is not in fastboot (if iOS is up, nothing crashed: use phone.py logs)"; exit 2; }
echo "previous image: $PREV"
$D/flash_boot.sh "$RESCUE" | tail -1
for i in $(seq 1 50); do sleep 3; timeout 3 $ADB devices | grep -q "recovery$" && break; done
timeout 3 $ADB devices | grep -q "recovery$" || { echo "the rescue did not come up; boot still holds the rescue image"; exit 3; }
for f in $($ADB shell -T 'ls /sys/fs/pstore/' | tr -d '\r'); do $ADB shell -T "cat /sys/fs/pstore/$f" > "$R/$f"; done
$ADB shell -T 'cat /proc/cmdline' > "$R/rescue-cmdline.txt"
ls -la "$R" | tail -n +2
$D/to_fastboot.sh 180 || { echo "not back in fastboot; boot still holds the rescue image"; exit 4; }
if [ "${2:-}" = reboot ]; then $D/flash_boot.sh "$PREV" | tail -1; else $D/flash_boot.sh "$PREV" noreboot | tail -1; echo "phone left in fastboot with the previous image in boot"; fi
if [ -s "$R/console-ramoops-0" ]; then
  tr -d '\000' < "$R/console-ramoops-0" > "$R/last-log.txt"
  echo "last log: $R/last-log.txt ($(wc -c < "$R/last-log.txt") bytes); its end:"; tail -15 "$R/last-log.txt" | cut -c1-200
  grep -a -n -i -E "panic\(|panic:|NORD2 HANG|watchdog|SWEET SURF: refused|THERM|kernel_trap|Abort|stackshot" "$R/last-log.txt" | tail -12 | cut -c1-200
else
  echo "no console-ramoops-0: the kernel that crashed had no patch 0301 (or DRAM was lost); pmsg-ramoops-0 has only the first 64 KiB of that boot"
fi
