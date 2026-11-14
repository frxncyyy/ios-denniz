#!/bin/bash
# webxr1: rehearsal of guest-webxr1.sh / -undo.sh (Nord2Metal 36f755ea + library 9f263e6f over shader99b fe4ecc9f + 7c0c5aba),
# made from apps-20261104/rehearse-shader99b.sh: fake roots made from the CURRENT mirror's bundle + receipt (mirror of 14 Nov 16:01 =
# shader99b installed). unshare -r; nothing touches the mirror or a phone. Fake roots under gpu/build/.rehearse-webxr1, removed at the
# end. Every write is checked against the NARROW owned list below (and owned-webxr1.json); every refusal leaves the root byte-identical.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); AP=$B/apps-20261104; BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d; NEWL=9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a
SHN=fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d; SHL=7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56
API=d86b3f9f9b731e35fc594d78e11de5efb9ba5793b24d19f3400e1357be163363; TL=3ae0c2f8fcd43a4b59ef0cb014df329f0231d960bbbf732dd03960e576227f3d
L6=d9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb
TOP=$B/.rehearse-webxr1; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$(basename $1).py"; unshare -r python3 "$F/$(basename $1).py"; }
st() { (cd "$R" && find . \( -type f -o -type d \) -printf '%p %m\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-webxr1.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
narrow={'/System/Library/Extensions/Nord2Metal.bundle/Nord2Metal','/System/Library/Extensions/Nord2Metal.bundle/libredwoodair.dylib','/private/var/db/nord2-metal/receipt.json','/private/var/db/nord2-metal/Nord2Metal.pre-webxr1','/private/var/db/nord2-metal/libredwoodair.dylib.pre-webxr1','/private/var/db/nord2-metal/no-webxr'}
assert narrow <= own, 'owned json misses %r'%(narrow-own)
own=narrow
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3])
bad=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k) and k not in own)
assert not bad, 'written outside owned-webxr1.json: %r'%bad
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
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-gpufinal6 libredwoodair.dylib.pre-gpufinal6 Nord2Metal.pre-texlayout1 Nord2Metal.pre-api99a Nord2Metal.pre-shader99b libredwoodair.dylib.pre-shader99b >/dev/null 2>&1 || true
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"
  [ "$(h Nord2Metal)" = $SHN ] && [ "$(h libredwoodair.dylib)" = $SHL ] || die "mirror is not shader99b (the phone per the mirror)"
}
refused() {  # refused <guest> <payload> <grep> <what>: must fail with that reason and change nothing
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
G=$A/guest-webxr1.sh; GU=$A/guest-webxr1-undo.sh
P=$A/payload-webxr1; PO=$A/payload-webxr1-off; PU=$A/payload-webxr1-undo
[ "$(sha256sum < $P/Nord2Metal | cut -c1-64)" = $NEWN ] && [ "$(sha256sum < $P/libredwoodair.dylib | cut -c1-64)" = $NEWL ] || die "payload files"
cmp -s $P/Nord2Metal "$A/build1/payload/Nord2Metal.bundle/Nord2Metal" || die "payload is not the build"   # (build2 gave the same bytes; deleted)
cmp -s $P/libredwoodair.dylib "$A/build1/payload/Nord2Metal.bundle/libredwoodair.dylib" || die "library is not the build"
cmp -s $P/Nord2Metal $PO/Nord2Metal && cmp -s $P/libredwoodair.dylib $PO/libredwoodair.dylib || die "-off payload files"
[ "$(sha256sum < $PU/Nord2Metal | cut -c1-64)" = $SHN ] && [ "$(sha256sum < $PU/libredwoodair.dylib | cut -c1-64)" = $SHL ] || die "undo payload files"

echo "== 1 phone state (shader99b): install, again, undo, again"
fresh phone; s0=$(st); snap "$F/0.snap"
run $G $P | grep -E '^WEBXR1_(INSTALLED|NOTE|FLAG)|^CC_'
[ "$(h Nord2Metal)" = $NEWN ] && [ "$(h libredwoodair.dylib)" = $NEWL ] || die "not installed"
python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['Nord2Metal']=='$NEWN' and i['libredwoodair.dylib']=='$NEWL' and rc['webxr1']['previous']=={'Nord2Metal':'$SHN','libredwoodair.dylib':'$SHL'} and 'shader99b' in rc and 'api99a' in rc" || die "receipt"
[ "$(sha256sum < "$DBD/Nord2Metal.pre-webxr1" | cut -c1-64)" = $SHN ] && [ "$(sha256sum < "$DBD/libredwoodair.dylib.pre-webxr1" | cut -c1-64)" = $SHL ] || die "backups"
[ "$(stat -c %a "$R/$BU/Nord2Metal")" = 755 ] || die "mode"
snap "$F/1.snap"; owned "$F/0.snap" "$F/1.snap"; s1=$(st)
run $G $P | grep -qE '^WEBXR1_ALREADY' || die "not idempotent"; [ "$(st)" = "$s1" ] || die "second install changed the root"
run $GU $PU | grep -E '^WEBXR1_UNDONE'
[ "$(st)" = "$s0" ] || die "undo is not byte-identical to the start"
run $GU $PU | grep -q "nothing to do" || die "second undo"; [ "$(st)" = "$s0" ] || die "second undo changed the root"

echo "== 2 the -off payload sets the switch; the plain payload clears it; undo removes ours"
run $G $PO | grep -E '^WEBXR1_(INSTALLED|FLAG)'
[ "$(cat "$DBD/no-webxr")" = made-by-webxr1 ] || die "switch not set"
snap "$F/2.snap"; owned "$F/0.snap" "$F/2.snap"
run $G $P | grep -E '^WEBXR1_(ALREADY|FLAG)'
[ ! -e "$DBD/no-webxr" ] || die "switch not cleared"
run $G $PO > /dev/null; run $GU $PU | grep -E '^WEBXR1_(UNDONE|FLAG)'
[ "$(st)" = "$s0" ] || die "undo after -off is not byte-identical to the start"

echo "== 3 a switch that is not ours is kept by install and by undo"
printf 'by hand\n' > "$DBD/no-webxr"; sf=$(st)
run $G $P | grep -E '^WEBXR1_NOTE|present \(not ours' || die "foreign switch not reported"
run $GU $PU > /dev/null
[ "$(cat "$DBD/no-webxr")" = "by hand" ] && [ "$(st)" = "$sf" ] || die "foreign switch touched"
rm "$DBD/no-webxr"

echo "== 4 undo without its backups uses the payload's copies; with a damaged backup it refuses"
run $G $P > /dev/null; rm "$DBD/Nord2Metal.pre-webxr1" "$DBD/libredwoodair.dylib.pre-webxr1"
run $GU $PU | grep -E '^WEBXR1_UNDONE' | grep -q fallback || die "fallback not used"
[ "$(st)" = "$s0" ] || die "fallback undo is not byte-identical to the start"
run $G $P > /dev/null; printf 'x' >> "$DBD/libredwoodair.dylib.pre-webxr1"
refused $GU $PU "no usable copy" "undo with a damaged library backup"
cp $PU/libredwoodair.dylib "$DBD/libredwoodair.dylib.pre-webxr1"
run $GU $PU > /dev/null; [ "$(st)" = "$s0" ] || die "undo after repair"

echo "== 6 refusals of the install (root unchanged)"
fresh other; mkdir "$F/badpay"; cp $P/Nord2Metal $P/libredwoodair.dylib "$F/badpay/"; printf 'x' >> "$F/badpay/libredwoodair.dylib"
refused $G "$F/badpay" "is not the webxr1 build" "a payload library that is not the build"
setfile Nord2Metal "$AP/payload-api99a/Nord2Metal"
refused $G $P "is not the shader99b state" "current Nord2Metal = api99a"
fresh lib; setfile libredwoodair.dylib "$AP/payload-gpufinal7/libredwoodair.dylib"
refused $G $P "is not the shader99b state" "current library = gpufinal7's"
fresh drift; printf 'x' >> "$R/$BU/libredwoodair.dylib"
refused $G $P "differs from its receipt" "a bundle that differs from its receipt"
fresh stale; python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['webxr1']={'installed':{'Nord2Metal':'x'}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused $G $P "stale receipt record" "a stale record webxr1"
fresh bak; printf 'other\n' > "$DBD/libredwoodair.dylib.pre-webxr1"
refused $G $P "a different backup" "a different libredwoodair.dylib.pre-webxr1 already there"
fresh link; ln -s /etc/passwd "$DBD/no-webxr"
refused $G $P "not a regular file" "a switch that is a symlink"
fresh undo-foreign; setfile Nord2Metal "$AP/payload-gpufinal7/Nord2Metal"
refused $GU $PU "neither webxr1 nor shader99b" "undo over gpufinal7"

echo "== 7 after webxr1 undo, shader99b's own undo still works (its record and backups were left)"
fresh chain; run $G $P > /dev/null; run $GU $PU > /dev/null
run "$AP/guest-shader99b-undo.sh" "$AP/payload-shader99b-undo" | grep -E '^SHADER99B_UNDONE' | cut -c1-200
[ "$(h Nord2Metal)" = $API ] || die "shader99b undo did not give api99a back"
echo REHEARSAL_OK
