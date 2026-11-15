#!/bin/bash
set -e
W=$NORD2_WORK
B=$W/gpu/build; T=$W/gpu/tools; MAIN=$NORD2_REPO; D=$B/autocycle; X=$B/cc-txn-bb1
log(){ echo "$(date -Is) $*" | tee -a $X/events; }
ADDON=$(python3 -c "import json;print(json.load(open('$X/offline/verified.json'))['addon_sha256'])")
python3 $T/commit_root_delta_fast.py --main $MAIN --snapshot-receipt $X/snapshot/verified.json --update $X/offline --output $X/device-install --expected-addon-sha256 $ADDON --commit > $X/device-install.log 2>&1; log committed
$D/to_fastboot.sh 300; $D/flash_boot.sh $(ls $D/q10bp-BB-BRIDGE-image/*.img); log kernel flashed
mkdir -p $D/runs/bb1; python3 $W/tools/nord2-usb-console.py $D/runs/bb1/ui-usb --wait 240 --read-seconds 600 > $D/runs/bb1/usb.log 2>&1 || true
log capture done
