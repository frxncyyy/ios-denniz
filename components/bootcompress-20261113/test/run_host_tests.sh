#!/bin/bash
# Host tests of a NORD2_BOOT_COMPRESS=nostock boot image (bootcompress-20261113). No phone, no device access.
#   run_host_tests.sh [CANDIDATE_DIR] [LEGACY_IMAGE]
#     CANDIDATE_DIR  build_q10jfi.sh output (default device-mmu-q10jgs-c-candidate): k/kernel/kernel.development.nord2-metal-NAME,
#                    ui-sched/{*.img,gpu.adt,image-receipt.json}
#     LEGACY_IMAGE   image of the same kernel WITH the Android kernel (default: q10jgs), for the A/B run
# Steps: 1 stub rebuilds to the same bytes  2 emulated boot (real loader bytes; RAM window == original kernel; A/B against the legacy
# image; failure paths; see emu_boot.py)  3 static checker on the image with the payload rebuilt from the kernel  4 budget and flash gate
# 5 the test detects damage (three one-bit mutations must FAIL)  6 kernel identity with the reference candidate (when given the default).
# Prints HOST_TESTS_OK or exits 1.
set -u
B=$NORD2_WORK/gpu/build
M=$NORD2_REPO
D=$B/bootcompress-20261113; PY=$D/venv/bin/python; T=$D/test/out
C=${1:-$B/device-mmu-q10jgs-c-candidate}; REFC=$B/device-mmu-q10jgs-candidate
LEG=${2:-$(ls $REFC/ui-sched/*.img)}
fail() { echo "HOST_TESTS_FAILED: $*"; exit 1; }
mkdir -p $T
IMG=$(ls $C/ui-sched/*.img) || fail "no image in $C/ui-sched"; [ "$(echo "$IMG" | wc -l)" = 1 ] || fail "more than one image"
K=$(ls $C/k/kernel/kernel.development.nord2-metal-* | grep -v '\.strip$'); REC=$C/ui-sched/image-receipt.json; ADT=$C/ui-sched/gpu.adt
export NORD2_PHYS_BASE=$(python3 -c "import json;print(json.load(open('$REC'))['phys_base'])") NORD2_MEM_SIZE=$(python3 -c "import json;print(json.load(open('$REC'))['mem_size'])")
LOADER=$(python3 -c "import json;print(json.load(open('$REC'))['loader'])")
echo "== 1 stub"; rm -rf $T/stubcheck; sh $D/stub/build_stub.sh $T/stubcheck || fail "stub build"
cmp $T/stubcheck/stub-Image $D/stub/stub-Image || fail "stub-Image is not what the sources build"
[ "$(python3 -c "import json;print(json.load(open('$REC')).get('stub_sha256'))")" = "$(sha256sum $D/stub/stub-Image | cut -c1-64)" ] || fail "receipt stub_sha256"
echo "== 2 emulated boot"; rm -f $T/expected.payload
nice -n 10 $PY $D/test/emu_boot.py "$IMG" --kernel "$K" --adt $ADT --receipt $REC --legacy "$LEG" --stub $D/stub/stub-Image --save-payload $T/expected.payload > $T/emu.log 2>&1; rc=$?
grep -a -E "^(FAIL|image |HOST BOOT)" $T/emu.log; echo "   $(grep -a -c '^ok ' $T/emu.log) checks ok, log $T/emu.log"
[ $rc = 0 ] && grep -a -q "HOST BOOT TEST: all checks passed" $T/emu.log || fail "emulated boot"
echo "== 3 static checker (payload rebuilt from the kernel)"
(cd $B/../.. && python3 $D/nord2-check-xnu-nostock.py "$IMG" $T/expected.payload --stock $M/research/nord2/rescue/recompressed/base.img --stub $D/stub/stub-Image --loader "$LOADER") > $T/check.log 2>&1; rc=$?
grep -E "^FAIL|FAILURES" $T/check.log; echo "   $(grep -c '^ok ' $T/check.log) checks ok"
[ $rc = 0 ] && grep -q "FAILURES: none" $T/check.log && grep -q "FAILURES: none" $C/ui-sched/check.log || fail "static checker"
echo "== 4 budget / flash gate"
S=$(stat -c %s "$IMG"); SP=$((33554432 - S)); echo "   image $S bytes, boot_spare $SP ($((SP / 1048576)) MiB), sha256 $(sha256sum "$IMG" | cut -c1-64)"
[ "$S" -le 33554432 ] || fail "flash_boot.sh would refuse the image"; [ $SP -ge 4194304 ] || fail "boot_spare below 4 MiB"; [ $((S % 2048)) = 0 ] || fail "image not page aligned"
echo "== 5 the test detects damage"
for mut in kernel-byte adt-byte stub-byte; do
  $PY $D/test/emu_boot.py "$IMG" --kernel "$K" --adt $ADT --receipt $REC --stub $D/stub/stub-Image --mutate $mut > $T/mut-$mut.log 2>&1; rc=$?
  n=$(grep -c '^FAIL' $T/mut-$mut.log); echo "   $mut: exit $rc, $n FAIL: $(grep -m1 '^FAIL' $T/mut-$mut.log | cut -c1-110)"
  [ $rc = 1 ] && [ $n -ge 1 ] || fail "mutation $mut not detected"
done
echo "== 6 kernel identity"
if [ -d $REFC ] && [ "$C" != "$REFC" ]; then
  a=$(sha256sum "$K" | cut -c1-64); b=$(python3 -c "import json;print(json.load(open('$REFC/ui-sched/image-receipt.json'))['kernel'])")
  if [ "$a" = "$b" ]; then echo "   kernel sha256 = q10jgs ($a)"; else echo "   kernel sha256 $a differs from q10jgs $b (expected for any other patch set)"; fi
  cmp -s $ADT $REFC/ui-sched/gpu.adt && echo "   gpu.adt = q10jgs"
fi
rm -f $T/expected.payload; rm -rf $T/stubcheck
echo HOST_TESTS_OK
