#!/bin/bash
# q10jr9 (gnss-dl2-20261112) = q10jr8 + 0362: vendor-order data link V9: ladder K=23 (ioctl 163) = steps 1..17 + MCUB RESET_DONE ack,
# EMI_REQ_GPS, USRT INTEN, RX DMA2 armed (0xA5 ring) BEFORE TX of frame 1, 2 s capture + hex dump, rollback; K=24 (ioctl 164) = V9 HOLD pipe.
# frame 1, 19 capture the DSP reply; 20 = 5 s, 21 = probe, 22 = HOLD + /dev/gpsdl0 read/write (ioctls 141..162). n2gps=1.
# usage: build_q10jr_gnss.sh [NAME] [OUTDIR]   (OUTDIR must not exist); NOPACK=1 stops after the kernel
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jr9}; O=${2:-$W/gpu/build/device-mmu-q10jr9-candidate}
B=$W/gpu/build; SB=$B/scratchpad-backup-20261106
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
# 0350/0351 are frozen as built for q10jr (gnss-20261112/*.patch); the host sources moved on with M2 (gnss-m2-20261112/0352)
SERIES=$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0298,0299,0300,0310,0311,0312,0313,0314,0315,0316,0317,0320,0321,0330,0331,0340,0350,0351,0353,0354,0355,0356,0357,0358,0359,0362
PD=""
for d in $SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 $B/autotest $B/surfbudget-20261111 $B/surfholders-20261111 $W/gpu/hwcodec/kernel $W/gpu/hwcodec/kern/p031x $B/audio-capture-20261111/patches $B/i2c8cam-20261111 $B/cam60-20261112 $B/gnss-20261112 $B/gnss-safe-20261112 $B/gnss-safe2-20261112 $B/gnss-safe3-20261112 $B/gnss-ladder-20261112 $B/gnss-ladder2-20261112 $B/gnss-probe-20261112 $B/gnss-dl-20261112 $B/gnss-dl2-20261112; do PD="$PD --patch-dir $d"; done
mkdir -p $O/k
cd $W
python3 $B/autotest/build_p1_candidate.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name $NAME --out $O/k/kernel > $O/k/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/k/kernel.stdout; ls -t $O/k/kernel/*.log | head -1 | xargs tail -20; exit 1; }
grep -q 'patching file bsd/dev/nord2_gpsdl.inc' $O/k/kernel/apply-0350.log && grep -q 'patching file bsd/dev/sweet_touch.c' $O/k/kernel/apply-0351.log && grep -q 'patching file bsd/dev/nord2_gpsdl.inc' $O/k/kernel/apply-0353.log && grep -q 'patching file bsd/dev/sweet_touch.c' $O/k/kernel/apply-0354.log && grep -q 'patching file bsd/dev/nord2_gpsdl.inc' $O/k/kernel/apply-0355.log && grep -q 'patching file bsd/dev/nord2_gpsdl.inc' $O/k/kernel/apply-0356.log && grep -q 'patching file bsd/dev/nord2_gnss_pwr.inc' $O/k/kernel/apply-0357.log && grep -q 'patching file bsd/dev/nord2_gpsdl.inc' $O/k/kernel/apply-0358.log && grep -q 'patching file bsd/dev/sweet_touch.c' $O/k/kernel/apply-0359.log && grep -q 'patching file bsd/dev/nord2_gpsdl.inc' $O/k/kernel/apply-0362.log
echo KERNEL OK
[ "${NOPACK:-0}" = 1 ] && exit 0
mkdir -p $O/.tmp; TMPDIR=$O/.tmp python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --drop-arg keepsyms=1 --kernel $O/k/kernel/kernel.development.nord2-metal-$NAME --system-metal --audio-package ${AUDIO_PKG:-$B/cam60-20261112/audio-q10jp.pkg} --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37 n2gps=1" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/.tmp $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload $O/k/kernel/kernel.startup.a
# image check: pack checks clean, boot-args carry nord2gnss=1, the kernel carries the GNSS strings
grep -h "FAILURES" $O/ui-sched/check.log
python3 -c "import json,sys;c=json.load(open(sys.argv[1]))['commandline'].split();sys.exit(0 if 'n2gps=1' in c else 1)" $O/ui-sched/image-receipt.json || { echo "IMAGECHECK: n2gps=1 not in the boot-args"; exit 1; }   # the image itself is compressed
grep -a -q 'V9 TX libmnl frame 1 (15 bytes, RX armed before TX)' $O/k/kernel/kernel.development.nord2-metal-$NAME || { echo "IMAGECHECK: GNSS code not in kernel"; exit 1; }
sha256sum $O/ui-sched/*.img
echo "done IMAGECHECK_OK"; tail -14 $O/ui-sched.stdout
