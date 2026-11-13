#!/bin/bash
# usage: cycle_photo.sh NAME   (e.g. q10if) -- build bundle from the tree, pack with q10at3 kernel, flash boot-only, run the photo scenario.
set -u
W=$NORD2_WORK
B=$W/gpu/build; C=$B/camera-userland-20261109; A=$B/autotest; N=$1; D=$B/device-mmu-$N-candidate
cd $C && bash build_cam.sh > $B/cycle_$N.build.log 2>&1 || { echo BUILD_FAILED; tail -5 $B/cycle_$N.build.log; exit 1; }
bash run_tests.sh > $B/cycle_$N.tests.log 2>&1 || { echo TESTS_FAILED; exit 1; }
cp out/audio.pkg $A/pins/audio-$N.pkg; echo "audio $(sha256sum out/audio.pkg | cut -c1-16) $(stat -c %s out/audio.pkg) B"
cd $W; mkdir -p $D
python3 $B/device-mmu-q10hq-candidate/pack_with_args.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $A/q10at3/kernel/kernel.development.nord2-metal-q10at3 --system-metal --audio-package $A/pins/audio-$N.pkg --output-dir $D/ui-sched --image-name nord2-ios-metal-$N-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $D/ui-sched.stdout 2>&1 || { echo PACK_FAILED; tail -3 $D/ui-sched/check.log; exit 1; }
tail -1 $D/ui-sched/check.log; rm -rf $D/ui-sched/kernel.strip $D/ui-sched/gpu.payload
cd $A; timeout 60 python3 phone.py reset fastboot | tail -1; sleep 15
bash flash_q10at.sh $D/ui-sched/nord2-ios-metal-$N-ui654fb60dvfs-cap3500-ram3776-sched.img $N 2>&1 | tail -2 || exit 1
sleep 50; timeout 100 python3 phone.py launch camera | tail -1; sleep 8; timeout 100 python3 phone.py launch camera | tail -1; sleep 8; timeout 60 python3 phone.py mode video | tail -1; sleep 6
mkdir -p runs/$N; timeout 60 python3 phone.py screenshot runs/$N/pre.png --scale 4 | tail -1
timeout 30 python3 phone.py shutter | tail -1; sleep 12; timeout 60 python3 phone.py screenshot runs/$N/rec1.png --scale 4 | tail -1
sleep 8; timeout 60 python3 phone.py screenshot runs/$N/rec2.png --scale 4 | tail -1
timeout 30 python3 phone.py shutter | tail -1; sleep 12; timeout 60 python3 phone.py screenshot runs/$N/post.png --scale 4 | tail -1
timeout 150 python3 phone.py logs runs/$N/all.log 2>&1 | tail -1; echo "log: $A/runs/$N/all.log ($(wc -c < runs/$N/all.log) B)"
