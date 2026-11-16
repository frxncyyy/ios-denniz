#!/bin/bash
# gfx100a: fake-root rehearsal of guest-gfx100a.sh / -undo.sh (Nord2Metal e6f75db1 over webxr1 36f755ea + library 9f263e6f), made
# from apps-20261104/rehearse-shader99b.sh. Fake root = the CURRENT mirror's bundle + receipt (shader99b), brought to webxr1 by
# webxr-20261114/guest-webxr1.sh first (the phone after the orchestrator's webxr1 transaction). unshare -r; nothing touches the
# mirror or a phone. Every write is checked against owned-gfx100a.json; every refusal leaves the root byte-identical.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/../.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle; X=$B/webxr-20261114
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593; WXN=36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d
WXL=9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a; S99N=fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d
S99L=7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56
TOP=$B/.rehearse-gfx100a; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$(basename $1).py"; unshare -r python3 "$F/$(basename $1).py"; }
st() { (cd "$R" && find . \( -type f -o -type d \) -printf '%p %m\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-gfx100a.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3])
bad=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k) and k not in own)
assert not bad, 'written outside owned-gfx100a.json: %r'%bad
print('  owned check: %d path(s) differ, all owned: %s'%(len([k for k in set(a)|set(b) if a.get(k)!=b.get(k)]), sorted(k.rsplit('/',1)[1] for k in set(a)|set(b) if a.get(k)!=b.get(k))))
PY
}
setfile() { cp "$2" "$R/$BU/$1"; python3 - "$R" "$1" <<'PY'
import hashlib,json,sys
R,n=sys.argv[1:]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
}
fresh() {   # fresh <name>: fake root = the mirror's bundle + receipt (shader99b), then webxr1 installed over it
  F=$TOP/$1; mkdir -p "$F/root"; R=$F/root; DBD=$R/private/var/db/nord2-metal
  mkdir -p "$R/$BU" "$DBD"
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-shader99b libredwoodair.dylib.pre-shader99b >/dev/null 2>&1 || true
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"
  [ "$(h Nord2Metal)" = $S99N ] && [ "$(h libredwoodair.dylib)" = $S99L ] || die "mirror is not shader99b (fe4ecc9f + 7c0c5aba)"
  [ "${2:-webxr}" = webxr ] || return 0
  run $X/guest-webxr1.sh $X/payload-webxr1 | grep -qE '^WEBXR1_INSTALLED' || die "webxr1 did not install on the fake root"
  [ "$(h Nord2Metal)" = $WXN ] && [ "$(h libredwoodair.dylib)" = $WXL ] || die "not webxr1 after its guest"
}
refused() {  # refused <guest> <payload> <grep> <what>: must fail with that reason and change nothing
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
G=$A/guest-gfx100a.sh; GU=$A/guest-gfx100a-undo.sh
P=$A/payload-gfx100a; PO=$A/payload-gfx100a-off; PN=$A/payload-gfx100a-nortcopy; PU=$A/payload-gfx100a-undo
for d in $P $PO $PN; do [ "$(sha256sum < $d/Nord2Metal | cut -c1-64)" = $NEWN ] || die "payload file in $d"; done
cmp -s $P/Nord2Metal "$A/../build2/payload/Nord2Metal.bundle/Nord2Metal" || die "payload is not build2"
[ "$(sha256sum < $PU/Nord2Metal | cut -c1-64)" = $WXN ] || die "undo payload file"

echo "== 1 phone state (webxr1): install, again, undo, again"
fresh phone; s0=$(st); snap "$F/0.snap"
run $G $P | grep -E '^GFX100A_(INSTALLED|NOTE|FLAG|LOGDIR)|^CC_'
[ "$(h Nord2Metal)" = $NEWN ] && [ "$(h libredwoodair.dylib)" = $WXL ] || die "not installed"
python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['Nord2Metal']=='$NEWN' and i['libredwoodair.dylib']=='$WXL' and rc['gfx100a']['previous']=='$WXN' and rc['gfx100a']['logdir_made'] and 'webxr1' in rc and 'shader99b' in rc" || die "receipt"
[ "$(sha256sum < "$DBD/Nord2Metal.pre-gfx100a" | cut -c1-64)" = $WXN ] || die "backup"
[ "$(stat -c %a "$R/$BU/Nord2Metal")" = 755 ] || die "mode"
[ "$(stat -c %a "$DBD/log")" = 1777 ] || die "log directory mode $(stat -c %a "$DBD/log")"
snap "$F/1.snap"; owned "$F/0.snap" "$F/1.snap"; s1=$(st)
run $G $P | grep -qE '^GFX100A_ALREADY' || die "not idempotent"; [ "$(st)" = "$s1" ] || die "second install changed the root"
# logs written by the processes meanwhile: the undo takes them with the directory it made
printf 'x\n' > "$DBD/log/backboardd.61.log"; printf 'y\n' > "$DBD/log/SpringBoard.26.log"
run $GU $PU | grep -E '^GFX100A_UNDONE'
[ "$(st)" = "$s0" ] || die "undo is not byte-identical to the start"
run $GU $PU | grep -q "nothing to do" || die "second undo"; [ "$(st)" = "$s0" ] || die "second undo changed the root"

echo "== 2 a log directory that was already there is kept by the undo"
mkdir -m 755 "$DBD/log"; printf 'old\n' > "$DBD/log/keep.log"; sk=$(st)
run $G $P | grep -E '^GFX100A_(INSTALLED|LOGDIR)'
run $GU $PU | grep -E '^GFX100A_UNDONE'
[ "$(st)" = "$sk" ] && [ "$(cat "$DBD/log/keep.log")" = old ] || die "existing log directory touched"
rm -r "$DBD/log"; [ "$(st)" = "$s0" ] || die "cleanup"

echo "== 3 -off and -nortcopy set their switch; the plain payload clears it; undo removes ours"
run $G $PO | grep -E '^GFX100A_(INSTALLED|FLAG)'
[ "$(cat "$DBD/no-gfx100a")" = made-by-gfx100a ] || die "switch not set"
snap "$F/3.snap"; owned "$F/0.snap" "$F/3.snap"
run $G $PN | grep -E '^GFX100A_(ALREADY|FLAG)'
[ ! -e "$DBD/no-gfx100a" ] && [ "$(cat "$DBD/no-rtcopy")" = made-by-gfx100a ] || die "switches after -nortcopy"
run $G $P | grep -E '^GFX100A_(ALREADY|FLAG)'
[ ! -e "$DBD/no-rtcopy" ] || die "switch not cleared"
run $G $PO > /dev/null; run $GU $PU | grep -E '^GFX100A_(UNDONE|FLAG)'
[ "$(st)" = "$s0" ] || die "undo after -off is not byte-identical to the start"

echo "== 4 a switch that is not ours is kept by install and by undo"
printf 'by hand\n' > "$DBD/no-filelog"; sf=$(st)
run $G $P | grep -E '^GFX100A_NOTE' || die "foreign switch not reported"
run $GU $PU > /dev/null
[ "$(cat "$DBD/no-filelog")" = "by hand" ] && [ "$(st)" = "$sf" ] || die "foreign switch touched"
rm "$DBD/no-filelog"

echo "== 5 undo without its backup uses the payload's copy; with a damaged backup it refuses"
run $G $P > /dev/null; rm "$DBD/Nord2Metal.pre-gfx100a"
run $GU $PU | grep -E '^GFX100A_UNDONE' | grep -q fallback || die "fallback not used"
[ "$(st)" = "$s0" ] || die "fallback undo is not byte-identical to the start"
run $G $P > /dev/null; printf 'x' >> "$DBD/Nord2Metal.pre-gfx100a"
refused $GU $PU "no usable copy" "undo with a damaged backup"
cp $PU/Nord2Metal "$DBD/Nord2Metal.pre-gfx100a"
run $GU $PU > /dev/null; [ "$(st)" = "$s0" ] || die "undo after repair"

echo "== 6 refusals of the install (root unchanged)"
fresh s99 noweb; refused $G $P "is not webxr1's 9f263e6f" "the phone still on shader99b (webxr1 not installed)"
fresh other; mkdir "$F/badpay"; cp $P/Nord2Metal $P/plan.json "$F/badpay/"; printf 'x' >> "$F/badpay/Nord2Metal"
refused $G "$F/badpay" "is not the gfx100a one" "a payload Nord2Metal that is not the build"
setfile Nord2Metal "$B/apps-20261104/payload-gpufinal7/Nord2Metal"
refused $G $P "is not the webxr1 state" "current Nord2Metal = gpufinal7"
fresh drift; printf 'x' >> "$R/$BU/libredwoodair.dylib"
refused $G $P "differs from its receipt" "a bundle that differs from its receipt"
fresh stale; python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['gfx100a']={'installed':'x'};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused $G $P "stale receipt record" "a stale record gfx100a"
fresh bak; printf 'other\n' > "$DBD/Nord2Metal.pre-gfx100a"
refused $G $P "a different backup" "a different Nord2Metal.pre-gfx100a already there"
fresh link; ln -s /etc/passwd "$DBD/no-rtcopy"
refused $G $P "not a regular file" "a switch that is a symlink"
fresh loglink; ln -s /tmp "$DBD/log"
refused $G $P "log path is not a directory" "a log path that is a symlink"
fresh undo-foreign; setfile Nord2Metal "$B/apps-20261104/payload-gpufinal7/Nord2Metal"
refused $GU $PU "neither gfx100a nor webxr1" "undo over gpufinal7"

echo "== 7 after gfx100a undo, webxr1's own undo still works (its record and backups were left)"
fresh chain; run $G $P > /dev/null; run $GU $PU > /dev/null
run $X/guest-webxr1-undo.sh $X/payload-webxr1-undo | grep -E '^WEBXR1_UNDONE' | cut -c1-160
[ "$(h Nord2Metal)" = $S99N ] && [ "$(h libredwoodair.dylib)" = $S99L ] || die "webxr1 undo did not give shader99b back"
echo REHEARSAL_OK
