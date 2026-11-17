#!/usr/bin/env python3
"""Read-only look at the DRAM state from the rescue Linux (4.19 stock-derived): only cat/ls/find/grep, nothing written to the phone."""
import subprocess,sys
ADB='$HOME/Android/Sdk/platform-tools/adb'
out=open(sys.argv[1],'w')
def sh(title,cmd,timeout=60):
    out.write('\n===== %s\n$ %s\n'%(title,cmd))
    try:
        r=subprocess.run([ADB,'shell',cmd],capture_output=True,timeout=timeout)
        out.write(r.stdout.decode(errors='replace')+r.stderr.decode(errors='replace'))
    except Exception as e: out.write('ERR %s\n'%e)
    out.flush()
sh('kernel cmdline (bootloader arguments)','cat /proc/cmdline | tr " " "\\n" | grep -i -E "dram|ddr|emi|mem|lpddr" ; echo; cat /proc/cmdline | wc -c')
sh('dmesg: DRAM / EMI / DVFSRC','dmesg | grep -i -E "dramc|dram|ddr|emi|dvfsrc|lpddr|vcore|data.rate" | head -120')
sh('sysfs names','find /sys -maxdepth 7 \\( -iname "*dram*" -o -iname "*dvfsrc*" -o -iname "*emi*" -o -iname "*vcore*" \\) 2>/dev/null | head -80')
sh('dram_data_rate and friends','for f in $(find /sys -maxdepth 8 \\( -name "dram_data_rate" -o -name "dram_type" -o -name "dram_info" \\) 2>/dev/null); do echo "-- $f"; cat $f 2>&1 | head -20; done')
sh('devfreq','for d in /sys/class/devfreq/*; do echo "-- $d"; for k in name cur_freq min_freq max_freq available_frequencies governor; do [ -r $d/$k ] && echo "$k: $(cat $d/$k)"; done; done 2>&1 | head -60')
sh('debugfs mounted?','mount | grep -i debugfs; ls /sys/kernel/debug 2>&1 | head -5')
sh('dvfsrc debugfs (if present)','for f in /sys/kernel/debug/dvfsrc/* /sys/kernel/debug/*dvfsrc*/* ; do [ -f "$f" ] && { echo "-- $f"; head -c 2500 "$f" 2>&1; }; done 2>&1 | head -120')
sh('memory','head -3 /proc/meminfo; cat /proc/iomem | grep -i -E "ram|dram|reserved" | head -12')
sh('cpu clocks seen by Linux','for c in 0 4 7; do echo "cpu$c: $(cat /sys/devices/system/cpu/cpu$c/cpufreq/scaling_cur_freq 2>&1)"; done')
