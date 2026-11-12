#!/bin/bash
# gpu/hwcodec image = the q10ik kernel series (q10hq series + 0296) + 0298, packed exactly like q10ik (same audio package pin).
# Host only. usage: build_image.sh NAME   (e.g. q10hw1; gpu/hwcodec/build/NAME must not exist)
set -uo pipefail
W=$NORD2_WORK
cd $W; B=$W/gpu/build; SB=$B/scratchpad-backup-20261106; N=${1:?name}; O=$W/gpu/hwcodec/build/$N; [ -e $O ] && { echo "$O exists"; exit 1; }
SERIES=$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0298
PD=""
for d in $SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 $B/autotest $W/gpu/hwcodec/kernel; do PD="$PD --patch-dir $d"; done
mkdir -p $O
python3 $B/autotest/build_p1_candidate.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name $N --out $O/kernel > $O/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/kernel.stdout; ls -t $O/kernel/*.log | head -1 | xargs tail -20; exit 1; }
python3 $B/device-mmu-q10hq-candidate/pack_with_args.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $O/kernel/kernel.development.nord2-metal-$N --system-metal --audio-package ${AUDIO_PKG:-$B/autotest/pins/audio-q10ik.pkg} --output-dir $O/ui-sched --image-name nord2-ios-metal-$N-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -5 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload $O/kernel/kernel.startup.a
echo done; tail -12 $O/ui-sched.stdout
