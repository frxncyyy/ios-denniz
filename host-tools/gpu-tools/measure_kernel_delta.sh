#!/bin/sh
# Approximate link-size delta for the GPU patch, without claiming a bootable
# image.  The CI link inputs are an archive of all kernel objects.  We link
# that archive twice with ld64.lld, once baseline and once with the candidate
# GPU archive force-loaded, with dead stripping enabled.  The absolute sizes
# differ from the CI's final -filelist image because undefined kext helpers are
# stubbed and the selected objects/layout differ; only the delta is used.
set -eu
cd "$(dirname "$0")/.."
WORK=$PWD
CI=${1:-$NORD2_REPO/research/ci-40034353275/xnu-kernel/kernel.development.sweet.link}
BASE=$CI/kernel.development.sweet.a
GPU=${2:-$WORK/build/libnord2gpu.a}
OUT=$WORK/build/kernel-link
mkdir -p "$OUT"
[ -f "$BASE" ] || { echo "missing CI archive: $BASE" >&2; exit 2; }
[ -f "$GPU" ] || { echo "missing $GPU; run tools/run_host_tests.sh" >&2; exit 2; }
for mode in base gpu; do
  extra=
  if [ "$mode" = gpu ]; then extra="-all_load $GPU"; fi
  # shellcheck disable=SC2086
  ld64.lld -arch arm64 -platform_version macos 14.0 14.0 -pie -e __start \
    -dead_strip -all_load -undefined dynamic_lookup "$BASE" $extra \
    -o "$OUT/kernel.$mode"
done
b=$(stat -c %s "$OUT/kernel.base")
g=$(stat -c %s "$OUT/kernel.gpu")
echo "synthetic baseline: $b bytes"
echo "synthetic +GPU:     $g bytes"
echo "delta:              $((g - b)) bytes"
echo "NOTE: not a boot image; absolute size is not the CI kernel size."
