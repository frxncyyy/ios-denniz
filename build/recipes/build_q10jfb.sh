#!/bin/bash
# q10jfb = q10jp4 (q10jm series + cam60-20261112/audio-q10jp4.pkg) + 0390 (fbblend-20261112: SweetFramebuffer composites the SwapEnd layers;
# Photos black grid = backboardd detached the app drawable to layer 0 and the transparent UI layer was memcpy-d over it). n2fbblend=0 = old copy.
# q10jm = q10jk + 0340 (cam60-20261112: the main camera's sensor modes, REDWOOD_CAMERA_MODE: 2048x1152 and 4096x2304 at 60/s) + the camera
# bundle of camera-userland-20261112-cam60 (q10jl's pictures thread + the 60/s mode asked for a 60-60 film). Packed by
# cam60-20261112/pack_with_args_nosyms.py: the kernel's symbol table stripped (boot_spare was 0 with q10jl). Recipe otherwise build_q10jk.sh.
# usage: build_q10jk.sh NAME OUTDIR   (OUTDIR must not exist); NOPACK=1 stops after the kernel
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jfb}; O=${2:-$W/gpu/build/device-mmu-q10jfb-candidate}
B=$W/gpu/build; SB=$B/scratchpad-backup-20261106
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
SERIES=$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0298,0299,0300,0310,0311,0312,0313,0314,0315,0316,0317,0320,0321,0330,0331,0340,0390
PD=""
for d in $SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 $B/autotest $B/surfbudget-20261111 $B/surfholders-20261111 $W/gpu/hwcodec/kernel $W/gpu/hwcodec/kern/p031x $B/audio-capture-20261111/patches $B/i2c8cam-20261111 $B/cam60-20261112 $B/fbblend-20261112; do PD="$PD --patch-dir $d"; done
mkdir -p $O/k
cd $W
python3 $B/autotest/build_p1_candidate.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name $NAME --out $O/k/kernel > $O/k/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/k/kernel.stdout; ls -t $O/k/kernel/*.log | head -1 | xargs tail -20; exit 1; }
echo KERNEL OK
[ "${NOPACK:-0}" = 1 ] && exit 0
python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $O/k/kernel/kernel.development.nord2-metal-$NAME --system-metal --audio-package ${AUDIO_PKG:-$B/cam60-20261112/audio-q10jp4.pkg} --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload $O/k/kernel/kernel.startup.a
echo done; tail -14 $O/ui-sched.stdout
