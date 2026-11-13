#!/bin/bash
# q10jh = q10jg + 0313 (opus-pool): RWVC vcodec DMA pool moved to 0xA0000000 and grown 86 -> 384 MiB (free low DRAM below
# XNU's window, so iOS keeps all 3776 MiB; RWVC_INFO reports pool_pa 0xa0000000 pool_bytes 0x18000000). Not 0301.
# usage: build_q10jh.sh NAME OUTDIR   (OUTDIR must not exist); NOPACK=1 stops after the kernel
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jh}; O=${2:-$W/gpu/build/device-mmu-q10jh-candidate}
B=$W/gpu/build; SB=$B/scratchpad-backup-20261106
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
SERIES=$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0298,0299,0300,0310,0311,0312,0313,0320,0321,0330,0331
PD=""
for d in $SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 $B/autotest $B/surfbudget-20261111 $B/surfholders-20261111 $W/gpu/hwcodec/kernel $W/gpu/hwcodec/kern/p031x $B/audio-capture-20261111/patches $B/i2c8cam-20261111; do PD="$PD --patch-dir $d"; done
mkdir -p $O/k
cd $W
python3 $B/autotest/build_p1_candidate.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name $NAME --out $O/k/kernel > $O/k/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/k/kernel.stdout; ls -t $O/k/kernel/*.log | head -1 | xargs tail -20; exit 1; }
echo KERNEL OK
[ "${NOPACK:-0}" = 1 ] && exit 0
python3 $B/device-mmu-q10hq-candidate/pack_with_args.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $O/k/kernel/kernel.development.nord2-metal-$NAME --system-metal --audio-package $B/autotest/pins/audio-q10jy.pkg --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload $O/k/kernel/kernel.startup.a
echo done; tail -14 $O/ui-sched.stdout
