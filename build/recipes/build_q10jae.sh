#!/bin/bash
# q10jae = the q10jfb kernel (device-mmu-q10jfb-candidate/k/kernel/kernel.development.nord2-metal-q10jfb: q10jp4's series + 0390 fbblend) UNCHANGED +
# cam60-20261112/audio-q10jae.pkg (camera-userland-20261112-ae2: the AE of run lm6's "lights flicker in strong light" fixed -- the GPU's statistics
# with their own frame's exposure, AE damped/deadband/averaged, highlights' target by the white share, mains floor + flicker hold; see NOTES.md).
# Packed as q10jp4/q10jfb (symbols stripped, the same args).
# usage: build_q10jae.sh [NAME] [OUTDIR]   (OUTDIR must not exist)
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jae}; O=${2:-$W/gpu/build/device-mmu-q10jae-candidate}
B=$W/gpu/build
K=$B/device-mmu-q10jfb-candidate/k/kernel/kernel.development.nord2-metal-q10jfb
PKG=${AUDIO_PKG:-$B/cam60-20261112/audio-q10jae.pkg}
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
mkdir -p $O
sha256sum $K $PKG | tee $O/inputs.sha256
python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $K --system-metal --audio-package $PKG --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload
grep -h "audio package replaced" $O/ui-sched.stdout; grep -h "FAILURES" $O/ui-sched/check.log
# IMAGECHECK: pack clean, spare kept, the q10jfb kernel and boot-args unchanged, the ae2 package in it
grep -q "FAILURES: none" $O/ui-sched/check.log || { echo "IMAGECHECK: check.log failures"; exit 1; }
python3 - $O/ui-sched/image-receipt.json $B/device-mmu-q10jfb-candidate/ui-sched/image-receipt.json $(sha256sum $K | cut -d' ' -f1) <<'PY' || { echo "IMAGECHECK: receipt"; exit 1; }
import json,sys
a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2]))
assert a['boot_spare']>0, 'boot_spare'
assert a['kernel']==sys.argv[3], 'kernel'
assert a['commandline']==b['commandline'], 'commandline'
PY
grep -q "audio package replaced: .* sha256 $(sha256sum $PKG | cut -d' ' -f1)" $O/ui-sched.stdout || { echo "IMAGECHECK: package"; exit 1; }
sha256sum $O/ui-sched/*.img
echo "done IMAGECHECK_OK"
