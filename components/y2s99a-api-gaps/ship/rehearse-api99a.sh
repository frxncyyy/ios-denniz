#!/bin/bash
# y2s99a: rehearsal of guest-api99a.sh / -undo.sh (y2s99a-api-gaps Nord2Metal over texlayout1 3ae0c2f8; library untouched), made from
# apps-20261104/rehearse-texlayout1.sh: fake roots made
# from the mirror's bundle + receipt (the mirror = THE PHONE: texlayout1 3ae0c2f8 + d9dd236e). unshare -r; nothing touches the mirror
# or a phone. Fake roots under gpu/build/.rehearse-api99a, removed at the end. Every write is checked against owned-api99a.json
# (paths added/removed AND files whose content changed); every refusal is checked to leave the root byte-identical.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/../.." && pwd); AP=$B/apps-20261104; BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=$(sha256sum < "$A/payload-api99a/Nord2Metal" | cut -c1-64); TL=3ae0c2f8fcd43a4b59ef0cb014df329f0231d960bbbf732dd03960e576227f3d
N6=fe406e9c91b42f063235cbac4035977e63bf59012f7c731a0aa4587139435bfe; L6=d9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb
TOP=$B/.rehearse-api99a; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . \( -type f -o -type d \) -printf '%p %m\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-api99a.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3])
bad=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k) and k not in own)
assert not bad, 'written outside owned-api99a.json: %r'%bad
print('  owned check: %d path(s) differ, all owned: %s'%(len([k for k in set(a)|set(b) if a.get(k)!=b.get(k)]), sorted(k.rsplit('/',1)[1] for k in set(a)|set(b) if a.get(k)!=b.get(k))))
PY
}
setfile() { cp "$2" "$R/$BU/$1"; chmod 755 "$R/$BU/$1"; python3 - "$R" "$1" <<'PY'
import hashlib,json,sys
R,n=sys.argv[1:]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
}
fresh() {   # fresh <name>: fake root = the mirror's bundle + receipt (+ gpufinal6's own backups, to see they are left alone)
  F=$TOP/$1; mkdir -p "$F/root"; R=$F/root; DBD=$R/private/var/db/nord2-metal
  mkdir -p "$R/$BU" "$DBD"
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-gpufinal6 libredwoodair.dylib.pre-gpufinal6 Nord2Metal.pre-texlayout1 >/dev/null 2>&1
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal" "$DBD/Nord2Metal.pre-gpufinal6" "$DBD/Nord2Metal.pre-texlayout1"
  [ "$(h Nord2Metal)" = $TL ] && [ "$(h libredwoodair.dylib)" = $L6 ] || die "mirror is not texlayout1 (phone)"
}
refused() {  # refused <guest> <payload> <grep> <what>: must fail with that reason and change nothing
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
P=$A/payload-api99a; PO=$A/payload-api99a-off; PU=$A/payload-api99a-undo
[ "$(sha256sum < $P/Nord2Metal | cut -c1-64)" = $NEWN ] && cmp -s $P/Nord2Metal $PO/Nord2Metal && [ "$(sha256sum < $PU/Nord2Metal | cut -c1-64)" = $TL ] || die "payload files"
cmp -s $P/Nord2Metal "$B/y2s99a-api-gaps/build2/payload/Nord2Metal.bundle/Nord2Metal" || die "payload is not the y2s99a build"

echo "== 1 phone state (texlayout1): install, again, undo, again"
fresh phone; s0=$(st); snap "$F/0.snap"
run guest-api99a.sh $P | grep -E '^API99A_(INSTALLED|NOTE|FLAG)|^CC_'
[ "$(h Nord2Metal)" = $NEWN ] && [ "$(h libredwoodair.dylib)" = $L6 ] || die "not installed / library touched"
python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['Nord2Metal']=='$NEWN' and i['libredwoodair.dylib']=='$L6' and rc['api99a']['previous']=={'Nord2Metal':'$TL'} and 'texlayout1' in rc" || die "receipt"
[ "$(sha256sum < "$DBD/Nord2Metal.pre-api99a" | cut -c1-64)" = $TL ] || die "backup"
[ "$(stat -c %a "$R/$BU/Nord2Metal")" = 755 ] || die "mode"
snap "$F/1.snap"; owned "$F/0.snap" "$F/1.snap"; s1=$(st)
run guest-api99a.sh $P | grep -qE '^API99A_ALREADY' || die "not idempotent"; [ "$(st)" = "$s1" ] || die "second install changed the root"
run guest-api99a-undo.sh $PU | grep -E '^API99A_UNDONE'
[ "$(st)" = "$s0" ] || die "undo is not byte-identical to the start"
run guest-api99a-undo.sh $PU | grep -q "nothing to do" || die "second undo"; [ "$(st)" = "$s0" ] || die "second undo changed the root"

echo "== 2 the -off payload sets the switch; the plain payload clears it; undo removes ours"
run guest-api99a.sh $PO | grep -E '^API99A_(INSTALLED|FLAG)'
[ "$(cat "$DBD/no-api99a")" = made-by-api99a ] || die "switch not set"
snap "$F/2.snap"; owned "$F/0.snap" "$F/2.snap"
run guest-api99a.sh $P | grep -E '^API99A_(ALREADY|FLAG)'
[ ! -e "$DBD/no-api99a" ] || die "switch not cleared"
run guest-api99a.sh $PO > /dev/null; run guest-api99a-undo.sh $PU | grep -E '^API99A_(UNDONE|FLAG)'
[ "$(st)" = "$s0" ] || die "undo after -off is not byte-identical to the start"

echo "== 3 a switch that is not ours is kept by install and by undo"
printf 'by hand\n' > "$DBD/no-api99a"; sf=$(st)
run guest-api99a.sh $P | grep -E '^API99A_NOTE' || die "foreign switch not reported"
run guest-api99a-undo.sh $PU | grep -E '^API99A_NOTE' || die "foreign switch not reported by undo"
[ "$(cat "$DBD/no-api99a")" = "by hand" ] && [ "$(st)" = "$sf" ] || die "foreign switch touched"
rm "$DBD/no-api99a"

echo "== 4 undo without its backup uses the payload's texlayout1 copy; with a different backup it refuses"
run guest-api99a.sh $P > /dev/null; rm "$DBD/Nord2Metal.pre-api99a"
run guest-api99a-undo.sh $PU | grep -E '^API99A_UNDONE' | grep -q fallback || die "fallback not used"
[ "$(st)" = "$s0" ] || die "fallback undo is not byte-identical to the start"
run guest-api99a.sh $P > /dev/null; printf 'x' >> "$DBD/Nord2Metal.pre-api99a"
refused guest-api99a-undo.sh $PU "no usable copy" "undo with a damaged backup"
cp $PU/Nord2Metal "$DBD/Nord2Metal.pre-api99a"; chmod 755 "$DBD/Nord2Metal.pre-api99a"
run guest-api99a-undo.sh $PU > /dev/null; [ "$(st)" = "$s0" ] || die "undo after repair"

echo "== 5 refusals of the install (root unchanged)"
fresh other; cp $P/Nord2Metal "$F/n.bad"; printf 'x' >> "$F/n.bad"; mkdir "$F/badpay"; cp "$F/n.bad" "$F/badpay/Nord2Metal"
refused guest-api99a.sh "$F/badpay" "is not the y2s99a-api-gaps one" "a payload Nord2Metal that is not the build"
setfile Nord2Metal "$AP/payload-gpufinal7/Nord2Metal"
refused guest-api99a.sh $P "is not texlayout1" "current Nord2Metal = gpufinal7 (not the state it is built on)"
fresh lib; setfile libredwoodair.dylib "$AP/payload-gpufinal7/libredwoodair.dylib"; chmod 644 "$R/$BU/libredwoodair.dylib"
refused guest-api99a.sh $P "is not gpufinal6's d9dd236e" "current library = gpufinal7's"
fresh drift; printf 'x' >> "$R/$BU/Nord2Metal"
refused guest-api99a.sh $P "differs from its receipt" "a bundle that differs from its receipt"
fresh stale; python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['api99a']={'installed':{'Nord2Metal':'x'}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused guest-api99a.sh $P "stale receipt record" "a stale record api99a"
fresh tmp; : > "$R/$BU/Nord2Metal.nord2tmp"; python3 - "$DBD/receipt.json" "$BU" <<'PY'
import json,sys,hashlib
p,bu=sys.argv[1:]; rc=json.load(open(p)); rc['inventory'][bu]['Nord2Metal.nord2tmp']=hashlib.sha256(b'').hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
refused guest-api99a.sh $P "leftover .nord2tmp" "a leftover .nord2tmp"
fresh bak; printf 'other\n' > "$DBD/Nord2Metal.pre-api99a"
refused guest-api99a.sh $P "a different backup" "a different Nord2Metal.pre-api99a already there"
fresh link; ln -s /etc/passwd "$DBD/no-api99a"
refused guest-api99a.sh $P "not a regular file" "a switch that is a symlink"
fresh undo-foreign; setfile Nord2Metal "$AP/payload-gpufinal7/Nord2Metal"
refused guest-api99a-undo.sh $PU "neither api99a nor texlayout1" "undo over gpufinal7"

echo "== 6 after api99a undo, texlayout1's own undo still works (its record and backup were left)"
fresh chain; run guest-api99a.sh $P > /dev/null; run guest-api99a-undo.sh $PU > /dev/null
run_ap() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$AP/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
run_ap guest-texlayout1-undo.sh "$AP/payload-texlayout1-undo" | grep -E '^TEXLAYOUT1_UNDONE' | cut -c1-160
[ "$(h Nord2Metal)" = $N6 ] || die "texlayout1 undo did not give gpufinal6 back"
fresh chain2; run guest-api99a.sh $P > /dev/null
if run_ap guest-texlayout1-undo.sh "$AP/payload-texlayout1-undo" > "$F/t1.log" 2>&1; then die "texlayout1-undo accepted over api99a (must be undone first)"; fi
echo "  texlayout1-undo over api99a refuses (undo api99a first): $(grep -m1 -o 'AssertionError.*' "$F/t1.log" | cut -c1-110)"
echo REHEARSAL_OK
