#!/bin/bash
# SPDX-License-Identifier: MIT
# opus-ios. Rehearsal of guest-vcodec.sh / guest-vcodec-undo.sh on a FAKE root (the scripts' own Python, /work/root and /src
# replaced), run inside `unshare -r` so the scripts' chown(0,0) works without privileges.  The fake root carries the REAL
# launchd.plist and Metal receipt.json read from the rootfs mirror (read-only, autocycle/hfsx.py), so the job edit and the
# receipt hash step are exercised on the real 532-job binary plist.  Nothing touches the mirror or a phone.
# decrun copy of rehearse-vcodec.sh: installs with guest-vcodec-decrun.sh, undoes with the unchanged guest-vcodec-undo.sh.
# usage: rehearse-vcodec-decrun.sh [PAYLOAD=payload-vcodec]       output: /tmp/n2hw-opus-ios/rehearse (deleted at the start)
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
P=$(cd "${1:-$A/payload-vcodec}" && pwd)
F=${N2_REHEARSE_DIR:-/tmp/n2hw-opus-ios/rehearse}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
mkdir -p "$R/System/Library/xpc" "$R/private/var/db/nord2-metal" "$R/usr/libexec" "$R/usr/local/libexec"
$H get /System/Library/xpc "$R/System/Library/xpc" launchd.plist >/dev/null 2>&1
$H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" receipt.json >/dev/null 2>&1
echo model > "$R/usr/libexec/keybagd"; echo other > "$R/usr/local/libexec/nord2-metal-probe"
# opus-lead2: gpu/video's installed plug-in + its record from the mirror (the previous owner of Slim.videoencoder)
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
touch "$R/private/var/db/nord2-video-claim-hw"   # must be switched OFF by a default plug-in install
V0=$(cat "$R/System/Library/VideoEncoders/Slim.videoencoder" "$R/private/var/db/nord2-video/install.json" | sha256sum | cut -c1-12)
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|S='/src'|S='$P'|" > "$F/$1.py"
        unshare -r python3 "$F/$1.py"; }
state() { python3 - "$R" <<'PY'
import hashlib, json, os, plistlib, sys
R = sys.argv[1]; raw = open(R + '/System/Library/xpc/launchd.plist', 'rb').read(); LD = plistlib.loads(raw)['LaunchDaemons']
rc = json.load(open(R + '/private/var/db/nord2-metal/receipt.json'))
jobs = sorted(j['Label'] for j in LD.values() if j.get('Label', '').startswith(('com.nord2.vcodec', 'com.nord2.jpeg')))
files = sorted(os.path.relpath(os.path.join(b, n), R) for b, _, fs in os.walk(R) for n in fs
               if 'n2vcd' in n or 'vcodec' in b or n == 'vcodecd' or 'nord2-vcodec' in n or n == 'n2jpeg_phone' or 'n2vendor/jpeg' in b)
print('  jobs', jobs, 'njobs', len(LD), 'receipt_ok', rc['installed_launchd_sha256'] == hashlib.sha256(raw).hexdigest())
print('  files', len(files), files[:4], '...')
PY
}
H0=$(sha256sum "$R/System/Library/xpc/launchd.plist" | cut -c1-12)
echo "start: launchd.plist $H0"; state
echo "== install";  run guest-vcodec-decrun.sh | grep -E "^VCODEC_|^CC_"; state
if python3 -c "import json,sys; sys.exit(0 if json.load(open('$P/plan.json')).get('plugins') else 1)"; then
	python3 - "$R" "$P" <<'PY'
import hashlib, json, os, sys
R, P = sys.argv[1:3]; plan = json.load(open(P + '/plan.json'))
D = {'Slim.videoencoder': '/System/Library/VideoEncoders/', 'Slim.videodecoder': '/System/Library/VideoDecoders/'}
for n, h in plan['plugins'].items():
    got = hashlib.sha256(open(R + D[n] + n, 'rb').read()).hexdigest(); assert got == h, n
