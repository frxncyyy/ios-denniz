#!/bin/bash
# q10jr (gnss-20261112) = q10jm's series (q10jp runs q10jm's kernel) + 0350/0351: /dev/gpsdl0,1 and the MT6893 GNSS power
# core bound to the retained Wi-Fi conninfra owner; boot-arg n2gps=1 added (alias of nord2gnss=1; keepsyms=1 dropped (the packer strips the kernel symbol table anyway) to stay under the 607-char boot-args limit). Packed like q10jp (audio-q10jp.pkg, symbols
# stripped). Separate from q10jq (another agent: 0360 on the same base); merge later.
# usage: build_q10jr_gnss.sh [NAME] [OUTDIR]   (OUTDIR must not exist); NOPACK=1 stops after the kernel
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jr}; O=${2:-$W/gpu/build/device-mmu-q10jr-candidate}
B=$W/gpu/build; SB=$B/scratchpad-backup-20261106
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
# 0350/0351 are frozen as built for q10jr (gnss-20261112/*.patch); the host sources moved on with M2 (gnss-m2-20261112/0352)
SERIES=$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0298,0299,0300,0310,0311,0312,0313,0314,0315,0316,0317,0320,0321,0330,0331,0340,0350,0351
PD=""
for d in $SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 $B/autotest $B/surfbudget-20261111 $B/surfholders-20261111 $W/gpu/hwcodec/kernel $W/gpu/hwcodec/kern/p031x $B/audio-capture-20261111/patches $B/i2c8cam-20261111 $B/cam60-20261112 $B/gnss-20261112; do PD="$PD --patch-dir $d"; done
mkdir -p $O/k
cd $W
python3 $B/autotest/build_p1_candidate.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name $NAME --out $O/k/kernel > $O/k/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/k/kernel.stdout; ls -t $O/k/kernel/*.log | head -1 | xargs tail -20; exit 1; }
grep -q 'patching file bsd/dev/nord2_gpsdl.inc' $O/k/kernel/apply-0350.log && grep -q 'patching file bsd/dev/sweet_touch.c' $O/k/kernel/apply-0351.log
echo KERNEL OK
[ "${NOPACK:-0}" = 1 ] && exit 0
python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --drop-arg keepsyms=1 --kernel $O/k/kernel/kernel.development.nord2-metal-$NAME --system-metal --audio-package ${AUDIO_PKG:-$B/cam60-20261112/audio-q10jp.pkg} --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37 n2gps=1" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload $O/k/kernel/kernel.startup.a
# image check: pack checks clean, boot-args carry nord2gnss=1, the kernel carries the GNSS strings
grep -h "FAILURES" $O/ui-sched/check.log
python3 -c "import json,sys;c=json.load(open(sys.argv[1]))['commandline'].split();sys.exit(0 if 'n2gps=1' in c else 1)" $O/ui-sched/image-receipt.json || { echo "IMAGECHECK: n2gps=1 not in the boot-args"; exit 1; }   # the image itself is compressed
grep -a -q 'NORD2 GNSS: /dev/gpsdl0,1 made' $O/k/kernel/kernel.development.nord2-metal-$NAME || { echo "IMAGECHECK: GNSS code not in kernel"; exit 1; }
sha256sum $O/ui-sched/*.img
echo "done IMAGECHECK_OK"; tail -14 $O/ui-sched.stdout
