#!/bin/bash
# Rehearsal of guest-gpufinal4.sh / -undo.sh (y2s94-final Nord2Metal + y2s91-thumbgrid library, one transaction) on fake roots made
# from the mirror's bundle + receipt + .pre-gpufinal3 backups (= the phone since cc-txn-gf3: gpufinal3 e31a9b07 + 7fa81fcf, record gpufinal3);
# unshare -r; nothing touches the mirror or a phone. Fake roots under gpu/build/.rehearse-gpufinal4, removed at the end. Every gpufinal4 write
# is checked against owned-gpufinal4.json; every refusal is checked to leave the root byte-identical. 'base' = the phone after
# guest-gpufinal3-undo.sh (thumbfix5 f90a091b + libmerge6 fe59bddb). Start states: phone (gpufinal3, plain and with its -off), base,
# thumbfix4+libmerge5, usecount1+libmerge6, gpufinal2; user switches; undo fallbacks; refusals.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=015f781d7f9dfa91ef012af8c903c11de0c8a313fd5b9ea895568bd895c5053f; NEWL=7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd
TOP=$B/.rehearse-gpufinal4; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
lst() { (cd "$R" && find . -mindepth 1 | sed 's|^\.||' | sort) > "$1"; }
owned() { python3 - "$R" "$A/owned-gpufinal4.json" "$1" <<'PY'
import json,os,sys
R,own,before=sys.argv[1:]; own=set(json.load(open(own))); old=set(open(before).read().split('\n'))
now=set()
for b,ds,fs in os.walk(R):
    for n in ds+fs: now.add('/'+os.path.relpath(os.path.join(b,n),R))
bad=[p for p in (now^old) if p and p not in own]
assert not bad, 'written outside owned-gpufinal4.json: %r'%bad
PY
}
setfile() { cp "$2" "$R/$BU/$1"; chmod 755 "$R/$BU/$1"; python3 - "$R" "$1" <<'PY'
import hashlib,json,sys
R,n=sys.argv[1:]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU][n]=hashlib.sha256(open(R+'/'+BU+'/'+n,'rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
}
fresh() {   # fresh <name>: fake root = the mirror's bundle + receipt
  F=$TOP/$1; mkdir -p "$F/root"; R=$F/root; DBD=$R/private/var/db/nord2-metal
  mkdir -p "$R/$BU" "$DBD"
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-gpufinal3 libredwoodair.dylib.pre-gpufinal3 >/dev/null 2>&1
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"
  chmod 755 "$DBD/Nord2Metal.pre-gpufinal3"
  [ "$(h Nord2Metal | cut -c1-8)" = e31a9b07 ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = 7fa81fcf ] || die "mirror is not gpufinal3 (phone)"
  [ "$(sha256sum < "$DBD/Nord2Metal.pre-gpufinal3" | cut -c1-8)" = f90a091b ] && [ "$(sha256sum < "$DBD/libredwoodair.dylib.pre-gpufinal3" | cut -c1-8)" = fe59bddb ] || die "mirror gpufinal3 backups"
}
base() {    # the phone with gpufinal3 undone by its own guest: thumbfix5 + libmerge6
  run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" >/dev/null
  [ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = fe59bddb ] || die "base is not thumbfix5 + libmerge6"
}
refused() {  # refused <guest> <payload> <grep> <what>: must fail with that reason and change nothing
  local s0; s0=$(st)
  if run "$1" "$2" > "$F/r.log" 2>&1; then die "$4 accepted"; fi
  grep -q -- "$3" "$F/r.log" || die "$4 refused for another reason: $(grep -m1 Error "$F/r.log")"
  [ "$(st)" = "$s0" ] || die "$4: refused but the root changed"
  echo "  refused, root unchanged: $4"
}
cycle() {   # install, again, undo, again -> byte-identical + owned
  local s0 n0 l0; s0=$(st); n0=$(h Nord2Metal); l0=$(h libredwoodair.dylib); lst "$F/before.lst"
  echo "== $1: Nord2Metal ${n0:0:12}, library ${l0:0:12}"
  run guest-gpufinal4.sh "$A/payload-gpufinal4" | grep -E '^GPUFINAL4_(INSTALLED|LIBRARY|NOTE|SWITCH)'
  run guest-gpufinal4.sh "$A/payload-gpufinal4" | grep -qE '^GPUFINAL4_ALREADY' || die "not idempotent"
  [ "$(h Nord2Metal)" = $NEWN ] && [ "$(h libredwoodair.dylib)" = $NEWL ] || die "pair not installed"
  python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['Nord2Metal']=='$NEWN' and i['libredwoodair.dylib']=='$NEWL' and rc['gpufinal4']['previous']=={'Nord2Metal':'$n0','libredwoodair.dylib':'$l0'}" || die "receipt"
  owned "$F/before.lst"
  ${2:-true}
  run guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" | grep -E '^GPUFINAL4_UNDONE'
  run guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" | grep -qE '^GPUFINAL4_UNDONE nothing to do' || die "undo not idempotent"
  [ ! -e "$DBD/Nord2Metal.pre-gpufinal4" ] && [ ! -e "$DBD/libredwoodair.dylib.pre-gpufinal4" ] || die "backups left"
  [ "$(st)" = "$s0" ] && echo "  install+again+undo+again byte-identical, owned: ok" || die "$1 not byte-identical after undo"
}
flagtest() {  # inside an installed gpufinal4: -off sets no-final, plain clears it, foreign flag kept
  run guest-gpufinal4.sh "$A/payload-gpufinal4-off" | grep -E '^GPUFINAL4_FLAG'
  [ "$(cat "$DBD/no-final")" = made-by-gpufinal4 ] || die "no-final not set"
  run guest-gpufinal4.sh "$A/payload-gpufinal4" | grep -E '^GPUFINAL4_FLAG'
  [ ! -e "$DBD/no-final" ] || die "no-final not cleared"
  run guest-gpufinal4.sh "$A/payload-gpufinal4-off" >/dev/null
  echo user > "$DBD/no-gamepipe"; echo user > "$DBD/no-thumbgrid-log"
  run guest-gpufinal4.sh "$A/payload-gpufinal4" | grep -E '^GPUFINAL4_SWITCH'
  run guest-gpufinal4.sh "$A/payload-gpufinal4-off" >/dev/null
  run guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" | grep -E '^GPUFINAL4_FLAG'
  [ "$(cat "$DBD/no-gamepipe")" = user ] && [ "$(cat "$DBD/no-thumbgrid-log")" = user ] || die "foreign flag touched"
  [ ! -e "$DBD/no-final" ] || die "our no-final left by undo"
  rm "$DBD/no-gamepipe" "$DBD/no-thumbgrid-log"
  run guest-gpufinal4.sh "$A/payload-gpufinal4" >/dev/null   # back to installed for cycle()'s own undo
  echo "  -off sets no-final, plain clears it, foreign switches kept by install and undo: ok"
}

# 1. the mirror state: thumbfix5 + libmerge6, full flag flow
fresh base; base; cycle "thumbfix5 + libmerge6 (base)" flagtest
# 2. THE PHONE STATE: gpufinal3 installed by its own guest over the mirror (cc-txn-gf3: thumbfix5+libmerge6 -> e31a9b07+7fa81fcf).
#    Nord2Metal e31a9b07 gets a .pre-gpufinal4 backup, library already new (left); gpufinal3's record/backups untouched; gpufinal3-undo
#    refused while gpufinal4 is installed and works after gpufinal4's undo (back to thumbfix5 + libmerge6)
for GP in payload-gpufinal3 payload-gpufinal3-off; do
  fresh "$GP"; run guest-gpufinal3.sh "$A/$GP" | grep -qE '^GPUFINAL3_ALREADY' || die "mirror gpufinal3 not ALREADY"
  cycle "gpufinal3 via ${GP#payload-} (= phone)" "eval refused guest-gpufinal3-undo.sh $A/payload-gpufinal3-undo neither 'gpufinal3-undo while gpufinal4 is installed'"
  if [ "$GP" = payload-gpufinal3-off ]; then [ "$(cat "$DBD/no-final")" = made-by-gpufinal3 ] || die "gpufinal3 flag touched"; fi
  python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));assert rc['gpufinal3']['installed']['Nord2Metal'].startswith('e31a9b07')" || die "gpufinal3 record touched"
  run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" | grep -E '^GPUFINAL3_UNDONE'
  [ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = fe59bddb ] || die "gpufinal3 undo after"
  [ ! -e "$DBD/no-final" ] || die "gpufinal3's no-final left after its undo"
  echo "  gpufinal3's own undo still works after gpufinal4's undo (${GP#payload-}): ok"