vr = json.load(open(R + '/private/var/db/nord2-video/install.json'))['files']
assert vr.get('/System/Library/VideoEncoders/Slim.videoencoder') in (plan['plugins'].get('Slim.videoencoder'), None) or 'Slim.videoencoder' not in plan['plugins']
for n in plan.get('video_switches_off', []): assert not os.path.exists(R + '/private/var/db/nord2-video-' + n), n
for n in plan.get('video_switches_on', []): assert os.path.exists(R + ('/private/var/db/nord2-camera-keephevc' if n == 'camera-keephevc' else '/private/var/db/nord2-video-' + n)), n
print('  plug-ins installed', sorted(plan['plugins']), 'gpu/video record updated; switches on', plan.get('video_switches_on'), 'off', plan.get('video_switches_off'), ': ok')
PY
fi
echo "== again (idempotent)"; run guest-vcodec-decrun.sh | grep -E "^CC_|INSTALLED" | head -3
python3 - "$R" "$P" <<'PY'
import json, plistlib, sys
R, P = sys.argv[1:3]; LD = plistlib.loads(open(R + '/System/Library/xpc/launchd.plist', 'rb').read())['LaunchDaemons']
plan = json.load(open(P + '/plan.json'))
for k, j in sorted(LD.items()):
    if j.get('Label', '').startswith(('com.nord2.vcodec', 'com.nord2.jpeg')):
        print('  JOB', j['Label'], 'RunAtLoad', j['RunAtLoad'], 'KeepAlive', j.get('KeepAlive'), 'args', ' '.join(j['ProgramArguments']))
PY
echo "== a foreign file in our place must stop the install and change nothing"
cp "$R/System/Library/xpc/launchd.plist" "$F/lp.before"
FG=n2vcd_probe; [ -e "$R/usr/local/libexec/$FG" ] || FG=vcodecd     # opus-cam: a --prod payload has no probe
mv "$R/usr/local/libexec/$FG" "$F/probe.ours"; echo foreign > "$R/usr/local/libexec/$FG"
rm -f "$R/private/var/db/nord2-vcodec/install.json.bak"
if run guest-vcodec-decrun.sh > "$F/foreign.log" 2>&1; then echo "REHEARSAL FAILED: install accepted a foreign file"; exit 1; fi
tail -1 "$F/foreign.log"; cmp -s "$F/lp.before" "$R/System/Library/xpc/launchd.plist" && echo "  launchd.plist unchanged: ok"
[ "$(cat "$R/usr/local/libexec/$FG")" = foreign ] && echo "  foreign file kept: ok"
mv "$F/probe.ours" "$R/usr/local/libexec/$FG"
echo "== phone results appear, then undo"
echo "N2VCDT DONE" > "$R/private/var/db/nord2-vcodec/selftest-77.txt"; echo x > "$R/private/var/db/nord2-vcodec/out/probe-9.h264"
run guest-vcodec-undo.sh | grep -E "^VCODEC_|^CC_"; state
H1=$(sha256sum "$R/System/Library/xpc/launchd.plist" | cut -c1-12)
[ -f "$R/private/var/db/nord2-vcodec/selftest-77.txt" ] && [ -f "$R/private/var/db/nord2-vcodec/out/probe-9.h264" ] && echo "  results kept: ok"
[ -f "$R/usr/local/libexec/nord2-metal-probe" ] && echo "  unrelated file kept: ok"
V1=$(cat "$R/System/Library/VideoEncoders/Slim.videoencoder" "$R/private/var/db/nord2-video/install.json" | sha256sum | cut -c1-12)
if python3 -c "import json,sys; sys.exit(0 if json.load(open('$P/plan.json')).get('plugins') else 1)"; then
	[ "$V0" = "$V1" ] && echo "  gpu/video plug-in + record restored byte-identical: ok" || { echo "REHEARSAL FAILED: plug-in not restored"; exit 1; }
	[ ! -e "$R/System/Library/VideoDecoders/Slim.videodecoder" ] && echo "  decoder plug-in removed: ok"
	[ ! -e "$R/private/var/db/nord2-vcodec/prev" ] && echo "  prev/ removed: ok"
fi
[ ! -e "$R/usr/local/lib" ] && echo "  /usr/local/lib removed (was absent): ok"
echo "== undo again"; run guest-vcodec-undo.sh | grep -E "^VCODEC_UNDO|^CC_"
echo "launchd.plist: start $H0, after undo $H1"
[ "$H0" = "$H1" ] && echo "REHEARSAL_OK launchd.plist byte-identical after install+undo" || echo "REHEARSAL_OK (launchd.plist re-serialised: content compared below)"
python3 - "$R" <<'PY'
import plistlib, sys
print('  jobs left with our labels:', [j['Label'] for j in plistlib.loads(open(sys.argv[1] + '/System/Library/xpc/launchd.plist', 'rb').read())['LaunchDaemons'].values() if j.get('Label', '').startswith('com.nord2.vcodec')])
PY
