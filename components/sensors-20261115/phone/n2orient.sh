#!/bin/sh
# 0761 ssh diagnostic: orientation path. n2orient.sh [seconds]
# 1) kernel side: every second the detector state (orient = UIDeviceOrientation 1 portrait 2 upside down 3 landscape left
#    4 landscape right 5 face up 6 face down), changes, low-pass gravity and the device-orientation HID service (c clients,
#    ri ReportInterval, o opens, e events dispatched to the event system).
# 2) userland side, if a unified-log reader exists (log / oslog from the jailbreak): locationd's orientation lines
#    ("AOO update", "Skipping stale orientation", "Starting orientation detection", "Received orientation") and backboardd's
#    "Updating device orientation from CoreMotion".
N=${1:-30}
PATH=$PATH:/var/jb/usr/sbin:/var/jb/usr/bin:/usr/sbin
R=$(command -v log || command -v oslog || true)
if [ -n "$R" ]; then
  echo "== userland log via $R (background, $N s)"
  ( "$R" stream --style compact --predicate 'process == "locationd" OR process == "backboardd"' 2>/dev/null \
      | grep -i -E 'orientation|AOO|accel|gyro' ) &
  LP=$!
else
  echo "== no unified-log reader on the phone (log/oslog): kernel side only"
fi
i=0
while [ $i -lt $N ]; do
  sysctl -n kern.n2scp_raw 2>/dev/null | tr '|' '\n' | grep -E 'orient|acc raw|device-orientation|accelerometer|gyro c' | tr '\n' '|'; echo
  i=$((i+1)); sleep 1
done
[ -n "${LP:-}" ] && kill $LP 2>/dev/null
exit 0
