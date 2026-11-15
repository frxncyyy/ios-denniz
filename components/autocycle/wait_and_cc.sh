#!/bin/bash
D=$(dirname "$(readlink -f "$0")"); F=$HOME/Android/Sdk/platform-tools/fastboot
until timeout 3 $F devices | grep -q fastboot; do sleep 5; done
echo "$(date -Is) fastboot seen, starting cc transaction" >> $D/cc.events
$D/cc_transaction.sh $(ls $D/q10bp-BB-BRIDGE-image/*.img) bb1 >> $D/cc.events 2>&1; echo "$(date -Is) cc_transaction rc=$?" >> $D/cc.events
