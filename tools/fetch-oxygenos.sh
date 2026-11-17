#!/usr/bin/env bash
# Stock OxygenOS images for the Nord 2 (oplus6893): there is NO automatic download.
#
# The port was built against one OxygenOS build. No official download URL of that full OTA is recorded in
# this project, so this script does not guess one. Get the images in one of two ways:
#   a) the official OxygenOS full OTA zip from OnePlus for your phone, then
#        payload-dumper-go -p boot,recovery,vendor <OTA.zip>
#   b) your own phone (bootloader unlocked, rescue booted): the read-only dump script
#        components/release-20261115/tools/n2dev/n2_read_device.sh ~/nord2-private/dump
# and then verify boot/recovery against the build the kernel constants were made for:
#   tools/fetch-oxygenos.sh verify boot.img recovery.img
set -euo pipefail
BOOT_SHA256=04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01       # release-manifest supported_firmware.boot
RECOVERY_SHA256=45a8e566822ce28aea1bfec19747609a76882c99b039e9ed5a7103ff5f88bdf4   # supported_firmware.recovery
if [ "${1:-}" != verify ] || [ $# -ne 3 ]; then sed -n '2,13p' "$0"; exit 2; fi
ok=1
echo "$BOOT_SHA256  $2" | sha256sum -c - || ok=0
echo "$RECOVERY_SHA256  $3" | sha256sum -c - || ok=0
[ $ok = 1 ] && echo "OXYGENOS_SUPPORTED" || { echo "OXYGENOS_DIFFERENT: another build; the kernel layout constants may not match (expect REBUILD_NEEDED)"; exit 1; }
