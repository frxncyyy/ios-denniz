#!/bin/bash
# SPDX-License-Identifier: MIT
# opus-ios, 11 Nov 2026. Makes the rootfs transaction payload for gpu/hwcodec (hardware video encoder host) from built products.
#
# usage: stage-vcodec.sh TAG [options]
#   --tools DIR        n2vcd_selftest + n2vcd_probe (gpu/hwcodec/ios/build.sh output; default /tmp/n2hw-opus-ios/out)
#   --vcodecd FILE     the linked arm64 vcodecd (gpu/hwcodec/build_ios.sh output, e.g. /tmp/n2hw-ip/ios/vcodecd); omitted =
#                      payload without the daemon (selftest-only transaction: needs no kernel change)
#   --on none          no gate at all (only the --off gates are removed; e.g. a switches-only measurement payload)
#   --on NAME / --off NAME   gate files /private/var/db/nord2-vcodec-NAME to create / remove; NAME in run|selftest|probe
#                      default: --on selftest, plus --on run --on probe when --vcodecd is given
#   --keepalive        com.nord2.vcodecd gets KeepAlive {SuccessfulExit: false} (restart after a non-zero exit, e.g. 70 on a
#                      guest stop); default: KeepAlive false (first runs).  (opus-enc4k) When the staged vcodecd knows
#                      --restart-on-switch, --keepalive also passes it (plan restart_on_switch): an OPEN of another codec than
#                      the previous session's makes the daemon exit 75 and launchd starts a fresh one (vc18 H.264->HEVC failure)
#   --probe-args "..." replaces the probe's default arguments after --gate (default: --delay 45 --wait-server 120 --codec avc1
#                      --size 640x480 --frames 10)
#   --no-restart       (opus-enc4k) with --keepalive: do not pass --restart-on-switch (the heap fix of 12 Nov makes
#                      sessions of any codec reusable in one process; keepalive then only restarts after a crash)
#   --vcodecd-arg ARG  (opus-enc4k) extra vcodecd argument in the launchd job (repeatable; checked against the binary's
#                      strings), e.g. --venc-keep-power
#   --preset NAME      (opus-enc4k) named probe argument sets for the 1080p/4K tests (needs the 11 Nov probe with --level auto,
#                      --open-timeout, --then; an older probe exits 2 "bad argument"): 720p30 1080p30 1080p60 4k30 4kdci
#                      hevc1080p30 hevc4k30, ladder-rs (High/Main ladder for --keepalive restart-on-switch), q (H.264 quality bisect, 7 sessions), ladder-lvl (= ladder with --level -1: vcodecd >= 79c6004d picks the level), and ladder = 1080p30 AVC -> 4K30 AVC -> 1080p30 HEVC -> 4K30 HEVC -> 4096x2160 AVC in
#                      ONE boot (the probe re-executes itself per session, see n2vcd_probe.c --then).  Bitrate = vcodecd's
#                      w*h*4 default, level from size*fps.  A later --probe-args replaces it.
#   --payload DIR      default gpu/build/apps-20261104/payload-vcodec (must be on the work disk: the offline step hard-links it)
#   --encoder-plugin FILE  (opus-lead2) Slim.videoencoder to install at /System/Library/VideoEncoders/Slim.videoencoder in the
#                      same transaction: gpu/video plug-in + P04 hardware encoders (gpu/hwcodec/ios/build_plugins.sh). P04
#                      registers only while /private/var/db/nord2-video-claim-hw exists; otherwise the registration is the
#                      software one of today (n2avc / VCPHEVC / n2jpeg), so recording works with vcodecd down.
#   --decoder-plugin FILE  Slim.videodecoder for /System/Library/VideoDecoders/Slim.videodecoder (DEC06); registers nothing unless
#                      /private/var/db/nord2-video-hwdec exists (then Apple H264SW/VCPHEVC stay the fallback).
#   --decoder STREAM   (opus-decint) hardware DECODE test, repeatable (up to 8 streams, decoded in this order): needs --vcodecd
#                      (built with N2VCD_WITH_DECODER: gpu/hwcodec/ios/build.sh) and TOOLS/n2vcd_dprobe.  Adds the streams
#                      (/usr/local/lib/n2vendor/streams/), n2vcd_dprobe, vcodecd --decoder --guest-log 300, the job
#                      com.nord2.vcodec-dprobe and its gate (default --on dprobe in addition to the other default gates).
#                      Codec from the extension (.h264 avc1, .h265 hvc1).  Procedure: gpu/hwcodec/DECODE_PHONE_TEST.md
#   --dprobe-args "..." replaces the decode probe's arguments after the streams (default: --delay 50 --after
#                      /private/var/db/nord2-vcodec/probe.log --after-timeout 240 --wait-server 180 --mm21-first)
#   --jpeg-test FILE   (opus-jpeg) the on-phone test of /dev/redwood-jpeg: FILE = n2jpeg_phone (gpu/hwcodec/jpeg/phone/
#                      build_ios.sh).  Installs /usr/local/libexec/n2jpeg_phone and the one-shot launchd job com.nord2.jpeg-selftest
#                      (gate /private/var/db/nord2-vcodec-jpeg; default --on jpeg in addition to the other default gates; give
#                      --on jpeg alone for a JPEG-only transaction).  Results: /private/var/db/nord2-vcodec/jpeg-selftest.log (fsync per line)
#                      + out/jpeg-WxH-qQ.jpg + out/jdec-*.yuv.  Needs no vcodecd and no kernel change beyond 0310 (q10jf/q10jg).
#                      Procedure: gpu/hwcodec/JPEG_PHONE_TEST.md
#   --jpeg-dec FILE    (opus-jpeg) a baseline .jpg for the decode step, repeatable (up to 8): /usr/local/lib/n2vendor/jpeg/
#   --jpeg-args "..."  replaces n2jpeg_phone's arguments after --gate/--out/--log/--dec (default: --delay 20)
#   --video-on NAME / --video-off NAME  switch files /private/var/db/nord2-video-NAME (e.g. claim-hw, hwdec, hwdec-no-hevc).
#                      Default when a plug-in is given: --video-off claim-hw --video-off hwdec (hardware VT paths OFF; the
#                      boot probe talks to vcodecd directly and needs no switch). The undo removes only switches it created.
#   --prod             (opus-cam, 12 Nov 2026) the PRODUCTION payload for Camera/Photos: vcodecd only (no selftest, no probes,
#                      no streams), launchd job com.nord2.vcodecd with RunAtLoad + KeepAlive {SuccessfulExit: false} +
#                      --restart-on-switch (+ --decoder when a decoder plug-in is given), gate 'run' ON permanently, the
#                      one-shot gates selftest/probe/dprobe OFF, both plug-ins installed, video switches claim-hw + hwdec ON
#                      (unless --video-on/--video-off are given).  Needs --vcodecd and --encoder-plugin (--decoder-plugin
#                      recommended).  Procedure: gpu/hwcodec/CAMERA_INTEGRATION.md
#
# The five vendor objects are the user's own phone files (gpu/build/cam-vendor-20261109/vfs): they are hard-linked into the
# payload (no copy on disk), checked against the sha256 pins below, never committed, never published.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd)                  # gpu/build/apps-20261104
GPU=$(cd "$A/../.." && pwd)                       # work/gpu
VFS=$GPU/build/cam-vendor-20261109/vfs
TAG=${1:?usage: stage-vcodec.sh TAG [options]}; shift
TOOLS=/tmp/n2hw-opus-ios/out; VCODECD=""; ON=(); OFF=(); KEEPALIVE=false; OUT=$A/payload-vcodec
ENCPLUG=""; DECPLUG=""; VON=(); VOFF=(); VARGS=(); DSTREAMS=(); PROD=false
JPEGT=""; JDECS=(); JPEG_ARGS="--delay 20"
DPROBE_ARGS="--delay 50 --after /private/var/db/nord2-vcodec/probe.log --after-timeout 240 --wait-server 180 --mm21-first"
PROBE_ARGS="--delay 45 --wait-server 120 --codec avc1 --size 640x480 --frames 10"
preset() {
	local c="--delay 45 --wait-server 120 --hex 256 --open-timeout 90 --frame-timeout 15"
	case $1 in
	720p30) echo "$c --codec avc1 --size 1280x720 --frames 30 --fps 30" ;;
	1080p30) echo "$c --codec avc1 --size 1920x1080 --frames 30 --fps 30" ;;
	1080p60) echo "$c --codec avc1 --size 1920x1080 --frames 60 --fps 60" ;;
	4k30) echo "$c --codec avc1 --size 3840x2160 --frames 30 --fps 30" ;;
	4kdci) echo "$c --codec avc1 --size 4096x2160 --frames 10 --fps 25" ;;   # 25 fps keeps level 5.1 (30 fps = 5.2)
	hevc1080p30) echo "$c --codec hvc1 --size 1920x1080 --frames 30 --fps 30" ;;
	hevc4k30) echo "$c --codec hvc1 --size 3840x2160 --frames 30 --fps 30" ;;
	ladder-lvl) echo "$c --level -1 --codec avc1 --size 1920x1080 --frames 30 --fps 30 --then avc1:3840x2160:30:30,hvc1:1920x1080:30:30,hvc1:3840x2160:30:30,avc1:4096x2160:10:25" ;;   # level 0 sent: vcodecd chooses
	q) echo "$c --codec avc1 --size 1920x1080 --frames 30 --fps 30 --profile 66 --level 41 --then avc1:1920x480:30:30:66:40,avc1:640x1080:30:30:66:31,avc1:1280x720:30:30:66:31,avc1:640x480:30:30:66:30,avc1:1920x1080:30:30:77:41,avc1:1920x1080:30:30:100:41" ;;   # H.264 quality bisect: size/width/height, then Main, High (last: a refused profile may poison the guest)
	fast) echo "$c --codec avc1 --size 3840x2160 --frames 30 --fps 30 --profile 100 --level 51 --venc-fast 0 --then avc1:3840x2160:30:30:100:51:fast,avc1:4096x2160:10:25:100:51:fast,avc1:1920x1080:60:60:100:42:fast,hvc1:3840x2160:30:30:1:153:fast,hvc1:1920x1080:60:60:1:123:fast" ;;   # q10jg (0312) + --keepalive: 4K H.264 High 273 vs 624 MHz, 4096 (vendor caps: refused E_ARGS), 1080p60 H.264, HEVC 4K30 + 1080p60
	reopen) echo "$c --codec avc1 --size 1920x1080 --frames 30 --fps 30 --profile 100 --then hvc1:1920x1080:30:30:1,hvc1:3840x2160:30:30:1,avc1:3840x2160:30:30:100,hvc1:1920x1080:60:60:1,avc1:1280x720:30:30:100" ;;   # heap-fix proof: codec switches and HEVC reopens in ONE vcodecd process (stage with --keepalive --no-restart)
	hevc4k) echo "$c --codec hvc1 --size 3840x2160 --frames 30 --fps 30 --profile 1 --then hvc1:3840x2160:30:30:1,hvc1:3840x1088:30:30:1,hvc1:1920x2160:30:30:1,hvc1:1920x1080:30:30:1" ;;   # vc24 HEVC 4K hang bisect: fresh-process 4K twice, width-only, height-only, 1080p (every HEVC session restarts the daemon: --keepalive)
	ladder-rs) echo "$c --codec avc1 --size 1920x1080 --frames 30 --fps 30 --profile 100 --then avc1:3840x2160:30:30:100,hvc1:1920x1080:30:30:1,hvc1:3840x2160:30:30:1,avc1:4096x2160:10:25:100" ;;   # with --keepalive + a vcodecd with --restart-on-switch: H.264 High, HEVC Main, codec switches restart the daemon
	ladder) echo "$c --codec avc1 --size 1920x1080 --frames 30 --fps 30 --then avc1:3840x2160:30:30,hvc1:1920x1080:30:30,hvc1:3840x2160:30:30,avc1:4096x2160:10:25" ;;
	*) echo "stage-vcodec.sh: unknown preset $1" >&2; return 1 ;;
	esac
}
while [ $# -gt 0 ]; do case $1 in
	--tools) TOOLS=$2; shift ;; --vcodecd) VCODECD=$2; shift ;; --on) ON+=("$2"); shift ;; --off) OFF+=("$2"); shift ;;
	--keepalive) KEEPALIVE=true ;; --probe-args) PROBE_ARGS=$2; shift ;; --payload) OUT=$2; shift ;;
	--preset) PROBE_ARGS=$(preset "$2") || exit 2; shift ;;
	--vcodecd-arg) VARGS+=("$2"); shift ;;
	--no-restart) NORESTART=1 ;;
	--encoder-plugin) ENCPLUG=$2; shift ;; --decoder-plugin) DECPLUG=$2; shift ;;
	--video-on) VON+=("$2"); shift ;; --video-off) VOFF+=("$2"); shift ;;
	--decoder) DSTREAMS+=("$2"); shift ;; --dprobe-args) DPROBE_ARGS=$2; shift ;;
	--prod) PROD=true ;;
	--jpeg-test) JPEGT=$2; shift ;; --jpeg-dec) JDECS+=("$2"); shift ;; --jpeg-args) JPEG_ARGS=$2; shift ;;
	*) echo "stage-vcodec.sh: unknown $1" >&2; exit 2 ;; esac; shift; done
