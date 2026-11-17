#!/bin/sh
# Offline-only preparation; no device commands. Run only when root requests build.
set -eu
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
OUTDIR=research/nord2/ufs-preflight-cost/ios-profile
IMAGE=shim/out/nord2-xnu/nord2-ios-ufs-cost-stop23.img
KERNEL=research/ci-39503015652/xnu-kernel/kernel.development.sweet
[ ! -e "$IMAGE" ] || { echo "image exists; preserve or remove explicitly" >&2; exit 1; }
[ ! -e "$OUTDIR/image-build.log" ] || { echo "build log exists; preserve or remove explicitly" >&2; exit 1; }
python3 "$OUTDIR/verify-image.py" --preflight
sh tools/nord2-image-from-kernel.sh "$KERNEL" "$IMAGE" \
 --base-boot research/nord2/rescue/recompressed/base.img \
 --zopfli ../toolchains/zopfli/zopfli --product-adt research/nord2/ipsw/d22ap.adt \
 debug=0xc cpus=8 keepsyms=1 -noprogress nord2stop=0 nord2vm=1 nord2fb=1 nord2stopat=23 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 nord2display=1 nord2input=1 nord2wifi=2 nord2fastlog=1 nord2ufsprofile=1 nord2fbdiag=1 nord2wififw=1 nord2nortc=1 nord2wifiemi=0 > "$OUTDIR/image-build.log" 2>&1
cat "$OUTDIR/image-build.log"
python3 "$OUTDIR/verify-image.py"
