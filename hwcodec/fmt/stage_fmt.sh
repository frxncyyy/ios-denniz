#!/bin/bash
# SPDX-License-Identifier: MIT
# opus-fmt: build + stage payload-vcodec-fmt (tag vc7-fmt): ONE boot decodes one small clip per hardware decoder format.
#   bash gpu/hwcodec/fmt/stage_fmt.sh            # host only: never touches the phone
# Steps: (1) fmt/run_sim_fmt.sh builds the patched /tmp copy of the tree (fmt/patches/*.patch) and rehearses every clip on the
# PC through the product path (vcodecd --decoder + n2vcd_dprobe); (2) gpu/hwcodec/ios/build.sh from that copy -> /tmp/n2hw-fmt/ios;
# (3) gpu/build/apps-20261104/stage-vcodec.sh with the clips in this order: H.264 control (the phone's own vc14 stream, bit-exact
# in vc22), MPEG-2, MPEG-4, H.263, VP8, VP9, AV1.  vcodecd --dec-wc off (first contact of new formats: no write-combining).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$HERE/.." && pwd)
A=$(cd "$W/../build/apps-20261104" && pwd)
SDK=$(cd "$W/../build/apple-toolchain/sdks/iPhoneOS16.5.sdk" && pwd)
OUT=/tmp/n2hw-fmt
TAG=${TAG:-vc7-fmt}
S=$HERE/streams
CLIPS=("$W/decint/streams/vc14-probe43-640x480-baseline.h264" "$S/f320-mpeg2.m2v" "$S/f320-mpeg4.m4v" "$S/f352-h263.h263"
       "$S/f320-vp8.ivf" "$S/f320-vp9.ivf" "$S/f320-av1.ivf")
[ -s "$S/f320-av1.ivf" ] || bash "$HERE/make_streams.sh"
OUT=$OUT/sim bash "$HERE/run_sim_fmt.sh" "${CLIPS[@]}"
cp "$W/ios/build.sh" "$OUT/sim/wtree/ios/build.sh"
(cd "$OUT/sim/wtree" && N2_IOS_SDK=$SDK nice -n 10 bash ios/build.sh "$OUT/ios" | tail -7)
DEC=(); for c in "${CLIPS[@]}"; do DEC+=(--decoder "$c"); done
bash "$A/stage-vcodec.sh" "$TAG" --tools "$OUT/ios" --vcodecd "$OUT/ios/vcodecd" --payload "$A/payload-vcodec-fmt" \
	--vcodecd-arg --dec-wc --vcodecd-arg off "${DEC[@]}"
echo "STAGED $A/payload-vcodec-fmt ($TAG)"