if [ "$PROD" = true ]; then   # opus-cam: production preset
	[ -n "$VCODECD" ] || { echo "--prod needs --vcodecd" >&2; exit 2; }
	[ -n "$ENCPLUG" ] || { echo "--prod needs --encoder-plugin" >&2; exit 2; }
	[ ${#DSTREAMS[@]} -eq 0 ] || { echo "--prod takes no --decoder test streams" >&2; exit 2; }
	KEEPALIVE=true
	[ ${#OFF[@]} -gt 0 ] || OFF=(selftest probe dprobe jpeg)
	case " ${OFF[*]} " in *" run "*) ;; *) [ ${#ON[@]} -gt 0 ] || ON=(run) ;; esac   # --off run: daemon installed, not started (fallback test)
	if [ ${#VON[@]} -eq 0 ] && [ ${#VOFF[@]} -eq 0 ]; then
		VON=(claim-hw); VOFF=(hwenc-sync)
		if [ -n "$DECPLUG" ]; then VON+=(hwdec); else VOFF+=(hwdec); fi
	fi
	if [ -n "$DECPLUG" ]; then
		case $(strings "$VCODECD") in *--decoder*) VARGS+=(--decoder) ;;
			*) echo "--prod with a decoder plug-in needs a vcodecd built with the decoder service (ios/build.sh)" >&2; exit 1 ;; esac
	fi
	case $(strings "$VCODECD") in *--restart-on-switch*) ;; *) echo "--prod needs a vcodecd with --restart-on-switch" >&2; exit 1 ;; esac
fi
[ ${#ON[@]} -gt 0 ] || [ "$PROD" = true ] || { ON=(selftest); [ -n "$VCODECD" ] && ON+=(run probe); [ ${#DSTREAMS[@]} -gt 0 ] && ON+=(dprobe); [ -n "$JPEGT" ] && ON+=(jpeg); }
[ "${ON[*]}" = none ] && ON=()   # opus-jpeg: --on none = no gate on (e.g. the HEIC measurement: only switches)
for n in "${ON[@]}" "${OFF[@]}"; do case $n in run|selftest|probe|dprobe|jpeg) ;; *) echo "bad gate $n" >&2; exit 2 ;; esac; done
if [ ${#JDECS[@]} -gt 0 ]; then
	[ -n "$JPEGT" ] || { echo "--jpeg-dec needs --jpeg-test" >&2; exit 2; }
	[ ${#JDECS[@]} -le 8 ] || { echo "at most 8 --jpeg-dec files" >&2; exit 2; }   # n2jpeg_phone takes 8 --dec
	for f in "${JDECS[@]}"; do case $(basename "$f") in [A-Za-z0-9]*.jpg) [ -s "$f" ] || { echo "no file $f" >&2; exit 2; } ;;
		*) echo "jpeg $f must be NAME.jpg" >&2; exit 2 ;; esac; done
fi
if [ ${#DSTREAMS[@]} -gt 0 ]; then
	[ -n "$VCODECD" ] || { echo "--decoder needs --vcodecd" >&2; exit 2; }
	[ ${#DSTREAMS[@]} -le 12 ] || { echo "at most 12 --decoder streams" >&2; exit 2; }
	# opus-fmt (12 Nov): .ivf (VP8/VP9/AV1, codec from the IVF fourcc), .m2v, .m4v, .h263 need an n2vcd_dprobe/vcodecd built
	# with gpu/hwcodec/fmt/patches (fmt/stage_fmt.sh); the tree's dprobe refuses them (exit 2) without touching vcodecd.
	for f in "${DSTREAMS[@]}"; do case $f in *.h264|*.h265|*.ivf|*.m2v|*.m4v|*.h263) [ -s "$f" ] || { echo "no stream $f" >&2; exit 2; } ;;
		*) echo "stream $f must end in .h264, .h265, .ivf, .m2v, .m4v or .h263" >&2; exit 2 ;; esac; done
fi
if [ -n "$ENCPLUG$DECPLUG" ] && [ ${#VON[@]} -eq 0 ] && [ ${#VOFF[@]} -eq 0 ]; then VOFF=(claim-hw hwdec); fi
for n in "${VON[@]}" "${VOFF[@]}"; do case $n in claim-hw|hwdec|hwdec-no-hevc|hwenc-sync|off|no-avc|no-hevc|no-jpeg|quiet|trace|probe|camera-keephevc) ;;
	*) echo "bad video switch $n" >&2; exit 2 ;; esac; done
for n in "${VON[@]}"; do for m in "${VOFF[@]}"; do [ "$n" != "$m" ] || { echo "switch $n both on and off" >&2; exit 2; }; done; done

declare -A PIN=(
	[bin/vpud]=82afe7221d1e906c6d76fdbd8cbe437c96d223a51952c5ff1152ec5250e9597b
	[lib/libvpud_vcodec.so]=07130d2a0d6cea60d148763c5ed8722a1abacaa0f0f7a1174dd71321f7274eae
	[lib/libvcodecdrv.so]=5e69ccfca0728d28b4e991d27a30926c0243abf4d3255d9e9619169dbc00b091
	[lib/libvcodec_utility.so]=e26673ed13ee73351f1b1cf6dcba1d58762fd7dfc585eda97c50d78805b54fd3
	[lib/libvcodec_oal.so]=cd5c5b6c40f23acff1c1936113b8c4604f7e8d46429d6c260ff8f5a5cb7029d6 )

rm -rf "$OUT"; mkdir -p "$OUT/vendor/bin" "$OUT/vendor/lib"
if [ "$PROD" = false ]; then   # opus-cam: the production payload carries no test tools
for b in n2vcd_selftest n2vcd_probe; do
	file "$TOOLS/$b" | grep -q 'Mach-O 64-bit arm64 executable' || { echo "$TOOLS/$b is not an arm64 Mach-O" >&2; exit 1; }
	cp "$TOOLS/$b" "$OUT/$b"; chmod 755 "$OUT/$b"
done
PSTR=$(strings "$OUT/n2vcd_probe")
for o in $PROBE_ARGS; do case $o in --*)
	grep -qx -- "$o" <<< "$PSTR" || { echo "n2vcd_probe in $TOOLS does not know $o (rebuild: gpu/hwcodec/ios/build.sh)" >&2; exit 1; } ;;
esac; done
fi
if [ ${#DSTREAMS[@]} -gt 0 ]; then
	file "$TOOLS/n2vcd_dprobe" | grep -q 'Mach-O 64-bit arm64 executable' || { echo "$TOOLS/n2vcd_dprobe is not an arm64 Mach-O" >&2; exit 1; }
	cp "$TOOLS/n2vcd_dprobe" "$OUT/n2vcd_dprobe"; chmod 755 "$OUT/n2vcd_dprobe"
	case $(strings "$VCODECD") in *--decoder*) ;; *) echo "$VCODECD has no --decoder (build with gpu/hwcodec/ios/build.sh)" >&2; exit 1 ;; esac
	mkdir -p "$OUT/streams"; : > "$OUT/stream_order"
	for f in "${DSTREAMS[@]}"; do cp "$f" "$OUT/streams/$(basename "$f")"; echo "$(basename "$f")" >> "$OUT/stream_order"; done
fi
if [ -n "$JPEGT" ]; then
	file "$JPEGT" | grep -q 'Mach-O 64-bit arm64 executable' || { echo "$JPEGT is not an arm64 Mach-O" >&2; exit 1; }
	JSTR=$(strings -n 3 "$JPEGT")
	for o in --gate --out --log --dec $JPEG_ARGS; do case $o in --*)
		grep -qx -- "$o" <<< "$JSTR" || { echo "$JPEGT does not know $o (rebuild: gpu/hwcodec/jpeg/phone/build_ios.sh)" >&2; exit 1; } ;;
	esac; done
	cp "$JPEGT" "$OUT/n2jpeg_phone"; chmod 755 "$OUT/n2jpeg_phone"
	mkdir -p "$OUT/jpeg"; : > "$OUT/jpeg_order"
	for f in "${JDECS[@]}"; do cp "$f" "$OUT/jpeg/$(basename "$f")"; echo "$(basename "$f")" >> "$OUT/jpeg_order"; done
fi
if [ -n "$VCODECD" ]; then
	file "$VCODECD" | grep -q 'Mach-O 64-bit arm64 executable' || { echo "$VCODECD is not an arm64 Mach-O" >&2; exit 1; }
	# the launchd job passes --gate; an older vcodecd without it would exit 2 at every boot ("unknown argument")
	case $(strings "$VCODECD") in *--gate*) ;; *) echo "$VCODECD has no --gate option (ask hw-ip; see the board)" >&2; exit 1 ;; esac
	# the shim-table archive defect: a vcodecd linked without -force_load has no thread/stdio shim tables
	SYMS=$(llvm-nm "$VCODECD" 2>/dev/null || true)
	case $SYMS in *" _n2shim_thread"*) ;; *) echo "$VCODECD lacks _n2shim_thread: relink with -force_load libn2vhost.a" >&2; exit 1 ;; esac
	cp "$VCODECD" "$OUT/vcodecd"; chmod 755 "$OUT/vcodecd"
