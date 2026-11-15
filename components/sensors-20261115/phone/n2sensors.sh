#!/bin/sh
# 0760 ssh diagnostic (run on the phone as root): n2sensors.sh [seconds] [interval]
# prints kern.n2scp_raw once per interval: raw hub values and what iOS gets (ios_mg = milli-g, ios_mrad = mrad/s, ios_muT = milli-uT),
# rates told to the hub, per-sensor counters, HID service state (c = clients, ri = ReportInterval us, "?" = never set, o = opens, e = events).
N=${1:-20}; I=${2:-1}; i=0
command -v sysctl >/dev/null 2>&1 || PATH=$PATH:/var/jb/usr/sbin:/var/jb/usr/bin:/usr/sbin
while [ $i -lt $N ]; do
  sysctl -n kern.n2scp_raw 2>/dev/null || { echo "kern.n2scp_raw missing: kernel without 0760"; exit 1; }
  i=$((i+1)); sleep $I
done
