#!/bin/bash
# capture the XNU USB console in 10-minute reads, appending, for up to 50 minutes
W=$NORD2_WORK
R=$W/gpu/build/autocycle/runs/q10f3
mkdir -p $R/ui-usb
for i in 1 2 3 4 5 6 7 8; do
  python3 $W/tools/nord2-usb-console.py $R/ui-usb --wait 30 --read-seconds 600 >> $R/usb-capture.log 2>&1
  echo "$(date -Is) capture pass $i: $(wc -c < $R/ui-usb/xnu-live.log 2>/dev/null) bytes" >> $R/events
done
echo done > $R/DONE
