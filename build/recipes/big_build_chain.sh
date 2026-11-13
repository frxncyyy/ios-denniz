#!/bin/bash
# big_build_chain.sh -- the ONE final combined build (12 Nov 2026). Modelled on phone_chain.sh. Prepared host-side, NOT executed.
#
# usage:  big_build_chain.sh --check            preflight only: hashes, files, nothing running, phone rootfs pre-state from the
#                                               mirror (read-only). Touches no phone. Run this first.
#         big_build_chain.sh TAG                phone in fastboot (or reachable by to_fastboot.sh). Three rootfs transactions
#                                               back to back, all with the SAME kernel image q10jk:
#           1. vt17       Nord2Metal y2s57 (346ccd1b -> e25c5ecf)              payload-vt17         owned-vt17.json
#           2. still4096  D22/AVCaptureSession.plist 4096x3072 (b66f008b -> 8e90184f)  payload-still4096  owned-still4096.json
#           3. vcodec     DEFAULT payload-vcodec-prod8: hardware ENCODE + hardware DECODE (bit-exact through VT at real sizes, dr6)
#                                                                              payload-vcodec-prod8 owned-vcodec-jpegdec.json
#           safe fallback: VCODEC_PAYLOAD=prod5-nohwdec (hardware encode, software decode). prod5/6/7: superseded (wrong pictures)
#         START_AT=still4096|vcodec big_build_chain.sh TAG   resume after a step that already finished ("capture done").
#         VCODEC_PAYLOAD=prod5-nohwdec [START_AT=vcodec] big_build_chain.sh TAG   safe fallback step 3 (default prod8)
#
# txn.sh takes ONE payload + ONE guest + ONE owned list, so the three are chained (each guest is idempotent and checks the
# exact pre-state before writing; a mismatch fails the offline step BEFORE commit, so nothing is written to the rootfs).
# The audio/camera bundle (audio.pkg 1aea9991) is not a rootfs step: it is inside the boot image (ADT nord2-audio-package),
# installed by the kernel at boot. The kernel image is flashed by every txn (same file, same sha each time).
# Recovery if a step FAILS after "rescue up": the boot partition then holds the RAM ADB rescue image; flash ONLY the exact
# image below (q10jk) to boot -> the phone boots the rootfs as the last committed step left it. See gpu/hwcodec/BIG_BUILD.md.
set -u
W=$NORD2_WORK
MAIN=$NORD2_REPO
B=$W/gpu/build; A=$B/autotest; D=$B/autocycle; P=$B/apps-20261104; FB=~/Android/Sdk/platform-tools/fastboot
IMG=$B/device-mmu-q10jk-candidate/ui-sched/nord2-ios-metal-q10jk-ui654fb60dvfs-cap3500-ram3776-sched.img
IMG_SHA=36aca6cf8bddd2423f8c387c571f32f648f57addbb262b18eed46691044209e7
MODE=${1:?usage: big_build_chain.sh --check | TAG}

