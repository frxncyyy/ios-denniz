#!/bin/bash
# q10jgs-ck = the EXACT q10jgs kernel (sha f94195ff..., not rebuilt), ADT, package and boot-args, packed with NORD2_BOOT_COMPRESS=nostock:
# the only difference to the known-good q10jgs image is the layout (stub instead of the Android kernel). First image to put on the phone.
#   pack_q10jgsck.sh            -> device-mmu-q10jgs-ck-candidate/ui-sched/...img   (log build_q10jgsck.run.log)
#   pack_q10jgsck.sh regress    -> packs the same inputs with the switch UNSET into bootcompress-20261113/test/out/regress and compares
#                                  the result with the q10jgs image byte for byte (the default path of the shared packer is unchanged)
set -u
B=$NORD2_WORK/gpu/build
R=$B/device-mmu-q10jgs-candidate; K=$R/k/kernel/kernel.development.nord2-metal-q10jgs; PKG=$B/cam60-20261112/audio-q10jae.pkg
export NORD2_PHYS_BASE=0x100000000 NORD2_MEM_SIZE=0x13e000000
pack() { # OUTDIR NAME
  mkdir -p $1/.tmp; (cd $B/../.. && TMPDIR=$1/.tmp python3 $B/cam60-20261112/pack_with_args_nosyms.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --drop-arg keepsyms=1 \
    --kernel $K --system-metal --audio-package $PKG --output-dir $1/ui-sched --image-name nord2-ios-metal-$2-ui654fb60dvfs-cap3500-ram5088-sched.img \
    --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37"); rc=$?; rm -rf $1/.tmp $1/ui-sched/kernel.strip $1/ui-sched/gpu.payload; return $rc; }
if [ "${1:-}" = regress ]; then
  O=$B/bootcompress-20261113/test/out/regress; rm -rf $O; mkdir -p $O; unset NORD2_BOOT_COMPRESS
  pack $O q10jgs > $O/stdout 2>&1 || { tail -5 $O/stdout; echo REGRESS_PACK_FAILED; exit 1; }
  if cmp $O/ui-sched/*.img $R/ui-sched/*.img; then echo "REGRESS_OK: switch unset -> byte-identical to q10jgs ($(sha256sum $O/ui-sched/*.img | cut -c1-64))"; rm -rf $O; else echo REGRESS_DIFFERENT; exit 1; fi
  exit 0
fi
O=$B/device-mmu-q10jgs-ck-candidate; [ ! -e $O ] || { echo "$O exists"; exit 1; }
export NORD2_BOOT_COMPRESS=nostock
mkdir -p $O/k/kernel; ln -s $K $O/k/kernel/kernel.development.nord2-metal-q10jgs
{ pack $O q10jgs-ck; echo "PACK_EXIT $?"; grep -h FAILURES $O/ui-sched/check.log; sha256sum $O/ui-sched/*.img; } > $B/build_q10jgsck.run.log 2>&1
tail -4 $B/build_q10jgsck.run.log
