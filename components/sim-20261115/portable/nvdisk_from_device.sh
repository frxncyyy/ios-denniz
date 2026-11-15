#!/bin/bash
# sim-20261115 (item 3) -- UNTESTED on hardware, host side written only; run by the main session, never by an agent.
# Builds the modem NVRAM blob (N2NVDSK1, read by sweet_amfi.c 0258/0261 at userdata+64 GiB) from THE CONNECTED PHONE'S OWN nvdata,
# protect1 (protect_f) and protect2 (protect_s) partitions, mounted READ-ONLY from the Android rescue, and writes it back to the same
# phone. Replaces the current practice of writing this phone's n2nvdisk-android-20261106.bin / IOS-BACKUP region to any phone
# (which would give every other Nord 2 this phone's IMEI and RF calibration).
#   usage: nvdisk_from_device.sh OUTDIR [--write]      (phone already in the adb rescue: autocycle/install_nvdisk.sh shows how to get there)
# OUTDIR keeps only the blob + a receipt (sha256 of the blob and of the three partitions); the extracted tree (identities) is deleted.
set -euo pipefail
A=${ADB:-$HOME/Android/Sdk/platform-tools/adb}
B=$NORD2_WORK/gpu/build
VEND=${VEND:-$B/baseband-recon-20261104/vendor-x}      # /vendor/etc/md + mdota: model/firmware files (same vendor build required)
O=${1:?OUTDIR}; WRITE=${2:-}
[ "$(timeout 5 $A devices | grep -c 'recovery$')" = 1 ] || { echo "need exactly one phone in the adb rescue"; exit 1; }
mkdir -p "$O"; T=$(mktemp -d); trap 'rm -rf "$T"; timeout 20 $A shell "for p in nvdata protect1 protect2; do umount /tmp/n2ro/\$p 2>/dev/null; done" || true' EXIT
mkdir -p $T/tree/vendor/etc
for pair in nvdata:nvdata protect1:protect_f protect2:protect_s; do
  part=${pair%%:*}; dst=${pair#*:}
  timeout 30 $A shell "grep -q ' /tmp/n2ro/$part ' /proc/mounts || { mkdir -p /tmp/n2ro/$part && mount -o ro -t ext4 /dev/block/by-name/$part /tmp/n2ro/$part; }" ||
    { echo "cannot mount $part read-only"; exit 2; }
  mkdir -p $T/tree/$dst
  timeout 120 $A exec-out "tar -C /tmp/n2ro/$part -cf - md" | tar -C $T/tree/$dst -xf - || { echo "tar of $part failed"; exit 3; }
  echo "$part $(timeout 120 $A shell "sha256sum /dev/block/by-name/$part" | cut -c1-64)" >> $O/partitions.sha256
done
cp -a $VEND/etc/md $T/tree/vendor/etc/md; [ -d $VEND/etc/mdota ] && cp -a $VEND/etc/mdota $T/tree/vendor/etc/mdota
python3 -I $(dirname "$(readlink -f "$0")")/mk_nvdisk_dev.py --tree $T/tree --out $O/n2nvdisk-this-device.bin --check | tee $O/receipt.txt
SHA=$(sha256sum $O/n2nvdisk-this-device.bin | cut -c1-64); MIB=$(( $(stat -c %s $O/n2nvdisk-this-device.bin) >> 20 ))
if [ "$WRITE" = --write ]; then
  timeout 60 $A push $O/n2nvdisk-this-device.bin /tmp/n2nvdisk.bin
  timeout 60 $A shell "grep -q ' /data ' /proc/mounts && { echo userdata mounted; exit 1; }; dd if=/tmp/n2nvdisk.bin of=/dev/block/by-name/userdata bs=1048576 seek=65536 count=$MIB conv=notrunc,fsync && sync"
  GOT=$(timeout 60 $A shell "dd if=/dev/block/by-name/userdata bs=1048576 skip=65536 count=$MIB 2>/dev/null | sha256sum" | cut -c1-64)
  [ "$GOT" = "$SHA" ] && echo "written and verified at userdata+64GiB ($SHA)" || { echo "VERIFY FAILED $GOT"; exit 4; }
fi
