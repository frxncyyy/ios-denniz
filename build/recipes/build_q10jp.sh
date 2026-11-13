#!/bin/bash
# q10jp = q10jm's kernel (7e7bbef8..., not rebuilt) + cam60-20261112/audio-q10jp.pkg (camera-userland-20261112-hd60): q10jo's bundle + every
# frame-rate property set and FormatIndex change logged. HD 60 itself needs the plist of payload-camhd60a (1080p60/720p60 on the Video-60
# stream format: the Video-FES-60 one matches none of the driver's formats, so CMCapture ran 1080p30). Packed as q10jm (symbols stripped).
# usage: build_q10jn.sh [NAME] [OUTDIR]   (OUTDIR must not exist)
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jp}; O=${2:-$W/gpu/build/device-mmu-q10jp-candidate}
B=$W/gpu/build
K=$B/device-mmu-q10jm-candidate/k/kernel/kernel.development.nord2-metal-q10jm
PKG=${AUDIO_PKG:-$B/cam60-20261112/audio-q10jp.pkg}
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
mkdir -p $O
sha256sum $K $PKG | tee $O/inputs.sha256
python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $K --system-metal --audio-package $PKG --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload
grep -h "audio package replaced" $O/ui-sched.stdout; grep -h "FAILURES" $O/ui-sched/check.log
echo done; tail -14 $O/ui-sched.stdout
