#!/bin/bash
# Bring the phone to LK fastboot without keys. From rescue: RTC fast-boot bit + sysrq reset. From iOS (q10aj+): wait for its self reset / WDT.
A=$HOME/Android/Sdk/platform-tools/adb; F=$HOME/Android/Sdk/platform-tools/fastboot
T=${1:-600}
for ((t=0; t<T; t+=3)); do
  timeout 3 $F devices | grep -q fastboot && { echo "fastboot after ${t}s"; exit 0; }
  if timeout 3 $A devices | grep -q "recovery$"; then
    timeout 8 $A shell 'P=/sys/devices/platform/10026000.pwrap/10026000.pwrap:mt6359-pmic/mt-pmic/pmic_access; echo 0x5b4 0x2000 > $P; echo 0x5c2 0x1 > $P; echo 1 > /proc/sys/kernel/sysrq; sync; echo b > /proc/sysrq-trigger' >/dev/null 2>&1
    sleep 6
  fi
  sleep 3
done
echo "TIMEOUT waiting for fastboot"; exit 1
