#!/bin/sh
# Private offline image preparation. False source/CI/schema gates block it.
# Parent alone may run after promoting and verifying exact reviewed sources.
set -euC
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
outdir=research/nord2/wifi-ios-profile/ios
image=shim/out/nord2-xnu/nord2-ios-wifi-scan-profile-battery-stop23.img
[ ! -e "$image" ] || { echo 'image exists; preserve explicitly' >&2; exit 1; }
[ ! -e "$outdir/image-build.log" ] || { echo 'build log exists; preserve explicitly' >&2; exit 1; }
python3 "$outdir/verify-image.py" --preflight
kernel=$(python3 "$outdir/verify-image.py" --kernel-path)
cmdline=$(python3 "$outdir/verify-image.py" --commandline)
temp=$(mktemp -d)
trap 'rm -rf "$temp"' EXIT HUP INT TERM
exec > "$outdir/image-build.log" 2>&1
python3 tools/nord2-check-rcpc2.py "$kernel"
../toolchains/cctools/bin/aarch64-apple-darwin-strip -S -x "$kernel" -o "$temp/kernel.strip"
python3 tools/mkadt-nord2.py "$temp/base.adt" --product-adt research/nord2/ipsw/d22ap.adt --wifi-service
python3 research/nord2/wifi/next-stage/main-fw-transport/main-fw-native-v2/inject_cal_adt.py \
 --enable-nord2wifimain "$temp/base.adt" research/nord2/wifi/calibration/WIFI "$temp/main.adt"
python3 tools/mkpayload.py "$temp/kernel.strip" "$temp/main.adt" "$temp/payload.bin" \
 --cmdline "$cmdline"
sh tools/nord2-build-loader.sh "$temp/loader"
python3 tools/nord2-pack-xnu.py research/nord2/rescue/recompressed/base.img \
 "$temp/loader/loader-Image" "$temp/payload.bin" "$image" \
 --zopfli ../toolchains/zopfli/zopfli
python3 tools/nord2-check-xnu.py "$image" "$temp/payload.bin" \
 --stock research/nord2/rescue/recompressed/base.img
python3 "$outdir/verify-image.py"
