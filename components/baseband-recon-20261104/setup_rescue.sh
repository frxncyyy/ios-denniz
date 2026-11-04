#!/bin/bash
# prepare a freshly booted rescue (RAM only) for n2mdinit: /vendor/etc files, run helper, permissive SELinux, mount points
ADB=$HOME/Android/Sdk/platform-tools/adb
cd "$(dirname "$0")"
$ADB shell 'setenforce 0; mkdir -p /vendor/etc/md /vendor/etc/mdota /mnt/vendor/nvdata /mnt/vendor/nvcfg /mnt/vendor/protect_f /mnt/vendor/protect_s /mnt/vendor/persist /data/vendor_de/md /data/vendor/mdlpm'
$ADB push -a vendor-x/etc/md/. /vendor/etc/md/ >/dev/null 2>&1
$ADB push -a vendor-x/etc/mdota/. /vendor/etc/mdota/ >/dev/null 2>&1
$ADB push n2mdinit/run.sh /tmp/run.sh >/dev/null 2>&1
$ADB push n2mdinit/n2mdinit /tmp/n2mdinit >/dev/null 2>&1
$ADB shell 'chmod 755 /tmp/n2mdinit; ls /vendor/etc/md/cacerts | head -2; ls /vendor/etc/mdota | head -3'
