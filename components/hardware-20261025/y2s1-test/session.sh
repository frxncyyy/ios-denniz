#!/bin/bash
cd $NORD2_WORK
E=gpu/build/hardware-20261025/y2s1-test
FB=$HOME/Android/Sdk/platform-tools/fastboot; ADB=$HOME/Android/Sdk/platform-tools/adb
waitfb(){ for i in $(seq 1 1800); do $FB devices | grep -q fastboot && return 0; sleep 2; done; return 1; }
echo "[$(date +%T)] waiting fastboot (rescue)"; waitfb || { echo NOFASTBOOT; exit 1; }
python3 $E/flash.py rescue 2>&1 | tail -1
echo "[$(date +%T)] waiting adb recovery"
for i in $(seq 1 120); do s=$($ADB devices | awk 'NR>1&&$2=="recovery"{print $1}'); [ -n "$s" ] && break; sleep 2; done
[ -n "$s" ] || { echo NOADB; exit 1; }
for i in $(seq 1 60); do u=$($ADB shell cat /proc/uptime 2>/dev/null | cut -d. -f1); [ -n "$u" ] && [ "$u" -gt 80 ] && break; sleep 3; done
echo "[$(date +%T)] rescue up, uptime $u; transaction"
python3 $E/run_transaction.py --jobs 4 > $E/transaction.out 2>&1; rc=$?
echo "[$(date +%T)] transaction rc=$rc"; tail -3 $E/transaction.out
[ $rc -eq 0 ] || { echo TRANSACTION-FAILED; exit 2; }
echo "[$(date +%T)] INSTALLED - put the phone in fastboot now"
waitfb || { echo NOFASTBOOT2; exit 3; }
python3 $E/flash.py p36 2>&1 | tail -1
python3 tools/nord2-usb-console.py $E/ui-usb --wait 600 --read-seconds 480 > $E/ui-capture.log 2>&1
echo "[$(date +%T)] capture-done"
