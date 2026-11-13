#!/bin/bash
# q10jfg = merge of the camera/display line q10jfe (q10jp4/q10jm series + 0390 fbblend + 0391 fbdiag + 0392 fbretain, AE2 bundle
# cam60-20261112/audio-q10jae.pkg 94fb2f9b..., 255216 B) and the GNSS line q10jr11 (same q10jm base + 0350,0351,0353..0359,0362,0363,0364;
# 0352 (gnss-m2) and 0360/0361 (q10jq line) are NOT in either line and stay out).
# Both lines share hq-series + 0296..0340 exactly. The two added sets touch disjoint files (0390..0392: iokit/Kernel/arm/SweetFramebuffer.cpp,
# sweet_fb_blend.h, sweet_fb_scene.h; 0350..0364: bsd/dev/nord2_gpsdl.inc, nord2_gnss_pwr.inc, sweet_touch.c), so no textual conflicts.
# Order: GNSS first, 0390..0392 LAST, because build_p1_candidate.py checks the tracked sources against patched-sources-NNNN with
# NNNN <= series[-1]; with 0364 last, patched-sources-0392 (SweetFramebuffer.cpp) would be skipped and the check would fail.
# Boot-args: q10jfe's line is 606 chars (limit 607); n2gps=1 is added and keepsyms=1 dropped as in q10jr (the packer strips symbols) -> 603.
# Budget (boot_spare = 32 MiB - image): q10jp (q10jm kernel + 255216 B pkg) 81920; 0390..0392 cost 4096 (q10jfe 77824); the GNSS set
# costs 24576 (q10jr11 57344, same-size pkg) -> expected ~53248. No trim needed.
# usage: build_q10jfg.sh [NAME] [OUTDIR]   (OUTDIR must not exist); NOPACK=1 stops after the kernel
set -euo pipefail
W=$NORD2_WORK
NAME=${1:-q10jfg}; O=${2:-$W/gpu/build/device-mmu-q10jfg-candidate}
B=$W/gpu/build; SB=$B/scratchpad-backup-20261106
PKG=${AUDIO_PKG:-$B/cam60-20261112/audio-q10jae.pkg}
[ ! -e "$O" ] || { echo "$O exists"; exit 1; }
SERIES=$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0298,0299,0300,0310,0311,0312,0313,0314,0315,0316,0317,0320,0321,0330,0331,0340,0350,0351,0353,0354,0355,0356,0357,0358,0359,0362,0363,0364,0390,0391,0392
PD=""
for d in $SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 $B/autotest $B/surfbudget-20261111 $B/surfholders-20261111 $W/gpu/hwcodec/kernel $W/gpu/hwcodec/kern/p031x $B/audio-capture-20261111/patches $B/i2c8cam-20261111 $B/cam60-20261112 $B/gnss-20261112 $B/gnss-safe-20261112 $B/gnss-safe2-20261112 $B/gnss-safe3-20261112 $B/gnss-ladder-20261112 $B/gnss-ladder2-20261112 $B/gnss-probe-20261112 $B/gnss-dl-20261112 $B/gnss-dl2-20261112 $B/gnss-dl3-20261112 $B/gnss-dl4-20261112 $B/fbblend-20261112 $B/fbdiag-20261112 $B/fbretain-20261112; do PD="$PD --patch-dir $d"; done
mkdir -p $O/k
cd $W
python3 $B/autotest/build_p1_candidate.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name $NAME --out $O/k/kernel > $O/k/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/k/kernel.stdout; ls -t $O/k/kernel/*.log | head -1 | xargs tail -20; exit 1; }
L=$O/k/kernel
for p in 0350:nord2_gpsdl.inc 0351:sweet_touch.c 0353:nord2_gpsdl.inc 0354:sweet_touch.c 0355:nord2_gpsdl.inc 0356:nord2_gpsdl.inc 0357:nord2_gnss_pwr.inc 0358:nord2_gpsdl.inc 0359:sweet_touch.c 0362:nord2_gpsdl.inc 0363:nord2_gpsdl.inc 0363:sweet_touch.c 0364:nord2_gpsdl.inc; do
  grep -q "patching file bsd/dev/${p#*:}" $L/apply-${p%%:*}.log || { echo "APPLY: ${p%%:*} ${p#*:}"; exit 1; }; done
for p in 0390 0391 0392; do grep -q 'patching file iokit/Kernel/arm/SweetFramebuffer.cpp' $L/apply-$p.log || { echo "APPLY: $p"; exit 1; }; done
! grep -l -E 'Hunk .* FAILED|offset [0-9]+ lines|fuzz' $L/apply-*.log || { echo "APPLY: fuzz/offset/failed hunks above"; exit 1; }
# no new compiler warnings: per compile log, never more than q10jfe had (its only ones are the IOKit C++ header -Wsuggest-override & co.)
for f in $L/*-compile.log; do r=$B/device-mmu-q10jfe-candidate/k/kernel/$(basename $f); n=$(cat $f | grep -c 'warning:' || true); m=$(cat $r 2>/dev/null | grep -c 'warning:' || true)
  [ "$n" -le "$m" ] || { echo "COMPILE: $(basename $f) $n warnings > q10jfe $m"; exit 1; }; done
echo KERNEL OK
[ "${NOPACK:-0}" = 1 ] && exit 0
mkdir -p $O/.tmp; TMPDIR=$O/.tmp python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --drop-arg keepsyms=1 --kernel $L/kernel.development.nord2-metal-$NAME --system-metal --audio-package $PKG --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37 n2gps=1" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -15 $O/ui-sched.stdout; exit 1; }
rm -rf $O/.tmp $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload $L/kernel.startup.a
# IMAGECHECK: pack clean, spare kept, boot-args = q10jfe's - keepsyms=1 + n2gps=1 (<= 607), AE2 package in it, GNSS + fb 0392 code in the kernel
grep -h "FAILURES" $O/ui-sched/check.log
grep -q "FAILURES: none" $O/ui-sched/check.log || { echo "IMAGECHECK: check.log failures"; exit 1; }
python3 - $O/ui-sched/image-receipt.json $B/device-mmu-q10jfe-candidate/ui-sched/image-receipt.json <<'PY' || { echo "IMAGECHECK: receipt"; exit 1; }
import json,sys
a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2]))
assert a['boot_spare']>0, 'boot_spare'
want=[x for x in b['commandline'].split() if x!='keepsyms=1']+['n2gps=1']
assert a['commandline'].split()==want, 'commandline'
assert len(a['commandline'])<=607, 'commandline length'
print('boot_spare', a['boot_spare'], 'bytes', a['bytes'], 'cmdlen', len(a['commandline']))
PY
grep -q "audio package replaced: .* sha256 $(sha256sum $PKG | cut -d' ' -f1)" $O/ui-sched.stdout || { echo "IMAGECHECK: package"; exit 1; }
grep -a -q 'S11 RESULT: DSP booted (50/50 fragments acked)' $L/kernel.development.nord2-metal-$NAME || { echo "IMAGECHECK: GNSS code not in kernel"; exit 1; }
grep -a -q "layers outside a swap's mask kept %s (0392)" $L/kernel.development.nord2-metal-$NAME || { echo "IMAGECHECK: 0392 not in kernel"; exit 1; }
sha256sum $O/ui-sched/*.img
echo "done IMAGECHECK_OK"; tail -14 $O/ui-sched.stdout