fi
# VideoToolbox plug-ins: arm64 dylib, install name = the path VideoToolbox dlopens, the one entry it calls
plugcheck() {   # FILE INSTALLPATH ENTRY
	file "$1" | grep -q 'Mach-O 64-bit arm64 dynamically linked shared library' || { echo "$1 is not an arm64 dylib" >&2; exit 1; }
	llvm-objdump --macho --dylib-id "$1" | grep -qx "$2" || { echo "$1 install name is not $2" >&2; exit 1; }
	llvm-nm -gU "$1" | awk '{print $NF}' | grep -qx "_$3" || { echo "$1 does not export _$3" >&2; exit 1; }
}
if [ -n "$ENCPLUG" ]; then
	plugcheck "$ENCPLUG" /System/Library/VideoEncoders/Slim.videoencoder SLIMEncoderRegister
	cp "$ENCPLUG" "$OUT/Slim.videoencoder"; chmod 755 "$OUT/Slim.videoencoder"
fi
if [ -n "$DECPLUG" ]; then
	plugcheck "$DECPLUG" /System/Library/VideoDecoders/Slim.videodecoder SLIMRegister
	cp "$DECPLUG" "$OUT/Slim.videodecoder"; chmod 755 "$OUT/Slim.videodecoder"
fi
for rel in "${!PIN[@]}"; do
	got=$(sha256sum "$VFS/$rel" | cut -d' ' -f1)
	[ "$got" = "${PIN[$rel]}" ] || { echo "vendor $rel sha256 $got, pin ${PIN[$rel]}" >&2; exit 1; }
	ln "$VFS/$rel" "$OUT/vendor/$rel" 2>/dev/null || cp "$VFS/$rel" "$OUT/vendor/$rel"
