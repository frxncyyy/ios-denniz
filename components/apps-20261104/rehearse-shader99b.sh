#!/bin/bash
# y2s99b: rehearsal of guest-shader99b.sh / -undo.sh (merged y2s99b+y2s99a Nord2Metal fe4ecc9f + library 7c0c5aba over api99a
# d86b3f9f / texlayout1 3ae0c2f8 + d9dd236e), made from rehearse-texlayout1.sh / rehearse-api99a.sh: fake roots made from the mirror's
# bundle + receipt (the mirror of 14 Nov 01:51 = api99a installed). unshare -r; nothing touches the mirror or a phone. Fake roots under
# gpu/build/.rehearse-shader99b, removed at the end. Every write is checked against owned-shader99b.json; every refusal leaves the
# root byte-identical.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d; NEWL=7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56
API=d86b3f9f9b731e35fc594d78e11de5efb9ba5793b24d19f3400e1357be163363; TL=3ae0c2f8fcd43a4b59ef0cb014df329f0231d960bbbf732dd03960e576227f3d
L6=d9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb
TOP=$B/.rehearse-shader99b; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$(basename $1).py"; unshare -r python3 "$F/$(basename $1).py"; }
st() { (cd "$R" && find . \( -type f -o -type d \) -printf '%p %m\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-shader99b.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3])
bad=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k) and k not in own)
assert not bad, 'written outside owned-shader99b.json: %r'%bad
print('  owned check: %d path(s) differ, all owned: %s'%(len([k for k in set(a)|set(b) if a.get(k)!=b.get(k)]), sorted(k.rsplit('/',1)[1] for k in set(a)|set(b) if a.get(k)!=b.get(k))))
PY
}
setfile() { cp "$2" "$R/$BU/$1"; python3 - "$R" "$1" <<'PY'
import hashlib,json,sys
R,n=sys.argv[1:]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
}
fresh() {   # fresh <name>: fake root = the mirror's bundle + receipt (+ the earlier deliveries' backups, to see they are left alone)
  F=$TOP/$1; mkdir -p "$F/root"; R=$F/root; DBD=$R/private/var/db/nord2-metal
  mkdir -p "$R/$BU" "$DBD"
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-gpufinal6 libredwoodair.dylib.pre-gpufinal6 Nord2Metal.pre-texlayout1 Nord2Metal.pre-api99a >/dev/null 2>&1 || true
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"
  [ "$(h Nord2Metal)" = $API ] && [ "$(h libredwoodair.dylib)" = $L6 ] || die "mirror is not api99a + d9dd236e (the phone per the mirror)"
}
refused() {  # refused <guest> <payload> <grep> <what>: must fail with that reason and change nothing
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
G=$A/guest-shader99b.sh; GU=$A/guest-shader99b-undo.sh
P=$A/payload-shader99b; PO=$A/payload-shader99b-off; PU=$A/payload-shader99b-undo
[ "$(sha256sum < $P/Nord2Metal | cut -c1-64)" = $NEWN ] && [ "$(sha256sum < $P/libredwoodair.dylib | cut -c1-64)" = $NEWL ] || die "payload files"
cmp -s $P/Nord2Metal "$B/y2s99b-shaders/build-merged/payload/Nord2Metal.bundle/Nord2Metal" || die "payload is not the merged build"
cmp -s $P/Nord2Metal $PO/Nord2Metal && cmp -s $P/libredwoodair.dylib $PO/libredwoodair.dylib || die "-off payload files"
[ "$(sha256sum < $PU/Nord2Metal | cut -c1-64)" = $API ] && [ "$(sha256sum < $PU/libredwoodair.dylib | cut -c1-64)" = $L6 ] || die "undo payload files"

echo "== 1 phone state (api99a): install, again, undo, again"
fresh phone; s0=$(st); snap "$F/0.snap"
run $G $P | grep -E '^SHADER99B_(INSTALLED|NOTE|FLAG)|^CC_'
[ "$(h Nord2Metal)" = $NEWN ] && [ "$(h libredwoodair.dylib)" = $NEWL ] || die "not installed"
python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['Nord2Metal']=='$NEWN' and i['libredwoodair.dylib']=='$NEWL' and rc['shader99b']['previous']=={'Nord2Metal':'$API','libredwoodair.dylib':'$L6'} and 'api99a' in rc and 'texlayout1' in rc" || die "receipt"
[ "$(sha256sum < "$DBD/Nord2Metal.pre-shader99b" | cut -c1-64)" = $API ] && [ "$(sha256sum < "$DBD/libredwoodair.dylib.pre-shader99b" | cut -c1-64)" = $L6 ] || die "backups"
[ "$(stat -c %a "$R/$BU/Nord2Metal")" = 755 ] || die "mode"
snap "$F/1.snap"; owned "$F/0.snap" "$F/1.snap"; s1=$(st)
run $G $P | grep -qE '^SHADER99B_ALREADY' || die "not idempotent"; [ "$(st)" = "$s1" ] || die "second install changed the root"
run $GU $PU | grep -E '^SHADER99B_UNDONE'
[ "$(st)" = "$s0" ] || die "undo is not byte-identical to the start"
run $GU $PU | grep -q "nothing to do" || die "second undo"; [ "$(st)" = "$s0" ] || die "second undo changed the root"

echo "== 2 the -off payload sets the switch; the plain payload clears it; undo removes ours"
run $G $PO | grep -E '^SHADER99B_(INSTALLED|FLAG)'
[ "$(cat "$DBD/no-shader99")" = made-by-shader99b ] || die "switch not set"
snap "$F/2.snap"; owned "$F/0.snap" "$F/2.snap"
run $G $P | grep -E '^SHADER99B_(ALREADY|FLAG)'
[ ! -e "$DBD/no-shader99" ] || die "switch not cleared"
run $G $PO > /dev/null; run $GU $PU | grep -E '^SHADER99B_(UNDONE|FLAG)'
[ "$(st)" = "$s0" ] || die "undo after -off is not byte-identical to the start"

echo "== 3 a switch that is not ours is kept by install and by undo"
printf 'by hand\n' > "$DBD/no-tgmem"; sf=$(st)
run $G $P | grep -E '^SHADER99B_NOTE' || die "foreign switch not reported"
run $GU $PU > /dev/null
[ "$(cat "$DBD/no-tgmem")" = "by hand" ] && [ "$(st)" = "$sf" ] || die "foreign switch touched"
rm "$DBD/no-tgmem"

echo "== 4 undo without its backups uses the payload's copies; with a damaged backup it refuses"
run $G $P > /dev/null; rm "$DBD/Nord2Metal.pre-shader99b" "$DBD/libredwoodair.dylib.pre-shader99b"
run $GU $PU | grep -E '^SHADER99B_UNDONE' | grep -q fallback || die "fallback not used"
[ "$(st)" = "$s0" ] || die "fallback undo is not byte-identical to the start"
run $G $P > /dev/null; printf 'x' >> "$DBD/libredwoodair.dylib.pre-shader99b"
refused $GU $PU "no usable copy" "undo with a damaged library backup"
cp $PU/libredwoodair.dylib "$DBD/libredwoodair.dylib.pre-shader99b"
run $GU $PU > /dev/null; [ "$(st)" = "$s0" ] || die "undo after repair"

echo "== 5 over texlayout1 (api99a not installed): install and undo"
fresh tl; run "$B/y2s99a-api-gaps/ship/guest-api99a-undo.sh" "$B/y2s99a-api-gaps/ship/payload-api99a-undo" | grep -E '^API99A_UNDONE' | cut -c1-120 || true
[ "$(h Nord2Metal)" = $TL ] || die "could not make a texlayout1 root"
s2=$(st); run $G $P | grep -E '^SHADER99B_INSTALLED' | cut -c1-160
run $GU $PU | grep -E '^SHADER99B_UNDONE' 
[ "$(st)" = "$s2" ] || die "undo over texlayout1 is not byte-identical"

echo "== 6 refusals of the install (root unchanged)"
fresh other; mkdir "$F/badpay"; cp $P/Nord2Metal $P/libredwoodair.dylib "$F/badpay/"; printf 'x' >> "$F/badpay/libredwoodair.dylib"
refused $G "$F/badpay" "is not the y2s99b-shaders one" "a payload library that is not the build"
setfile Nord2Metal "$A/payload-gpufinal7/Nord2Metal"
refused $G $P "is not the texlayout1 state" "current Nord2Metal = gpufinal7"
fresh lib; setfile libredwoodair.dylib "$A/payload-gpufinal7/libredwoodair.dylib"
refused $G $P "is not the texlayout1 state" "current library = gpufinal7's"
fresh drift; printf 'x' >> "$R/$BU/libredwoodair.dylib"
refused $G $P "differs from its receipt" "a bundle that differs from its receipt"
fresh stale; python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['shader99b']={'installed':{'Nord2Metal':'x'}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused $G $P "stale receipt record" "a stale record shader99b"
fresh bak; printf 'other\n' > "$DBD/libredwoodair.dylib.pre-shader99b"
refused $G $P "a different backup" "a different libredwoodair.dylib.pre-shader99b already there"
fresh link; ln -s /etc/passwd "$DBD/no-blendcolor"
refused $G $P "not a regular file" "a switch that is a symlink"
fresh undo-foreign; setfile Nord2Metal "$A/payload-gpufinal7/Nord2Metal"
refused $GU $PU "neither shader99b nor texlayout1" "undo over gpufinal7"

echo "== 7 after shader99b undo, api99a's own undo still works (its record and backup were left)"
fresh chain; run $G $P > /dev/null; run $GU $PU > /dev/null
run "$B/y2s99a-api-gaps/ship/guest-api99a-undo.sh" "$B/y2s99a-api-gaps/ship/payload-api99a-undo" | grep -E '^API99A_UNDONE' | cut -c1-160
[ "$(h Nord2Metal)" = $TL ] || die "api99a undo did not give texlayout1 back"
echo REHEARSAL_OK
