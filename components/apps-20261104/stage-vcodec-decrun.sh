#!/bin/bash
# SPDX-License-Identifier: MIT
# decrun (12 Nov 2026): payload-vcodec-prod6-decrun = payload-vcodec-prod6 (staged by the unchanged stage-vcodec.sh with the
# prod6 command of HARDWARE_CODECS.md 8.3) + n2vt_decrun (installed as /usr/local/libexec/n2vcd_vtdecrun) + its clips
# (/usr/local/lib/n2vendor/streams/) + plan['decrun'], run once at boot by guest-vcodec-decrun.sh's job (label
# com.nord2.vcodec-dprobe, gate nord2-vcodec-dprobe ON).  Guest: guest-vcodec-decrun.sh.  Undo: guest-vcodec-undo.sh.
# usage: stage-vcodec-decrun.sh [TAG=dr1]   env: DP4 (prod6 inputs), DECRUN (built n2vt_decrun), CLIPS (dir), DELAY (s, 60),
#   OUT (payload dir), EXTRA (extra tool args, e.g. --console-frames)
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); GPU=$(cd "$A/../.." && pwd); V=$GPU/hwcodec/vtdec_run
TAG=${1:-dr1}; OUT=${OUT:-$A/payload-vcodec-prod6-decrun}   # dr2: OUT=$A/payload-vcodec-prod6-decrun2
DP4=${DP4:-/tmp/claude-1000/-home-user/b6fc0670-1b3b-4379-9bef-96b78e4e9ee7/scratchpad/dp4}
DECRUN=${DECRUN:-$V/build/ios/n2vt_decrun}; CLIPS=${CLIPS:-$V/clips}; DELAY=${DELAY:-60}
ORDER=(${CLIP_ORDER:-t1080p60-h264-high-60f.h264 t1080p60-hevc-main-60f.h265 t2160-hevc-main-bf2-20f.h265 cam0056-1080p30.h264})   # dr5: CLIP_ORDER env
bash "$A/stage-vcodec.sh" "$TAG" --prod --tools "$DP4/ios" --vcodecd "$DP4/vcd/vcodecd" \
	--encoder-plugin "$DP4/plugins/Slim.videoencoder" --decoder-plugin "$DP4/plugins/Slim.videodecoder" \
	--video-on claim-hw --video-on hwdec --video-off hwenc-sync --video-off trace --video-off probe --video-off camera-keephevc \
	--payload "$OUT" > "$A/stage-decrun-$TAG.log"
file "$DECRUN" | grep -q 'Mach-O 64-bit arm64 executable' || { echo "$DECRUN is not an arm64 Mach-O" >&2; exit 1; }
cp "$DECRUN" "$OUT/n2vcd_vtdecrun"; chmod 755 "$OUT/n2vcd_vtdecrun"
mkdir -p "$OUT/streams"
for n in "${ORDER[@]}"; do [ -s "$CLIPS/$n" ] || { echo "no clip $CLIPS/$n" >&2; exit 1; }; ln "$CLIPS/$n" "$OUT/streams/$n" 2>/dev/null || cp "$CLIPS/$n" "$OUT/streams/$n"; done
N2_EXTRA="${EXTRA:-}" python3 - "$OUT" "$DELAY" "${ORDER[@]}" <<'PY'
import hashlib, json, os, sys
out, delay, order = sys.argv[1], sys.argv[2], sys.argv[3:]
sha = lambda p: hashlib.sha256(open(os.path.join(out, p), 'rb').read()).hexdigest()
plan = json.load(open(os.path.join(out, 'plan.json')))
plan['files']['n2vcd_vtdecrun'] = sha('n2vcd_vtdecrun')
plan['decrun'] = {'clips': {n: sha('streams/' + n) for n in order}, 'clip_order': order, 'args': ['--delay', delay] + os.environ.get('N2_EXTRA', '').split()}
plan['gates_on'] = ['run', 'dprobe']; plan['gates_off'] = ['selftest', 'probe', 'jpeg']
plan['purpose'] += (' | decrun (12 Nov): + /usr/local/libexec/n2vcd_vtdecrun (gpu/hwcodec/vtdec_run/n2vt_decrun.c): one boot run, job label '
                    'com.nord2.vcodec-dprobe gated by /private/var/db/nord2-vcodec-dprobe, decodes ' + ', '.join(order) +
                    ' from /usr/local/lib/n2vendor/streams/ through VTDecompressionSession (no decoder spec, 420v), console lines '
                    'N2VTDEC, log /private/var/db/nord2-vcodec/decrun.log. Guest guest-vcodec-decrun.sh; verdict gpu/hwcodec/cam/decrun_check.py.')
json.dump(plan, open(os.path.join(out, 'plan.json'), 'w'), indent=1)
print(json.dumps({k: v for k, v in plan.items() if k not in ('purpose', 'vendor')}, indent=1))
PY
du -shL "$OUT" | awk '{print "payload: "$2" ("$1")"}'