done
# 3. other combinations
for C in "payload-thumbfix4 payload-libmerge5" "payload-usecount1 payload-libmerge6"; do
  set -- $C; fresh "$1-$2"; base; setfile Nord2Metal "$A/$1/Nord2Metal"; setfile libredwoodair.dylib "$A/$2/libredwoodair.dylib"; cycle "${1#payload-} + ${2#payload-}"
done
fresh gpufinal2; base; run guest-gpufinal2.sh "$A/payload-gpufinal2" >/dev/null; [ "$(h Nord2Metal | cut -c1-8)" = be7a746d ] || die "gpufinal2 setup"
cycle "gpufinal2"
# 4. a user's usecount-yuv / final-camera: reported and kept by install and undo
fresh userflags; base; echo user > "$DBD/usecount-yuv"; echo user > "$DBD/final-camera"
run guest-gpufinal4.sh "$A/payload-gpufinal4" | grep -E '^GPUFINAL4_SWITCH usecount-yuv' || die "usecount-yuv not reported"
run guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" >/dev/null
[ "$(cat "$DBD/usecount-yuv")" = user ] && [ "$(cat "$DBD/final-camera")" = user ] || die "user flag touched"
echo "  a user's usecount-yuv / final-camera reported and kept by install and undo: ok"
# 5. undo fallbacks: over thumbfix5+libmerge6 with both backups lost -> fallbacks; over gpufinal3 with the backup lost -> refused
fresh fallback; base; S0=$(st); run guest-gpufinal4.sh "$A/payload-gpufinal4" >/dev/null; rm "$DBD"/*.pre-gpufinal4
run guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" | grep -E '^GPUFINAL4_UNDONE'; [ "$(st)" = "$S0" ] || die "fallback undo not byte-identical"
echo "  undo with both backups missing (fallbacks thumbfix5 + libmerge6): byte-identical: ok"
fresh fallback3; run guest-gpufinal4.sh "$A/payload-gpufinal4" >/dev/null; rm "$DBD/Nord2Metal.pre-gpufinal4"
refused guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" "no usable copy of the previous Nord2Metal" "undo with the gpufinal3 backup lost (no fallback for e31a9b07)"
# 6. refusals before any write
fresh refuse; base
echo foreign > "$F/foreign"; setfile Nord2Metal "$F/foreign"
refused guest-gpufinal4.sh "$A/payload-gpufinal4" "none of the known states" "foreign Nord2Metal"
setfile Nord2Metal "$A/payload-thumbfix5/Nord2Metal"; setfile libredwoodair.dylib "$F/foreign"
refused guest-gpufinal4.sh "$A/payload-gpufinal4" "none of the known states" "foreign library"
setfile libredwoodair.dylib "$A/payload-libmerge6/libredwoodair.dylib"; setfile Nord2Metal "$A/payload-gpufinal4/Nord2Metal"
refused guest-gpufinal4.sh "$A/payload-gpufinal4" "without its record" "y2s94 Nord2Metal without record"
setfile Nord2Metal "$A/payload-thumbfix5/Nord2Metal"
python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['gpufinal4']={'previous':{},'installed':{}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused guest-gpufinal4.sh "$A/payload-gpufinal4" "stale receipt record" "stale gpufinal4 record"
python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc.pop('gpufinal4');open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
echo bad > "$R/$BU/libredwoodair.dylib.nord2tmp"
refused guest-gpufinal4.sh "$A/payload-gpufinal4" "differs from its receipt" "stray file in the bundle"
rm "$R/$BU/libredwoodair.dylib.nord2tmp"
for p in payload-gpufinal4 payload-gpufinal4-off payload-gpufinal4-undo; do python3 -c "import json;json.load(open('$A/$p/plan.json'))"; done
echo "REHEARSAL_OK gpufinal4 (Nord2Metal 015f781d + library 7fa81fcf, one transaction) over gpufinal3 (mirror = phone, plain and +no-final, its own undo works after), thumbfix5+libmerge6, thumbfix4+libmerge5, usecount1+libmerge6, gpufinal2; idempotent; -off no-final set/cleared; user switches kept; undo byte-identical (backups or fallbacks); refusals change nothing; owned"
