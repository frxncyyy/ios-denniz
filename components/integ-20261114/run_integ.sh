#!/bin/bash
# integ-20261114/run_integ.sh: THE integration entry point (q10jia and later).  One ordered PATCHES list + EXTRA_PATCH_DIRS.
#   usage: run_integ.sh preflight|nopack|full [NAME] [OUTDIR]       (defaults q10jia, $B/device-mmu-NAME-candidate)
#   The builder is generated at run time from the LIVE $B/build_q10jfi.sh (so pack switches added there, e.g. NORD2_BOOT_COMPRESS=lz4,
#   apply unchanged) with only the kernel recipe swapped for integ-20261114/build_p1_candidate_integ.py (= autotest recipe + 0430 Wi-Fi
#   member rebuild, 0432..0434 SAE members, 0436 binary patch of the CI wifi_normal_transport.o, 0450/0451 802.1X objects, optional N2_OPT
#   per-object size flags, default none = CI flags).  Patches are rebased by merge.sh (hand resolutions in resolve/, rules-*.py).
#   Adding a piece: drop NNNN-*.patch + patched-sources-NNNN into its own dir (rebased with merge.sh if it overlaps), append NNNN to
#   PATCHES (ascending) and the dir to INTEG_DIRS.  Env passes through (NORD2_BOOT_COMPRESS, WARN_BUDGET, EXTRA_ARGS, N2_OPT ...).
set -u
B=$NORD2_WORK/gpu/build
H=$B/integ-20261114
export NORD2_PHYS_BASE=0x100000000 NORD2_MEM_SIZE=0x13e000000 WARN_BUDGET=${WARN_BUDGET:-8}
export NORD2_BOOT_COMPRESS=${NORD2_BOOT_COMPRESS-nostock}   # bootcompress-20261113: no stock Android kernel in the image (set to "" for the old layout)
BASE_PATCHES="0394 0395 0396 0397 0398 0399 0400 0401 0403 0404 0405 0406 0407 0408 0409 0410 0411 0413 0414 0415 0416 0417 0418"   # q10jgs
NEW_PATCHES="0420 0430 0432 0433 0434 0436 0440 0441 0450 0451 0452 0453 0460 0461 0462 0470 0480 0481 0482 0483 0484 0490"
INTEG_DIRS="$H/p"                     # integ-20261114/p/NNNN/ = the rebased patches (merge.sh output); later pieces: append dirs or use merge.sh MORE_SRC
export PATCHES="$BASE_PATCHES $NEW_PATCHES${MORE_PATCHES:+ $MORE_PATCHES}"
export EXTRA_PATCH_DIRS="$(cat $B/memfix-20261113b/extra_dirs.txt) $INTEG_DIRS${MORE_DIRS:+ $MORE_DIRS}"
MODE=${1:-preflight}; NAME=${2:-q10jia}; O=${3:-$B/device-mmu-$NAME-candidate}
G=$H/.builder-$NAME.sh
sed "s#python3 \$B/autotest/build_p1_candidate.py #python3 \$B/integ-20261114/build_p1_candidate_integ.py #" $B/build_q10jfi.sh > $G
[ "$(grep -c 'integ-20261114/build_p1_candidate_integ.py' $G)" = 1 ] || { echo "RUN_INTEG: recipe line not found in build_q10jfi.sh"; exit 1; }
chmod +x $G
case "$MODE" in
  preflight) PREFLIGHT_ONLY=1 $G $NAME $O;;
  nopack)    NOPACK=1 $G $NAME $O;;
  full)      $G $NAME $O; rc=$?; echo "BUILD_EXIT $rc"
             if [ -d $O/k/kernel ]; then python3 $B/wifi-20261113/check_scan_path.py $O $B/device-mmu-q10jgs-candidate; echo "SCANPATH_EXIT $?"
               python3 $H/check_wifi_members.py $O; echo "WIFI_MEMBERS_EXIT $?"; fi
             exit $rc;;
  *) echo "usage: run_integ.sh preflight|nopack|full [NAME] [OUTDIR]"; exit 2;;
esac