done
RESTART=false; NORESTART=${NORESTART:-0}
if [ ${#VARGS[@]} -gt 0 ]; then
	[ -f "$OUT/vcodecd" ] || { echo "--vcodecd-arg needs --vcodecd" >&2; exit 2; }
	VSTR=$(strings "$OUT/vcodecd")
	for a in "${VARGS[@]}"; do case $a in --*) grep -qx -- "$a" <<< "$VSTR" || { echo "vcodecd does not know $a" >&2; exit 1; } ;; esac; done
fi
if [ "$KEEPALIVE" = true ] && [ "$NORESTART" = 0 ] && [ -f "$OUT/vcodecd" ]; then case $(strings "$OUT/vcodecd") in *--restart-on-switch*) RESTART=true ;; esac; fi
N2_JPEG_ARGS="$JPEG_ARGS" N2_PROD=$PROD N2_RESTART=$RESTART N2_VARGS="${VARGS[*]:-}" python3 - "$OUT" "$TAG" "$KEEPALIVE" "$PROBE_ARGS" "${ON[*]}" "${OFF[*]:-}" "${VON[*]:-}" "${VOFF[*]:-}" "$DPROBE_ARGS" <<'PY'
import hashlib, json, os, sys
out, tag, keepalive, probe_args, on, off, von, voff, dprobe_args = sys.argv[1:10]
sha = lambda p: hashlib.sha256(open(os.path.join(out, p), 'rb').read()).hexdigest()
files = {n: sha(n) for n in ('n2vcd_selftest', 'n2vcd_probe', 'n2vcd_dprobe', 'vcodecd', 'n2jpeg_phone') if os.path.exists(os.path.join(out, n))}
jorder = open(os.path.join(out, 'jpeg_order')).read().split() if os.path.exists(os.path.join(out, 'jpeg_order')) else []
order = open(os.path.join(out, 'stream_order')).read().split() if os.path.exists(os.path.join(out, 'stream_order')) else []
vendor = {r: sha('vendor/' + r) for r in ('bin/vpud', 'lib/libvpud_vcodec.so', 'lib/libvcodecdrv.so', 'lib/libvcodec_utility.so', 'lib/libvcodec_oal.so')}
plan = {'purpose': 'gpu/hwcodec: hardware H.264/HEVC encoder host. /usr/local/libexec/{n2vcd_selftest,n2vcd_probe,vcodecd}, the phone\'s '
                   'own five vendor codec objects in /usr/local/lib/n2vendor/{bin,lib}, launchd jobs com.nord2.vcodec-selftest, '
                   'com.nord2.vcodecd, com.nord2.vcodec-probe (each gated by /private/var/db/nord2-vcodec-{selftest,run,probe}). '
                   'Undo: guest-vcodec-undo.sh.',
        'tag': tag, 'files': files, 'vendor': vendor, 'keepalive': keepalive == 'true', 'restart_on_switch': os.environ.get('N2_RESTART') == 'true', 'vcodecd_extra': os.environ.get('N2_VARGS', '').split(), 'probe_args': probe_args.split(),
        'gates_on': on.split(), 'gates_off': off.split(),
        'plugins': {n: sha(n) for n in ('Slim.videoencoder', 'Slim.videodecoder') if os.path.exists(os.path.join(out, n))},
        'video_switches_on': von.split(), 'video_switches_off': voff.split()}
