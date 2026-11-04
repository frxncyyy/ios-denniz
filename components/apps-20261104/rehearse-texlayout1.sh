#!/bin/bash
# Rehearsal of guest-texlayout1.sh / -undo.sh (y2s98-texlayout Nord2Metal 3ae0c2f8 over gpufinal6; library untouched) on fake roots made
# from the mirror's bundle + receipt (the mirror = THE PHONE: gpufinal6 fe406e9c + d9dd236e). unshare -r; nothing touches the mirror
# or a phone. Fake roots under gpu/build/.rehearse-texlayout1, removed at the end. Every write is checked against owned-texlayout1.json
# (paths added/removed AND files whose content changed); every refusal is checked to leave the root byte-identical.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=3ae0c2f8fcd43a4b59ef0cb014df329f0231d960bbbf732dd03960e576227f3d
N6=fe406e9c91b42f063235cbac4035977e63bf59012f7c731a0aa4587139435bfe; L6=d9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb
TOP=$B/.rehearse-texlayout1; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . \( -type f -o -type d \) -printf '%p %m\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
snap() { (cd "$R" && find . -mindepth 1 \( -type d -printf '%p directory\n' -o -type f -exec sha256sum {} + \) | sed 's|^\([0-9a-f]\{64\}\)  \(.*\)$|\2 \1|' | sort) > "$1"; }
owned() { python3 - "$A/owned-texlayout1.json" "$1" "$2" <<'PY'
import json,sys
own=set(json.load(open(sys.argv[1])))
def load(p):
    d={}
    for l in open(p):
        k,v=l.rstrip('\n').rsplit(' ',1); d[k[1:]]=v
    return d
a,b=load(sys.argv[2]),load(sys.argv[3])
bad=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k) and k not in own)
assert not bad, 'written outside owned-texlayout1.json: %r'%bad
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
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-gpufinal6 libredwoodair.dylib.pre-gpufinal6 >/dev/null 2>&1
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal" "$DBD/Nord2Metal.pre-gpufinal6"
  [ "$(h Nord2Metal)" = $N6 ] && [ "$(h libredwoodair.dylib)" = $L6 ] || die "mirror is not gpufinal6 (phone)"
}
refused() {  # refused <guest> <payload> <grep> <what>: must fail with that reason and change nothing
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
P=$A/payload-texlayout1; PO=$A/payload-texlayout1-off; PU=$A/payload-texlayout1-undo
[ "$(sha256sum < $P/Nord2Metal | cut -c1-64)" = $NEWN ] && cmp -s $P/Nord2Metal $PO/Nord2Metal && [ "$(sha256sum < $PU/Nord2Metal | cut -c1-64)" = $N6 ] || die "payload files"
cmp -s $P/Nord2Metal "$B/y2s98-texlayout/payload/Nord2Metal.bundle/Nord2Metal" || die "payload is not the y2s98-texlayout build"

echo "== 1 phone state (gpufinal6): install, again, undo, again"
fresh phone; s0=$(st); snap "$F/0.snap"
run guest-texlayout1.sh $P | grep -E '^TEXLAYOUT1_(INSTALLED|NOTE|FLAG)|^CC_'
[ "$(h Nord2Metal)" = $NEWN ] && [ "$(h libredwoodair.dylib)" = $L6 ] || die "not installed / library touched"
python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['Nord2Metal']=='$NEWN' and i['libredwoodair.dylib']=='$L6' and rc['texlayout1']['previous']=={'Nord2Metal':'$N6'} and 'gpufinal6' in rc" || die "receipt"
[ "$(sha256sum < "$DBD/Nord2Metal.pre-texlayout1" | cut -c1-64)" = $N6 ] || die "backup"
[ "$(stat -c %a "$R/$BU/Nord2Metal")" = 755 ] || die "mode"
snap "$F/1.snap"; owned "$F/0.snap" "$F/1.snap"; s1=$(st)
run guest-texlayout1.sh $P | grep -qE '^TEXLAYOUT1_ALREADY' || die "not idempotent"; [ "$(st)" = "$s1" ] || die "second install changed the root"
run guest-texlayout1-undo.sh $PU | grep -E '^TEXLAYOUT1_UNDONE'
[ "$(st)" = "$s0" ] || die "undo is not byte-identical to the start"
run guest-texlayout1-undo.sh $PU | grep -q "nothing to do" || die "second undo"; [ "$(st)" = "$s0" ] || die "second undo changed the root"

echo "== 2 the -off payload sets the switch; the plain payload clears it; undo removes ours"
run guest-texlayout1.sh $PO | grep -E '^TEXLAYOUT1_(INSTALLED|FLAG)'
[ "$(cat "$DBD/no-texlayout")" = made-by-texlayout1 ] || die "switch not set"
snap "$F/2.snap"; owned "$F/0.snap" "$F/2.snap"
run guest-texlayout1.sh $P | grep -E '^TEXLAYOUT1_(ALREADY|FLAG)'
[ ! -e "$DBD/no-texlayout" ] || die "switch not cleared"
run guest-texlayout1.sh $PO > /dev/null; run guest-texlayout1-undo.sh $PU | grep -E '^TEXLAYOUT1_(UNDONE|FLAG)'
[ "$(st)" = "$s0" ] || die "undo after -off is not byte-identical to the start"

echo "== 3 a switch that is not ours is kept by install and by undo"
printf 'by hand\n' > "$DBD/no-texlayout"; sf=$(st)
run guest-texlayout1.sh $P | grep -E '^TEXLAYOUT1_NOTE' || die "foreign switch not reported"
run guest-texlayout1-undo.sh $PU | grep -E '^TEXLAYOUT1_NOTE' || die "foreign switch not reported by undo"
[ "$(cat "$DBD/no-texlayout")" = "by hand" ] && [ "$(st)" = "$sf" ] || die "foreign switch touched"
rm "$DBD/no-texlayout"

echo "== 4 undo without its backup uses the payload's gpufinal6 copy; with a different backup it refuses"
run guest-texlayout1.sh $P > /dev/null; rm "$DBD/Nord2Metal.pre-texlayout1"
run guest-texlayout1-undo.sh $PU | grep -E '^TEXLAYOUT1_UNDONE' | grep -q fallback || die "fallback not used"
[ "$(st)" = "$s0" ] || die "fallback undo is not byte-identical to the start"
run guest-texlayout1.sh $P > /dev/null; printf 'x' >> "$DBD/Nord2Metal.pre-texlayout1"
refused guest-texlayout1-undo.sh $PU "no usable copy" "undo with a damaged backup"
cp $PU/Nord2Metal "$DBD/Nord2Metal.pre-texlayout1"; chmod 755 "$DBD/Nord2Metal.pre-texlayout1"
run guest-texlayout1-undo.sh $PU > /dev/null; [ "$(st)" = "$s0" ] || die "undo after repair"

echo "== 5 refusals of the install (root unchanged)"
fresh other; cp $P/Nord2Metal "$F/n.bad"; printf 'x' >> "$F/n.bad"; mkdir "$F/badpay"; cp "$F/n.bad" "$F/badpay/Nord2Metal"
refused guest-texlayout1.sh "$F/badpay" "is not the y2s98-texlayout one" "a payload Nord2Metal that is not the build"
setfile Nord2Metal "$A/payload-gpufinal7/Nord2Metal"
refused guest-texlayout1.sh $P "is not gpufinal6" "current Nord2Metal = gpufinal7 (not the state it is built on)"
fresh lib; setfile libredwoodair.dylib "$A/payload-gpufinal7/libredwoodair.dylib"; chmod 644 "$R/$BU/libredwoodair.dylib"
refused guest-texlayout1.sh $P "is not gpufinal6's d9dd236e" "current library = gpufinal7's"
fresh drift; printf 'x' >> "$R/$BU/Nord2Metal"
refused guest-texlayout1.sh $P "differs from its receipt" "a bundle that differs from its receipt"
fresh stale; python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['texlayout1']={'installed':{'Nord2Metal':'x'}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused guest-texlayout1.sh $P "stale receipt record" "a stale record texlayout1"
fresh tmp; : > "$R/$BU/Nord2Metal.nord2tmp"; python3 - "$DBD/receipt.json" "$BU" <<'PY'
import json,sys,hashlib
p,bu=sys.argv[1:]; rc=json.load(open(p)); rc['inventory'][bu]['Nord2Metal.nord2tmp']=hashlib.sha256(b'').hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
refused guest-texlayout1.sh $P "leftover .nord2tmp" "a leftover .nord2tmp"
fresh bak; printf 'other\n' > "$DBD/Nord2Metal.pre-texlayout1"
refused guest-texlayout1.sh $P "a different backup" "a different Nord2Metal.pre-texlayout1 already there"
fresh link; ln -s /etc/passwd "$DBD/no-texlayout"
refused guest-texlayout1.sh $P "not a regular file" "a switch that is a symlink"
fresh undo-foreign; setfile Nord2Metal "$A/payload-gpufinal7/Nord2Metal"
refused guest-texlayout1-undo.sh $PU "neither texlayout1 nor gpufinal6" "undo over gpufinal7"

echo "== 6 after texlayout1 undo, gpufinal6's own undo still works (its record and backups were left)"
fresh chain; run guest-texlayout1.sh $P > /dev/null; run guest-texlayout1-undo.sh $PU > /dev/null
run guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" | grep -E '^GPUFINAL6_UNDONE' | cut -c1-160
[ "$(h Nord2Metal | cut -c1-8)" = 015f781d ] || die "gpufinal6 undo did not give gpufinal4 back"
fresh chain2; run guest-texlayout1.sh $P > /dev/null
if run guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" > "$F/g6.log" 2>&1; then die "gpufinal6-undo accepted over texlayout1 (must be undone first)"; fi
echo "  gpufinal6-undo over texlayout1 refuses (undo texlayout1 first): $(grep -m1 -o 'AssertionError.*' "$F/g6.log" | cut -c1-110)"
echo REHEARSAL_OK
