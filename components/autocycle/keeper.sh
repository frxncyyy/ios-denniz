#!/bin/bash
# Keep the final daily modem image (q10bo) on the phone until 08:00; reflash after any crash (fastboot bit armed); log in 600 s segments.
D=$(dirname "$(readlink -f "$0")"); W=$NORD2_WORK
F=$HOME/Android/Sdk/platform-tools/fastboot; IMG=$(ls $D/q10bo-FINAL-image/*.img); n=${1:-1}; k=0
while [ "$(date +%H%M)" -lt 0800 ]; do
  if timeout 3 $F devices | grep -q fastboot; then
    n=$((n+1)); k=0; echo "$(date -Is) crash/fastboot seen -> flash #$n" >> $D/keeper.events
    $D/flash_boot.sh $IMG >> $D/keeper.events 2>&1
  else
    k=$((k+1)); mkdir -p $D/runs/final-$n
    python3 $W/tools/nord2-usb-console.py $D/runs/final-$n/seg-$k --wait 120 --read-seconds 600 > $D/runs/final-$n/seg-$k.log 2>&1
    echo "$(date -Is) run $n seg $k: $(tail -1 $D/runs/final-$n/seg-$k.log | cut -c1-80)" >> $D/keeper.events
  fi
  sleep 5
done
date -Is > $D/FINAL_DONE
