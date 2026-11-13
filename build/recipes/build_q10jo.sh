#!/bin/bash
# q10jo = q10jm's kernel (0340 sensor modes, 7e7bbef8..., not rebuilt) + the camera bundle cam60-20261112/audio-q10jo.pkg
# (camera-userland-20261112-gpustats): q10jn's + the GPU's frame statistics used for exposure, white balance and focus (needs
# libredwoodair y2s64-vt24: payload-vt24; with an older library the processor measures as before) + the processor's 2/3-size pictures
# whenever the GPU will not make one. Packed as q10jm (symbols stripped).
# usage: build_q10jn.sh [NAME] [OUTDIR]   (OUTDIR must not exist)
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jo}; O=${2:-$W/gpu/build/device-mmu-q10jo-candidate}
B=$W/gpu/build
K=$B/device-mmu-q10jm-candidate/k/kernel/kernel.development.nord2-metal-q10jm
PKG=${AUDIO_PKG:-$B/cam60-20261112/audio-q10jo.pkg}
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
mkdir -p $O
sha256sum $K $PKG | tee $O/inputs.sha256
python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $K --system-metal --audio-package $PKG --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload
grep -h "audio package replaced" $O/ui-sched.stdout; grep -h "FAILURES" $O/ui-sched/check.log
echo done; tail -14 $O/ui-sched.stdout