# ---- pinned artifacts (sha256). Payload files = plan.json hashes; plan.json itself pinned too (it names the addon).
PIN="
$IMG_SHA $IMG
d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5 $MAIN/research/nord2/rescue/nord2-adb-rescue.img
e25c5ecf650bf55726e076f653054db3415de8113f1f4464d9b78d49469cb63c $P/payload-vt17/Nord2Metal
2eb4d3e254b09d25f3a0c26a04fa8808ee8172bc8298d34f1dd657d98e9bdceb $P/payload-vt17/plan.json
8e90184ffdf634c03dbf3f317f28aa4b35b98546210457d2a257cb1a3c25edfa $P/payload-still4096/D22/AVCaptureSession.plist
bde4306608a3c784f838d9a6ec0887c8dda33510ffa969d0d784873009772827 $P/payload-still4096/plan.json
f57bdd4213c4e26ecb33f0ab86558aebf3906fb15aee2ab8d2dae64c92a5ec27 $P/payload-vcodec-prod8/plan.json
8fbbd9ab8e10769e843aa54e6549c4950f0562c9d8ed59320dbadecfb9dc6e93 $P/payload-vcodec-prod8/vcodecd
91bd53afc41bc5726eb7f656790331e3cd7e0aaccd1906c282f4216d3f0e08fc $P/payload-vcodec-prod8/Slim.videoencoder
68f71610492faffa59d9d036d7641a0600af686c2fb4b620169d9a71234843de $P/payload-vcodec-prod8/Slim.videodecoder
82afe7221d1e906c6d76fdbd8cbe437c96d223a51952c5ff1152ec5250e9597b $P/payload-vcodec-prod8/vendor/bin/vpud
07130d2a0d6cea60d148763c5ed8722a1abacaa0f0f7a1174dd71321f7274eae $P/payload-vcodec-prod8/vendor/lib/libvpud_vcodec.so
5e69ccfca0728d28b4e991d27a30926c0243abf4d3255d9e9619169dbc00b091 $P/payload-vcodec-prod8/vendor/lib/libvcodecdrv.so
e26673ed13ee73351f1b1cf6dcba1d58762fd7dfc585eda97c50d78805b54fd3 $P/payload-vcodec-prod8/vendor/lib/libvcodec_utility.so
cd5c5b6c40f23acff1c1936113b8c4604f7e8d46429d6c260ff8f5a5cb7029d6 $P/payload-vcodec-prod8/vendor/lib/libvcodec_oal.so
d75639555670b5f4268b2d6f626aa3e2fd80be432f2581ce3cfb7962d4f54725 $P/payload-vcodec-prod5-nohwdec/plan.json
3874f1fe82fda3d57af8810e7af165cbf5153302c11f63dadce543fc2fda1085 $P/payload-vcodec-prod5-nohwdec/Slim.videodecoder
8fbbd9ab8e10769e843aa54e6549c4950f0562c9d8ed59320dbadecfb9dc6e93 $P/payload-vcodec-prod5-nohwdec/vcodecd
91bd53afc41bc5726eb7f656790331e3cd7e0aaccd1906c282f4216d3f0e08fc $P/payload-vcodec-prod5-nohwdec/Slim.videoencoder
82afe7221d1e906c6d76fdbd8cbe437c96d223a51952c5ff1152ec5250e9597b $P/payload-vcodec-prod5-nohwdec/vendor/bin/vpud
07130d2a0d6cea60d148763c5ed8722a1abacaa0f0f7a1174dd71321f7274eae $P/payload-vcodec-prod5-nohwdec/vendor/lib/libvpud_vcodec.so
5e69ccfca0728d28b4e991d27a30926c0243abf4d3255d9e9619169dbc00b091 $P/payload-vcodec-prod5-nohwdec/vendor/lib/libvcodecdrv.so
e26673ed13ee73351f1b1cf6dcba1d58762fd7dfc585eda97c50d78805b54fd3 $P/payload-vcodec-prod5-nohwdec/vendor/lib/libvcodec_utility.so
cd5c5b6c40f23acff1c1936113b8c4604f7e8d46429d6c260ff8f5a5cb7029d6 $P/payload-vcodec-prod5-nohwdec/vendor/lib/libvcodec_oal.so
e0b1cdabe1c564537403fb650b2710e02c852ea8a633e8f8e8de2183ad30e989 $P/guest-vt17.sh
1e63b2f7b2acac95f6e36b5f1d2287860eaf6d283cf4a9b097f4d3a5e66c03b1 $P/guest-still4096.sh
ec14b557b490868a894a994de6454b811287360d66429f9ed3542271c154d220 $P/guest-vcodec.sh
eb2525e367ab9e055b39a8e0fd556ac33b39a447668350ee7091b545e09d470f $P/owned-vt17.json
6d61ceb4159a86d7ac5a24c0a47596447311d6973d9584bfb4878ce04f7d3092 $P/owned-still4096.json
24b418221baa1f6282480d476e42916bdfb53386a9ea4bf53d4d7338874068ea $P/owned-vcodec-jpegdec.json
3a52df3e58bf93a51e634a9587ae8b536ae8ed4b91f7b98b7a28ed61dd6fba16 $D/txn.sh
"
preflight() {
  local bad=0
  echo "$PIN" | sed '/^$/d' | sha256sum -c --quiet - || bad=1
  for f in $D/to_fastboot.sh $D/flash_boot.sh $A/phone.py $W/gpu/tools/snapshot_current_root_fast.py \
           $W/gpu/tools/check_update_catalog_fast.py $W/gpu/tools/commit_root_delta_fast.py $P/generic_update_offline.py \
           $MAIN/research/nord2/rootfs-vm/initramfs $W/tools/nord2-usb-console.py $B/mirror/rootfs-mirror.img $FB; do
    [ -e "$f" ] || { echo "MISSING $f"; bad=1; }
  done
  if pgrep -f 'queue_run.sh|autocycle/txn.sh|phone_chain.sh' > /dev/null; then
    echo "BUSY: a queue/txn/chain is running (pgrep -af 'queue_run.sh|txn.sh'): wait for it, do not overlap"; bad=1
  fi
  # phone rootfs pre-state as of the last snapshot (the mirror); read-only
  local T; T=$(mktemp -d); local H="python3 $D/hfsx.py $B/mirror/rootfs-mirror.img"
  $H get /System/Library/Extensions/Nord2Metal.bundle $T Nord2Metal libredwoodair.dylib > /dev/null 2>&1
  $H get /System/Library/PrivateFrameworks/CMCapture.framework/D22 $T AVCaptureSession.plist > /dev/null 2>&1
  local m l c; m=$(sha256sum < $T/Nord2Metal 2>/dev/null | cut -c1-64); l=$(sha256sum < $T/libredwoodair.dylib 2>/dev/null | cut -c1-64)
  c=$(sha256sum < $T/AVCaptureSession.plist 2>/dev/null | cut -c1-64); rm -rf $T
  case $m in 346ccd1b81325e0f*) echo "mirror Nord2Metal = vt16 (vt17 will install)";; e25c5ecf650bf557*) echo "mirror Nord2Metal = vt17 already";;
              *) echo "PRESTATE Nord2Metal unexpected: $m (guest-vt17.sh would refuse)"; bad=1;; esac
  [ "$l" = 6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea ] || { echo "PRESTATE libredwoodair.dylib unexpected: $l"; bad=1; }
  case $c in b66f008bdf095aa8*) echo "mirror D22 plist = dual-wide 4032 (still4096 will install)";; 8e90184ffdf634c0*) echo "mirror D22 plist = 4096 already";;
              *) echo "PRESTATE AVCaptureSession.plist unexpected: $c (guest-still4096.sh would refuse)"; bad=1;; esac
  [ $bad = 0 ] && echo "PREFLIGHT_OK image q10jk ${IMG_SHA:0:16}" || echo "PREFLIGHT_FAILED"
  return $bad
}
if [ "$MODE" = --check ]; then preflight; exit $?; fi
TAG=$MODE
preflight || exit 1
[ -e $B/cc-txn-$TAG-vt17 ] && [ "${START_AT:-vt17}" = vt17 ] && { echo "cc-txn-$TAG-vt17 exists: pick a new TAG or set START_AT"; exit 1; }

