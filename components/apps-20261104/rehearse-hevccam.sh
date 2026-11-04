#!/bin/bash
# SPDX-License-Identifier: MIT
# hevccam (12 Nov 2026). Host rehearsal of guest-vcodec-hevccam.sh / guest-vcodec-hevccam-undo.sh on a FAKE root, as the
# phone is now (payload-vcodec-prod8 installed by cam7b-prod8): the real launchd.plist, Metal receipt, gpu/video plug-in +
# record and com.apple.camera.plist come from the rootfs mirror (read-only, autocycle/hfsx.py); `unshare -r` for chown.
# Chain: prod8 (guest-vcodec.sh) -> hevccam (both blocks) -> hevccam again (idempotent) -> hevccam undo -> undo again.
# usage: rehearse-hevccam.sh [PROD8 payload] [HEVCCAM payload]     output: $N2_REHEARSE_DIR (default /tmp/n2hw-hevccam/rehearse)
# Last line REHEARSAL_HEVCCAM_OK when every check passed.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
P8=$(cd "${1:-$A/payload-vcodec-prod8}" && pwd); PH=$(cd "${2:-$A/payload-vcodec-prod8-hevccam}" && pwd)
F=${N2_REHEARSE_DIR:-/tmp/n2hw-hevccam/rehearse}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
PR=/private/var/mobile/Library/Preferences
mkdir -p "$R/System/Library/xpc" "$R/private/var/db/nord2-metal" "$R/usr/libexec" "$R/usr/local/libexec" "$R$PR"
$H get /System/Library/xpc "$R/System/Library/xpc" launchd.plist >/dev/null 2>&1
$H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" receipt.json >/dev/null 2>&1
$H get $PR "$R$PR" com.apple.camera.plist >/dev/null 2>&1
[ -s "$R$PR/com.apple.camera.plist" ] || { echo "no com.apple.camera.plist in the mirror"; exit 1; }
echo model > "$R/usr/libexec/keybagd"; echo other > "$R/usr/local/libexec/nord2-metal-probe"
mkdir -p "$R/System/Library/VideoEncoders/AppleVideoEncoder.bundle" "$R/System/Library/VideoDecoders" "$R/private/var/db/nord2-video"
echo model > "$R/System/Library/VideoEncoders/AppleVideoEncoder.bundle/AppleVideoEncoder"
$H get /System/Library/VideoEncoders "$R/System/Library/VideoEncoders" Slim.videoencoder >/dev/null 2>&1
$H get /private/var/db/nord2-video "$R/private/var/db/nord2-video" install.json >/dev/null 2>&1
python3 - "$R" <<'PY'
import hashlib, json, sys
R = sys.argv[1]; p = '/System/Library/VideoEncoders/Slim.videoencoder'; rec = json.load(open(R + '/private/var/db/nord2-video/install.json'))
h = hashlib.sha256(open(R + p, 'rb').read()).hexdigest()
if rec['files'].get(p) != h:   # the mirror may lag the phone: make the fake record consistent with the fake file
    rec['files'][p] = h; json.dump(rec, open(R + '/private/var/db/nord2-video/install.json', 'w'), indent=1)
PY
V0=$(cat "$R/System/Library/VideoEncoders/Slim.videoencoder" "$R/private/var/db/nord2-video/install.json" | sha256sum | cut -c1-12)
C0=$(python3 -c "import plistlib,sys,json;print(json.dumps(plistlib.load(open(sys.argv[1],'rb')),sort_keys=True,default=repr))" "$R$PR/com.apple.camera.plist" | sha256sum | cut -c1-12)
# block NAME of guest script G, with the fake root and payload P
run() { local G=$1 NAME=$2 P=$3
        sed -n "/^python3 - <<'$NAME'\$/,/^$NAME\$/p" "$A/$G" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|S='/src'|S='$P'|" > "$F/$G.$NAME.py"
        [ -s "$F/$G.$NAME.py" ] || { echo "no block $NAME in $G"; exit 1; }
        unshare -r python3 "$F/$G.$NAME.py"; }
# the GUEST blocks must be exactly guest-vcodec.sh's / guest-vcodec-undo.sh's (their rehearsals then apply as well)
for p in "guest-vcodec.sh guest-vcodec-hevccam.sh" "guest-vcodec-undo.sh guest-vcodec-hevccam-undo.sh"; do set -- $p
	cmp -s <(sed -n "/^python3 - <<'GUEST'\$/,/^GUEST\$/p" "$A/$1") <(sed -n "/^python3 - <<'GUEST'\$/,/^GUEST\$/p" "$A/$2") \
		&& echo "  $2: GUEST block identical to $1: ok" || { echo "REHEARSAL FAILED: $2 GUEST block differs"; exit 1; }
