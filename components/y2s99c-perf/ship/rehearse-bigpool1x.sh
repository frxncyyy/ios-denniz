#!/bin/bash
# y2s99c: rehearsal of guest-bigpool1x.sh / -undo.sh on fake roots made from the mirror's Nord2Metal bundle + receipt (the mirror = THE
# PHONE: shader99b, Nord2Metal fe4ecc9f + library 7c0c5aba). unshare -r; nothing touches the mirror or a phone (adapted from
# y2s99a-api-gaps/ship/rehearse-api99a.sh). Every write is checked against owned-bigpool1x.json; every refusal leaves the root identical.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/../.." && pwd); AP=$B/apps-20261104; BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWL=f271fcbe3dde817ea5fa719c32614f50a78df229d14672516c9ace7abf296a56; OLDL=9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a
N99=36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d; WX=$B/webxr-20261114/payload-webxr1
TOP=$B/.rehearse-bigpool1x; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . \( -type f -o -type d \) -printf '%p %m\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-bigpool1x.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3])
diff=[k for k in set(a)|set(b) if a.get(k)!=b.get(k)]
bad=sorted(k for k in diff if k not in own)
assert not bad, 'written outside owned-bigpool1x.json: %r'%bad
print('  owned check: %d path(s) differ, all owned: %s'%(len(diff), sorted(k.rsplit('/',1)[1] for k in diff)))
PY
}
setfile() { cp "$2" "$R/$BU/$1"; python3 - "$R" "$1" <<'PY'
import hashlib,json,sys
R,n=sys.argv[1:]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
}
fresh() {
  F=$TOP/$1; mkdir -p "$F/root"; R=$F/root; DBD=$R/private/var/db/nord2-metal
  mkdir -p "$R/$BU" "$DBD"
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-shader99b libredwoodair.dylib.pre-shader99b >/dev/null 2>&1
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"; chmod 644 "$R/$BU/libredwoodair.dylib"
  setfile Nord2Metal $WX/Nord2Metal; chmod 755 "$R/$BU/Nord2Metal"; setfile libredwoodair.dylib $WX/libredwoodair.dylib; chmod 644 "$R/$BU/libredwoodair.dylib"   # the webxr1 state (simulated)
  [ "$(h Nord2Metal)" = $N99 ] && [ "$(h libredwoodair.dylib)" = $OLDL ] || die "not the webxr1 state"
}
refused() {
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
P=$A/payload-bigpool1x; PO=$A/payload-bigpool1x-off; PU=$A/payload-bigpool1x-undo
[ "$(sha256sum < $P/libredwoodair.dylib | cut -c1-64)" = $NEWL ] && cmp -s $P/libredwoodair.dylib $PO/libredwoodair.dylib && [ "$(sha256sum < $PU/libredwoodair.dylib | cut -c1-64)" = $OLDL ] || die "payload files"
cmp -s $P/libredwoodair.dylib "$A/../buildx/payload/Nord2Metal.bundle/libredwoodair.dylib" || die "payload is not the y2s99c build"

echo "== 1 webxr1 state: install, again, undo, again"
fresh phone; s0=$(st); snap "$F/0.snap"
run guest-bigpool1x.sh $P | grep -E '^BIGPOOL1_(INSTALLED|NOTE|FLAG)|^CC_'
[ "$(h libredwoodair.dylib)" = $NEWL ] && [ "$(h Nord2Metal)" = $N99 ] || die "not installed / Nord2Metal touched"
python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['libredwoodair.dylib']=='$NEWL' and i['Nord2Metal']=='$N99' and rc['bigpool1']['previous']=={'libredwoodair.dylib':'$OLDL'} and 'shader99b' in rc" || die "receipt"
[ "$(sha256sum < "$DBD/libredwoodair.dylib.pre-bigpool1" | cut -c1-64)" = $OLDL ] || die "backup"
[ "$(stat -c %a "$R/$BU/libredwoodair.dylib")" = 644 ] || die "mode"
snap "$F/1.snap"; owned "$F/0.snap" "$F/1.snap"; s1=$(st)
run guest-bigpool1x.sh $P | grep -qE '^BIGPOOL1_ALREADY' || die "not idempotent"; [ "$(st)" = "$s1" ] || die "second install changed the root"
run guest-bigpool1x-undo.sh $PU | grep -E '^BIGPOOL1_UNDONE'
[ "$(st)" = "$s0" ] || die "undo is not byte-identical to the start"
run guest-bigpool1x-undo.sh $PU | grep -q "nothing to do" || die "second undo"; [ "$(st)" = "$s0" ] || die "second undo changed the root"

echo "== 2 the -off payload sets the switch; the plain payload clears it; undo removes ours"
run guest-bigpool1x.sh $PO | grep -E '^BIGPOOL1_(INSTALLED|FLAG)'
[ "$(cat "$DBD/no-bigpool")" = made-by-bigpool1 ] || die "switch not set"
snap "$F/2.snap"; owned "$F/0.snap" "$F/2.snap"
run guest-bigpool1x.sh $P | grep -E '^BIGPOOL1_(ALREADY|FLAG)'
[ ! -e "$DBD/no-bigpool" ] || die "switch not cleared"
run guest-bigpool1x.sh $PO > /dev/null; run guest-bigpool1x-undo.sh $PU | grep -E '^BIGPOOL1_(UNDONE|FLAG)'
[ "$(st)" = "$s0" ] || die "undo after -off is not byte-identical to the start"

echo "== 3 a switch that is not ours is kept by install and by undo"
printf 'by hand\n' > "$DBD/no-bigpool"; sf=$(st)
run guest-bigpool1x.sh $P | grep -E '^BIGPOOL1_NOTE' || die "foreign switch not reported"
run guest-bigpool1x-undo.sh $PU | grep -E '^BIGPOOL1_NOTE' || die "foreign switch not reported by undo"
[ "$(cat "$DBD/no-bigpool")" = "by hand" ] && [ "$(st)" = "$sf" ] || die "foreign switch touched"
rm "$DBD/no-bigpool"

echo "== 4 undo without its backup uses the payload's shader99b copy; with a damaged backup it refuses"
run guest-bigpool1x.sh $P > /dev/null; rm "$DBD/libredwoodair.dylib.pre-bigpool1"
run guest-bigpool1x-undo.sh $PU | grep -E '^BIGPOOL1_UNDONE' | grep -q fallback || die "fallback not used"
[ "$(st)" = "$s0" ] || die "fallback undo is not byte-identical to the start"
run guest-bigpool1x.sh $P > /dev/null; printf 'x' >> "$DBD/libredwoodair.dylib.pre-bigpool1"
refused guest-bigpool1x-undo.sh $PU "no usable copy" "undo with a damaged backup"
cp $PU/libredwoodair.dylib "$DBD/libredwoodair.dylib.pre-bigpool1"; chmod 644 "$DBD/libredwoodair.dylib.pre-bigpool1"
run guest-bigpool1x-undo.sh $PU > /dev/null; [ "$(st)" = "$s0" ] || die "undo after repair"

echo "== 5 refusals of the install (root unchanged)"
fresh other; mkdir "$F/badpay"; cp $P/libredwoodair.dylib "$F/badpay/"; printf 'x' >> "$F/badpay/libredwoodair.dylib"
refused guest-bigpool1x.sh "$F/badpay" "is not the y2s99c-perf one" "a payload library that is not the build"
setfile Nord2Metal "$AP/payload-api99a/Nord2Metal" 2>/dev/null || setfile Nord2Metal "$B/y2s99a-api-gaps/ship/payload-api99a/Nord2Metal"
refused guest-bigpool1x.sh $P "is not webxr1 36f755ea" "current Nord2Metal = api99a"
fresh lib; setfile libredwoodair.dylib "$AP/payload-gpufinal7/libredwoodair.dylib"
refused guest-bigpool1x.sh $P "is not webxr1" "current library = gpufinal7's"
fresh drift; printf 'x' >> "$R/$BU/libredwoodair.dylib"
refused guest-bigpool1x.sh $P "differs from its receipt" "a bundle that differs from its receipt"
fresh stale; python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['bigpool1']={'installed':{'x':'y'}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused guest-bigpool1x.sh $P "stale receipt record" "a stale record bigpool1"
fresh bak; printf 'other\n' > "$DBD/libredwoodair.dylib.pre-bigpool1"
refused guest-bigpool1x.sh $P "a different backup" "a different libredwoodair.dylib.pre-bigpool1 already there"
fresh link; ln -s /etc/passwd "$DBD/no-bigpool"
refused guest-bigpool1x.sh $P "not a regular file" "a switch that is a symlink"
fresh undo-foreign; setfile libredwoodair.dylib "$AP/payload-gpufinal7/libredwoodair.dylib"
refused guest-bigpool1x-undo.sh $PU "neither bigpool1 nor webxr1" "undo over gpufinal7's library"

echo REHEARSAL_OK