step() { # name payload guest owned
  echo "$(date -Is) === $1 ($2, $(basename $4), image ${IMG_SHA:0:16})"
  env PAYLOAD=$2 GUEST=$3 OWNED=$4 timeout 900 bash $D/txn.sh $IMG $TAG-$1 > $D/runs-$TAG-$1.out 2>&1
  tail -3 $B/cc-txn-$TAG-$1/events
  if grep -q FAILED $B/cc-txn-$TAG-$1/events || ! grep -q "capture done" $B/cc-txn-$TAG-$1/events; then
    echo "$(date -Is) STOP: step $1 did not finish. Read $B/cc-txn-$TAG-$1/{events,offline.log,catalog.log} and $D/runs-$TAG-$1.out."
    echo "If 'rescue up' is in events but 'kernel flashed' is not, boot holds the rescue image: flash ONLY $IMG (sha ${IMG_SHA:0:16}) to boot."
    exit 1
  fi
}
back_to_fastboot() {
  timeout 200 python3 $A/phone.py wait 200 > /dev/null 2>&1
  sleep 20
  timeout 60 python3 $A/phone.py reset fastboot | tail -1
  sleep 20
  $FB devices | head -1
}
case ${VCODEC_PAYLOAD:-prod8} in prod8|prod5-nohwdec) ;; *) echo "VCODEC_PAYLOAD must be prod8 or prod5-nohwdec (prod5/6/7 are superseded: wrong pictures)"; exit 1;; esac
case ${START_AT:-vt17} in
  vt17)      step vt17 payload-vt17 guest-vt17.sh $P/owned-vt17.json; back_to_fastboot;&
  still4096) step still4096 payload-still4096 guest-still4096.sh $P/owned-still4096.json; back_to_fastboot;&
  vcodec)    step vcodec payload-vcodec-${VCODEC_PAYLOAD:-prod8} guest-vcodec.sh $P/owned-vcodec-jpegdec.json;;
  *) echo "START_AT must be vt17, still4096 or vcodec"; exit 1;;
esac
echo "$(date -Is) chain done. Final boot = q10jk + vt17 + still4096 + production vcodec (vcodecd RunAtLoad). Checks:"
echo "  grep -a 'NORD2 VCODEC\|N2VCD\|NORD2 MIC\|FAN53870\|NORD2 JPEG' $D/runs/$TAG-vcodec/ui-usb/xnu-live.log | head"
echo "  Camera video test: gpu/hwcodec/CAMERA_INTEGRATION.md section 2 (phone.py scenario video, cam_check.py log)"
