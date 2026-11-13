#!/bin/bash
# q10jl = q10jk's kernel (k/kernel/kernel.development.nord2-metal-q10jk, e55b966e..., not rebuilt) + the camera bundle of
# camera-userland-20261112-pipe (audio.pkg 206efb91...: the pictures on a thread of their own, two read buffers; /var/db/nord2-camera-serial = as
# before). Pack identical to build_q10jk.sh otherwise (same args, same extra-args).
# usage: build_q10jl.sh [NAME] [OUTDIR]   (OUTDIR must not exist)
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jl}; O=${2:-$W/gpu/build/device-mmu-q10jl-candidate}
B=$W/gpu/build
K=$B/device-mmu-q10jk-candidate/k/kernel/kernel.development.nord2-metal-q10jk
PKG=${AUDIO_PKG:-$B/camera-userland-20261112-pipe/out/audio.pkg}
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
mkdir -p $O
sha256sum $K $PKG | tee $O/inputs.sha256
python3 $B/device-mmu-q10hq-candidate/pack_with_args.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $K --system-metal --audio-package $PKG --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload
grep -h "FAILURES" $O/ui-sched/check.log
echo done; tail -14 $O/ui-sched.stdout
