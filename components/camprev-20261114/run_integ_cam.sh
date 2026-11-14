#!/bin/bash
# camflash-20261114/run_integ_cam.sh: integ-20261114/run_integ.sh with the camera bundle of this dir (pkg/audio-q10jcr.pkg) in place of
# the AE2 package (build_q10jfi.sh pins its sha: swapped here in the generated builder only; the shared scripts are not touched).
#   usage: run_integ_cam.sh full NAME OUTDIR      (env as run_integ.sh: MORE_PATCHES, MORE_DIRS, EXTRA_ARGS, SCANPATH_EXTRA_ALLOWED)
set -u
B=$NORD2_WORK/gpu/build
H=$B/integ-20261114; D=$B/camflash-20261114
PKGF=$NORD2_WORK/gpu/build/camprev-20261114/pkg/audio-camprev1.pkg; PKGS=$(sha256sum $PKGF | cut -c1-64)
export NORD2_PHYS_BASE=0x100000000 NORD2_MEM_SIZE=0x13e000000 WARN_BUDGET=${WARN_BUDGET:-8}
export NORD2_BOOT_COMPRESS=${NORD2_BOOT_COMPRESS-nostock}
BASE_PATCHES="0394 0395 0396 0397 0398 0399 0400 0401 0403 0404 0405 0406 0407 0408 0409 0410 0411 0413 0414 0415 0416 0417 0418"
NEW_PATCHES="0420 0430 0432 0433 0434 0436 0440 0441 0450 0451 0452 0453 0460 0461 0462 0470 0480 0481 0482 0483 0484 0490"
export PATCHES="$BASE_PATCHES $NEW_PATCHES${MORE_PATCHES:+ $MORE_PATCHES}"
export EXTRA_PATCH_DIRS="$(cat $B/memfix-20261113b/extra_dirs.txt) $H/p${MORE_DIRS:+ $MORE_DIRS}"
export AUDIO_PKG=$PKGF
MODE=${1:-preflight}; NAME=${2:-q10jcr}; O=${3:-$B/device-mmu-$NAME-candidate}
G=$NORD2_WORK/gpu/build/camprev-20261114/.builder-$NAME.sh
sed -e "s#python3 \$B/autotest/build_p1_candidate.py #python3 \$B/integ-20261114/build_p1_candidate_integ.py #" \
    -e "s#PKG_SHA=94fb2f9b6ba09fd921d42f15f034c5618dbcd85de16af8e29ae03f99fb7584b4#PKG_SHA=$PKGS#" \
    -e "s#is not audio-q10jae.pkg#is not the camflash package#" $B/build_q10jfi.sh > $G
[ "$(grep -c 'integ-20261114/build_p1_candidate_integ.py' $G)" = 1 ] && [ "$(grep -c "PKG_SHA=$PKGS" $G)" = 1 ] || { echo "RUN_INTEG_CAM: builder edit failed"; exit 1; }
chmod +x $G
echo "camera package $PKGF sha256 $PKGS"
case "$MODE" in
  preflight) PREFLIGHT_ONLY=1 $G $NAME $O;;
  full)      $G $NAME $O; rc=$?; echo "BUILD_EXIT $rc"
             if [ -d $O/k/kernel ]; then python3 $B/wifi-20261113/check_scan_path.py $O $B/device-mmu-q10jgs-candidate; echo "SCANPATH_EXIT $?"
               python3 $H/check_wifi_members.py $O; echo "WIFI_MEMBERS_EXIT $?"; fi
             exit $rc;;
esac
