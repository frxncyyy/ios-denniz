#!/bin/bash
# n2_read_device.sh OUTDIR  --  READ-ONLY dump of THE CONNECTED Nord 2's own per-device data, taken from the ADB rescue.
# Run on the phone owner's computer, with exactly one phone in the rescue (adb state "recovery").  It never writes the phone:
#   - every partition used is first set read-only at block level (blockdev --setro), ext4 is mounted -o ro,noload (no journal
#     replay), raw reads are dd if=... only;
#   - OUTDIR (mode 700) receives: tree/{nvdata,protect_f,protect_s}/md (modem NVRAM files, incl. IMEI + RF calibration),
#     wifi/WIFI (Wi-Fi factory calibration + MAC), oplus_custom.bin (sensor calibration), gpt/lun-lba0-5.bin + gpt/lun-blocks.txt,
#     super-head.bin (LP geometry + metadata), partitions.sha256 (stock firmware partitions), receipt.txt.
# OUTDIR holds this phone's identities: keep it private, never share it, delete it after the install if you like.
#   env: ADB=/path/to/adb (default: adb in PATH)
set -euo pipefail
A=${ADB:-adb}
O=${1:?usage: n2_read_device.sh OUTDIR}
[ ! -e "$O" ] || { echo "$O exists: use a new directory"; exit 2; }
[ "$(timeout 5 $A devices | grep -c 'recovery$')" = 1 ] || { echo "need exactly one phone in the adb rescue (adb devices -> recovery)"; exit 1; }
sh_() { timeout "${T:-60}" $A shell "$@" | tr -d '\r'; }
umask 077; mkdir -p "$O"/tree "$O"/wifi "$O"/gpt; chmod 700 "$O"
P=/dev/block/by-name
trap 'sh_ "for p in nvdata protect1 protect2; do umount /tmp/n2ro/\$p 2>/dev/null; done" >/dev/null 2>&1 || true' EXIT
for part in nvdata protect1 protect2 oplus_custom super boot recovery userdata; do
  sh_ "[ -e $P/$part ]" || { echo "partition $part not found: is this a Nord 2 (oplus6893)?"; exit 3; }
done
sh_ "getprop ro.product.device; getprop ro.boot.prjname" > "$O/device.txt" || true
for pair in nvdata:nvdata protect1:protect_f protect2:protect_s; do
  part=${pair%%:*}; dst=${pair#*:}
  sh_ "blockdev --setro \$(readlink -f $P/$part); grep -q ' /tmp/n2ro/$part ' /proc/mounts || { mkdir -p /tmp/n2ro/$part && mount -t ext4 -o ro,noload $P/$part /tmp/n2ro/$part; }" ||
    { echo "cannot mount $part read-only"; exit 4; }
  mkdir -p "$O/tree/$dst"
  T=300 timeout 300 $A exec-out "tar -C /tmp/n2ro/$part -cf - md" | tar -C "$O/tree/$dst" -xf - || { echo "tar of $part failed"; exit 5; }
done
timeout 60 $A exec-out "cat /tmp/n2ro/nvdata/APCFG/APRDEB/WIFI" > "$O/wifi/WIFI"
[ "$(stat -c %s "$O/wifi/WIFI")" = 6146 ] || { echo "WIFI file missing or not 6146 bytes (nvdata/APCFG/APRDEB/WIFI)"; exit 6; }
for raw in oplus_custom super; do sh_ "blockdev --setro \$(readlink -f $P/$raw)"; done
timeout 60 $A exec-out "dd if=$P/oplus_custom bs=4096 count=1 2>/dev/null" > "$O/oplus_custom.bin"
timeout 60 $A exec-out "dd if=$P/super bs=4096 count=64 2>/dev/null" > "$O/super-head.bin"
DISK=$(sh_ "readlink -f $P/userdata" | sed 's/[0-9]*$//')
[ -n "$DISK" ] || { echo "cannot resolve the userdata disk"; exit 7; }
BS=$(sh_ "cat /sys/block/${DISK##*/}/queue/logical_block_size"); SECT=$(sh_ "cat /sys/block/${DISK##*/}/size")
[ "$BS" = 4096 ] || { echo "unexpected logical block size $BS"; exit 8; }
echo "$(( SECT / 8 )) 4096-byte blocks on ${DISK##*/}" > "$O/gpt/lun-blocks.txt"
timeout 60 $A exec-out "dd if=$DISK bs=4096 count=6 2>/dev/null" > "$O/gpt/lun-lba0-5.bin"
: > "$O/partitions.sha256"
for part in boot recovery vendor_boot dtbo md1img lk; do
  h=$(T=300 sh_ "[ -e $P/$part ] && sha256sum $P/$part" | cut -c1-64) || true
  [ -n "$h" ] && echo "$part $h" >> "$O/partitions.sha256"
done
for part in nvdata protect1 protect2; do echo "$part-blockdev $(T=300 sh_ "sha256sum $P/$part" | cut -c1-64)" >> "$O/partitions.sha256"; done
{ echo "n2_read_device $(date -Is) read-only"; ( cd "$O" && find . -type f ! -name receipt.txt -print0 | sort -z | xargs -0 sha256sum ) | awk '{n++} END {print n" files hashed"}'; } > "$O/receipt.txt"
find "$O" -type f -print0 | xargs -0 chmod 600
echo "DUMP_OK $O ($(find "$O/tree" -type f | wc -l) modem files; private: do not share)"