done
check() { python3 - "$R" "$1" <<'PY'
import json, os, plistlib, sys
R, want = sys.argv[1:3]
LD = plistlib.loads(open(R + '/System/Library/xpc/launchd.plist', 'rb').read())['LaunchDaemons']
jobs = {j['Label']: j for j in LD.values() if j.get('Label', '').startswith('com.nord2.vcodec')}
db = '/private/var/db/'
g = sorted(n for n in os.listdir(R + db) if n.startswith(('nord2-vcodec-', 'nord2-video-', 'nord2-camera-keep')))
cp = plistlib.load(open(R + '/private/var/mobile/Library/Preferences/com.apple.camera.plist', 'rb'))
keep = os.path.exists(R + '/private/var/mobile/Library/Preferences/com.apple.camera.plist.pre-hevccam')
args = jobs['com.nord2.vcodecd']['ProgramArguments'] if 'com.nord2.vcodecd' in jobs else None
print('  job', args, 'switches', g, 'EncodingBehavior', cp.get('CAMUserPreferenceCaptureEncodingBehavior'), 'backup', keep)
if want == 'prod8':
    assert args and '--restart-on-switch' in args and '--decoder' in args, args
    assert g == ['nord2-vcodec-run', 'nord2-video-claim-hw', 'nord2-video-hwdec'], g
elif want == 'hevccam':
    assert list(jobs) == ['com.nord2.vcodecd'] and '--restart-on-switch' not in args and '--decoder' in args, args
    j = jobs['com.nord2.vcodecd']; assert j['RunAtLoad'] and j['KeepAlive'] == {'SuccessfulExit': False}
    assert g == ['nord2-camera-keephevc', 'nord2-vcodec-run', 'nord2-video-claim-hw', 'nord2-video-hwdec'], g
    assert cp['CAMUserPreferenceCaptureEncodingBehavior'] == 0 and keep
elif want == 'undo':
    assert not jobs and g == [] and not keep, (jobs.keys(), g, keep)
print('  STATE', want, 'ok')
PY
}
echo "== 1. phone state: payload-vcodec-prod8 ($P8)"; run guest-vcodec.sh GUEST "$P8" | grep -E "^VCODEC_(JOB|GATE|VIDEO|PLUGINS)|^CC_JOB"; check prod8
echo "== 2. hevccam ($PH)"; run guest-vcodec-hevccam.sh GUEST "$PH" | grep -E "^VCODEC_(JOB|GATE|VIDEO|PLUGINS)|^CC_"
run guest-vcodec-hevccam.sh CAMPREFS "$PH"; check hevccam
python3 - "$R" "$PH" <<'PY'
import hashlib, json, sys
R, P = sys.argv[1:3]; plan = json.load(open(P + '/plan.json'))
for n, d in (('Slim.videoencoder', '/System/Library/VideoEncoders/'), ('Slim.videodecoder', '/System/Library/VideoDecoders/')):
    assert hashlib.sha256(open(R + d + n, 'rb').read()).hexdigest() == plan['plugins'][n], n
assert hashlib.sha256(open(R + '/usr/local/libexec/vcodecd', 'rb').read()).hexdigest() == plan['files']['vcodecd']
print('  files = plan: encoder', plan['plugins']['Slim.videoencoder'][:8], 'decoder', plan['plugins']['Slim.videodecoder'][:8], 'vcodecd', plan['files']['vcodecd'][:8])
PY
echo "== 3. hevccam again (idempotent)"; run guest-vcodec-hevccam.sh GUEST "$PH" | grep -E "^CC_" ; run guest-vcodec-hevccam.sh CAMPREFS "$PH"; check hevccam
echo "== 4. undo"; run guest-vcodec-hevccam-undo.sh GUEST "$PH" | grep -E "^VCODEC_RESTORED|^CC_"; run guest-vcodec-hevccam-undo.sh CAMPREFS "$PH"; check undo
echo "== 5. undo again"; run guest-vcodec-hevccam-undo.sh GUEST "$PH" | grep -E "^VCODEC_UNDO|^CC_" ; run guest-vcodec-hevccam-undo.sh CAMPREFS "$PH"; check undo
V1=$(cat "$R/System/Library/VideoEncoders/Slim.videoencoder" "$R/private/var/db/nord2-video/install.json" | sha256sum | cut -c1-12)
C1=$(python3 -c "import plistlib,sys,json;print(json.dumps(plistlib.load(open(sys.argv[1],'rb')),sort_keys=True,default=repr))" "$R$PR/com.apple.camera.plist" | sha256sum | cut -c1-12)
[ "$V0" = "$V1" ] && echo "  gpu/video plug-in + record restored byte-identical: ok" || { echo "REHEARSAL FAILED plug-in"; exit 1; }
[ "$C0" = "$C1" ] && echo "  com.apple.camera.plist content restored (all keys/values as before): ok" || { echo "REHEARSAL FAILED camera prefs"; exit 1; }
echo "== 6. prod8 -> hevccam -> straight back to prod8 (guest-vcodec-hevccam-back.sh + payload-vcodec-prod8)"
run guest-vcodec.sh GUEST "$P8" > /dev/null; check prod8
run guest-vcodec-hevccam.sh GUEST "$PH" > /dev/null; run guest-vcodec-hevccam.sh CAMPREFS "$PH"; check hevccam
cmp -s <(sed -n "/^python3 - <<'GUEST'\$/,/^GUEST\$/p" "$A/guest-vcodec.sh") <(sed -n "/^python3 - <<'GUEST'\$/,/^GUEST\$/p" "$A/guest-vcodec-hevccam-back.sh") \
	|| { echo "REHEARSAL FAILED: back guest GUEST block differs"; exit 1; }
run guest-vcodec-hevccam-back.sh GUEST "$P8" | grep -E "^VCODEC_(JOB|VIDEO)"; run guest-vcodec-hevccam-back.sh CAMPREFS "$P8"; check prod8
C2=$(python3 -c "import plistlib,sys,json;print(json.dumps(plistlib.load(open(sys.argv[1],'rb')),sort_keys=True,default=repr))" "$R$PR/com.apple.camera.plist" | sha256sum | cut -c1-12)
[ "$C0" = "$C2" ] && [ ! -e "$R$PR/com.apple.camera.plist.pre-hevccam" ] && echo "  back to prod8: camera prefs as before, backup gone: ok" || { echo "REHEARSAL FAILED back"; exit 1; }
echo REHEARSAL_HEVCCAM_OK
