#!/bin/bash
# q10jp4 = the q10jn kernel (7e7bbef8...; NOT 0360) + cam60-20261112/audio-q10jp4.pkg (camera-userland-20261112-gpuauto2): q10jp3's bundle
# with zero copy opt-in again (/var/db/nord2-camera-zerocopy) and only for strides of a multiple of 64 -- run cam30: the ultra-wide's
# 4080-byte rows in GPU memory were copied out by the library, the first picture outlasted the graph's 10 s stop, mediaserverd killed.
# GPU pictures/statistics/noise stay automatic with an API-version-2 library. Packed as q10jm (symbols stripped).
# usage: build_q10jn.sh [NAME] [OUTDIR]   (OUTDIR must not exist)
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jp4}; O=${2:-$W/gpu/build/device-mmu-q10jp4-candidate}
B=$W/gpu/build
K=$B/device-mmu-q10jm-candidate/k/kernel/kernel.development.nord2-metal-q10jm
PKG=${AUDIO_PKG:-$B/cam60-20261112/audio-q10jp4.pkg}
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
mkdir -p $O
sha256sum $K $PKG | tee $O/inputs.sha256
python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $K --system-metal --audio-package $PKG --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload
grep -h "audio package replaced" $O/ui-sched.stdout; grep -h "FAILURES" $O/ui-sched/check.log
echo done; tail -14 $O/ui-sched.stdout