if order:
    plan.update(decoder=True, streams={n: sha('streams/' + n) for n in order}, stream_order=order, dprobe_args=dprobe_args.split())
    plan['purpose'] += (' Plus the hardware DECODE test (opus-decint): /usr/local/libexec/n2vcd_dprobe, streams in '
                        '/usr/local/lib/n2vendor/streams/, vcodecd --decoder, launchd job com.nord2.vcodec-dprobe gated by '
                        '/private/var/db/nord2-vcodec-dprobe.')
if 'n2jpeg_phone' in files:
    plan.update(jpeg=True, jpeg_inputs={n: sha('jpeg/' + n) for n in jorder}, jpeg_order=jorder, jpeg_args=os.environ['N2_JPEG_ARGS'].split())
    plan['purpose'] += (' Plus the hardware JPEG test (opus-jpeg): /usr/local/libexec/n2jpeg_phone, decode inputs in '
                        '/usr/local/lib/n2vendor/jpeg/, launchd job com.nord2.jpeg-selftest gated by /private/var/db/nord2-vcodec-jpeg '
                        '(one-shot); results /private/var/db/nord2-vcodec/jpeg-selftest.log + out/jpeg-*.jpg + out/jdec-*.yuv.')
if os.environ.get('N2_PROD') == 'true':
    plan['prod'] = True
    plan['probe_args'] = []
    plan['purpose'] = ('gpu/hwcodec PRODUCTION (opus-cam): /usr/local/libexec/vcodecd + the phone\'s own five vendor codec objects in '
                       '/usr/local/lib/n2vendor/{bin,lib}; launchd job com.nord2.vcodecd (RunAtLoad, KeepAlive on failure, '
                       '--restart-on-switch) gated by /private/var/db/nord2-vcodec-run; no test jobs. Undo: guest-vcodec-undo.sh.')
if plan['plugins']:
    plan['purpose'] += ' Plus VideoToolbox plug-ins ' + ', '.join(sorted(plan['plugins'])) + ' (hardware paths behind nord2-video-claim-hw / nord2-video-hwdec).'
json.dump(plan, open(os.path.join(out, 'plan.json'), 'w'), indent=1)
print(json.dumps({k: v for k, v in plan.items() if k != 'purpose'}, indent=1))
PY
du -sh "$OUT" | awk '{print "payload: "$2" ("$1", vendor objects hard-linked)"}'
