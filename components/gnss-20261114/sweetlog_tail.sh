#!/bin/bash
# gnss-20261114/sweetlog_tail.sh: after a hard hang, read the persistent GNSS trace (kernel 0583: ramoops console zone 0x4811F000,
# preserved by LK across the WDT reset) from the phone in the RESCUE (stock Android kernel + DT => pstore/ramoops active).
#   sweetlog_tail.sh OUTDIR [N_LINES]            phone already in the rescue (adb device in "recovery" state)
#   ENTER=1 sweetlog_tail.sh OUTDIR [N_LINES]    first enter the rescue exactly like the txn scripts (to_fastboot.sh, flash rescue, adb)
# Only reads the phone (pstore files; plus the userdata tail if it carries a "SWEETLOG" header). Writes only OUTDIR.
set -u
W=$NORD2_WORK
B=$W/gpu/build; D=$B/autocycle; MAIN=$NORD2_REPO
A=$HOME/Android/Sdk/platform-tools/adb; H=$(cd "$(dirname "$0")" && pwd)
O=${1:?usage: sweetlog_tail.sh OUTDIR [N_LINES]}; N=${2:-80}; mkdir -p "$O"
if [ "${ENTER:-0}" = 1 ]; then
  $D/to_fastboot.sh ${FB_WAIT:-1800}; $D/flash_boot.sh $MAIN/research/nord2/rescue/nord2-adb-rescue.img
  for i in $(seq 1 40); do sleep 3; timeout 3 $A devices | grep -q "recovery$" && break; done
fi
timeout 5 $A devices | grep -q -E "recovery$|device$" || { echo "no adb device (enter the rescue first, or ENTER=1)"; exit 2; }
$A shell 'mkdir -p /sys/fs/pstore 2>/dev/null; grep -q pstore /proc/mounts || mount -t pstore pstore /sys/fs/pstore 2>/dev/null; ls -la /sys/fs/pstore' > "$O/pstore-ls.txt" 2>&1
cat "$O/pstore-ls.txt"
for f in $(tr -d '\r' < "$O/pstore-ls.txt" | awk '{print $NF}' | grep -E '^(console|pmsg|dmesg)-ramoops'); do
  $A exec-out "cat /sys/fs/pstore/$f" > "$O/$f.txt" 2>/dev/null; echo "fetched $f ($(stat -c %s "$O/$f.txt") bytes)"
done
$A shell 'dmesg 2>/dev/null | grep -i -E "pstore|ramoops|persistent_ram" | head -20' > "$O/rescue-dmesg-pstore.txt" 2>&1; cat "$O/rescue-dmesg-pstore.txt"
# optional: SWEETLOG area (kernel 0047) at the end of userdata, only if its header is there
dev=$($A shell 'for d in /dev/block/by-name/userdata /dev/block/bootdevice/by-name/userdata; do [ -e $d ] && readlink -f $d && break; done' | tr -d '\r')
if [ -n "$dev" ]; then
  bytes=$($A shell blockdev --getsize64 "$dev" | tr -d '\r'); skip=$((bytes / 4096 - 1025))
  $A exec-out "dd if=$dev bs=4096 skip=$skip count=1 2>/dev/null" > "$O/sweetlog-hdr.bin"
  if head -c 8 "$O/sweetlog-hdr.bin" | grep -q SWEETLOG; then $A exec-out "dd if=$dev bs=4096 skip=$skip count=1025 2>/dev/null" > "$O/sweetlog.bin"; echo "SWEETLOG area fetched"; else echo "no SWEETLOG header at the userdata tail (expected: the q10jcz line has no persistent console)"; fi
fi
rc=1
for f in "$O"/console-ramoops*.txt; do [ -s "$f" ] || continue; echo "=== $f"; python3 -I "$H/sweetlog_tail.py" "$f" "$N" && rc=0; done
[ -s "$O/sweetlog.bin" ] && { echo "=== SWEETLOG"; python3 -I "$H/sweetlog_tail.py" "$O/sweetlog.bin" "$N"; }
exit $rc
