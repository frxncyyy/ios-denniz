#!/bin/bash
# put the vendor tree + the VNDK-first library set into the (RAM) rescue so the original MediaTek daemons can run again
ADB=$HOME/Android/Sdk/platform-tools/adb
cd "$(dirname "$0")"
$ADB push -a vendor-x/. /vendor/ >/dev/null 2>&1
$ADB shell 'chmod 755 /vendor/bin/*; rm -rf /vendor/vndk31; mkdir -p /vendor/vndk31'
for l in android.system.suspend@1.0.so libbase.so libbinder.so libcgrouprc.so libcutils.so libhardware_legacy.so libhidlbase.so libprocessgroup.so libutils.so; do
  [ -f vndk31/p/lib64/$l ] && $ADB push vndk31/p/lib64/$l /vendor/vndk31/$l >/dev/null 2>&1
done
for l in libc++.so liblog.so libvndksupport.so; do $ADB push system-x/system/lib64/$l /vendor/vndk31/$l >/dev/null 2>&1; done
$ADB push system-x/system/lib64/bootstrap/libdl_android.so /vendor/vndk31/libdl_android.so >/dev/null 2>&1
$ADB push shim/libtrace.so /vendor/vndk31/libtrace.so >/dev/null 2>&1
$ADB shell 'ls /vendor/vndk31 | wc -l; env LD_LIBRARY_PATH=/vendor/lib64:/vendor/vndk31 /system/bin/linker64 --list /vendor/bin/ccci_mdinit 2>&1 | grep -ci "not found"'
