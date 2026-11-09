#!/bin/bash
# make sure the stock camera streams the rear main sensor (device 0)
A=~/Android/Sdk/platform-tools/adb
$A shell 'input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard' 
for i in 1 2 3; do
  st=$($A shell dumpsys media.camera 2>/dev/null | grep "is open" | tail -1)
  cur=$($A shell dumpsys media.camera 2>/dev/null | grep -c "Camera ID: 0")
  [ "$cur" -ge 1 ] && { echo "device 0 open"; exit 0; }
  if $A shell dumpsys media.camera 2>/dev/null | grep -q "Camera ID: [12]"; then $A shell input tap 900 1962; else $A shell 'am start -a android.media.action.STILL_IMAGE_CAMERA' >/dev/null; fi
  sleep 6
done
echo "could not open device 0"; exit 1
