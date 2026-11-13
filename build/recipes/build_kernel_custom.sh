#!/bin/bash
# q10at1 = q10hq kernel series + 0296 (gpu/build/autotest: synthetic touches/keys and panel pictures over the USB console).
# Same command line and pack as q10ic (audio package = camera-userland-20261109/out/audio.pkg as it is NOW unless AUDIO_PKG is given).
# Host only; flashing is a separate, serialized step.
set -uo pipefail
W=$NORD2_WORK
cd $W
B=$W/gpu/build; SB=$B/scratchpad-backup-20261106
O=$B/device-mmu-${NAME:?set NAME}-candidate/k
SERIES=${SERIES:-$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0297}
PD=""
for d in $SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 ${EXTRA_PATCH_DIRS:-} $B/autotest; do PD="$PD --patch-dir $d"; done
mkdir -p $O
python3 $B/autotest/build_p1_candidate.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name ${NAME:-q10at1} --out $O/kernel > $O/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/kernel.stdout; exit 1; }
echo kernel done; ls -l $O/kernel/kernel.development.nord2-metal-${NAME:-q10at1}
