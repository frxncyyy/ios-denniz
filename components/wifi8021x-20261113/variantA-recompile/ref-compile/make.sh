#!/bin/bash
# Reference compile logs for the two IOUC objects: the UNMODIFIED sources of the q10jgw stage, same staged flags.
set -euo pipefail
W=$NORD2_WORK
H=$(cd "$(dirname "$0")" && pwd); S=$W/gpu/build/device-mmu-q10jgw-candidate/k/kernel/stage; T=$(mktemp -d); cd $W
for n in wifi_ios_client wifi_ios_registry; do
  python3 gpu/tools/xnu_staged_compile.py $S/iokit/Kernel/arm/$n.cpp --cxx --out $T/$n.cpo --extra=-Wno-unsafe-buffer-usage > $H/$n-compile.log 2>&1
  echo "$n: $(grep -c 'warning:' $H/$n-compile.log) warnings, $(grep 'warning:' $H/$n-compile.log | grep -c '/k/kernel/stage/') in the stage sources"; done
rm -rf $T
