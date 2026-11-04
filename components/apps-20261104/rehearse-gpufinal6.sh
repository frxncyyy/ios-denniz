#!/bin/bash
# Rehearsal of guest-gpufinal6.sh / -undo.sh (y2s96-final Nord2Metal fe406e9c + library d9dd236e, one transaction) on fake roots made from the
# mirror's bundle + receipt + .pre-gpufinal3/.pre-gpufinal4 backups (= THE PHONE since cc-txn-gf4: gpufinal4 015f781d + 7fa81fcf, records
# gpufinal3 + gpufinal4), and on gpufinal3 / the base made from it by their own undo guests. unshare -r; nothing touches the mirror or a phone.
# Fake roots under gpu/build/.rehearse-gpufinal6, removed at the end. Every gpufinal6 write is checked against owned-gpufinal6.json; every
# refusal is checked to leave the root byte-identical. Undo chains: gpufinal6-undo -> gpufinal4-undo -> gpufinal3-undo back to the mirror's base.
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=fe406e9c91b42f063235cbac4035977e63bf59012f7c731a0aa4587139435bfe; NEWL=d9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb
TOP=$B/.rehearse-gpufinal6; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
lst() { (cd "$R" && find . -mindepth 1 | sed 's|^\.||' | sort) > "$1"; }
owned() { python3 - "$R" "$A/owned-gpufinal6.json" "$1" <<'PY'
import json,os,sys
R,own,before=sys.argv[1:]; own=set(json.load(open(own))); old=set(open(before).read().split('\n'))
now=set()
for b,ds,fs in os.walk(R):
    for n in ds+fs: now.add('/'+os.path.relpath(os.path.join(b,n),R))
bad=[p for p in (now^old) if p and p not in own]
assert not bad, 'written outside owned-gpufinal6.json: %r'%bad
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
  $H get /private/var/db/nord2-metal "$DBD" receipt.json Nord2Metal.pre-gpufinal3 libredwoodair.dylib.pre-gpufinal3 Nord2Metal.pre-gpufinal4 >/dev/null 2>&1
  local NAMES; NAMES=$(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
  $H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
  for d in $(python3 -c "import json;inv=json.load(open('$DBD/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
  chmod 755 "$R/$BU/Nord2Metal"
  chmod 755 "$DBD/Nord2Metal.pre-gpufinal3" "$DBD/Nord2Metal.pre-gpufinal4"
  [ "$(h Nord2Metal | cut -c1-8)" = 015f781d ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = 7fa81fcf ] || die "mirror is not gpufinal4 (phone)"
  [ "$(sha256sum < "$DBD/Nord2Metal.pre-gpufinal4" | cut -c1-8)" = e31a9b07 ] || die "mirror gpufinal4 backup"
  [ "$(sha256sum < "$DBD/Nord2Metal.pre-gpufinal3" | cut -c1-8)" = f90a091b ] && [ "$(sha256sum < "$DBD/libredwoodair.dylib.pre-gpufinal3" | cut -c1-8)" = fe59bddb ] || die "mirror gpufinal3 backups"
}
gf3() {     # the phone with gpufinal4 undone by its own guest: gpufinal3 e31a9b07 + 7fa81fcf
  run guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" >/dev/null
  [ "$(h Nord2Metal | cut -c1-8)" = e31a9b07 ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = 7fa81fcf ] || die "gf3 is not gpufinal3"
}
base() {    # then gpufinal3 undone by its own guest: thumbfix5 + libmerge6
  gf3
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
  run guest-gpufinal6.sh "$A/payload-gpufinal6" | grep -E '^GPUFINAL6_(INSTALLED|LIBRARY|NOTE|SWITCH)'
  run guest-gpufinal6.sh "$A/payload-gpufinal6" | grep -qE '^GPUFINAL6_ALREADY' || die "not idempotent"
  [ "$(h Nord2Metal)" = $NEWN ] && [ "$(h libredwoodair.dylib)" = $NEWL ] || die "pair not installed"
  python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));i=rc['inventory']['$BU'];assert i['Nord2Metal']=='$NEWN' and i['libredwoodair.dylib']=='$NEWL' and rc['gpufinal6']['previous']=={'Nord2Metal':'$n0','libredwoodair.dylib':'$l0'}" || die "receipt"
  [ "$(sha256sum < "$DBD/Nord2Metal.pre-gpufinal6" | cut -c1-64)" = $n0 ] && [ "$(sha256sum < "$DBD/libredwoodair.dylib.pre-gpufinal6" | cut -c1-64)" = $l0 ] || die "backups"
  owned "$F/before.lst"
  ${2:-true}
  run guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" | grep -E '^GPUFINAL6_UNDONE'
  run guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" | grep -qE '^GPUFINAL6_UNDONE nothing to do' || die "undo not idempotent"
  [ ! -e "$DBD/Nord2Metal.pre-gpufinal6" ] && [ ! -e "$DBD/libredwoodair.dylib.pre-gpufinal6" ] || die "backups left"
  [ "$(st)" = "$s0" ] && echo "  install+again+undo+again byte-identical, owned: ok" || die "$1 not byte-identical after undo"
}
flagtest() {  # inside an installed gpufinal6: -off sets no-rt1 + no-singletarget, plain clears them, foreign flags kept
  run guest-gpufinal6.sh "$A/payload-gpufinal6-off" | grep -E '^GPUFINAL6_FLAG'
  [ "$(cat "$DBD/no-rt1")" = made-by-gpufinal6 ] && [ "$(cat "$DBD/no-singletarget")" = made-by-gpufinal6 ] || die "no-rt1/no-singletarget not set"
  run guest-gpufinal6.sh "$A/payload-gpufinal6" | grep -E '^GPUFINAL6_FLAG'
  [ ! -e "$DBD/no-rt1" ] && [ ! -e "$DBD/no-singletarget" ] || die "no-rt1/no-singletarget not cleared"
  run guest-gpufinal6.sh "$A/payload-gpufinal6-tracecc" | grep -E '^GPUFINAL6_FLAG'
  [ "$(cat "$DBD/trace-cc")" = made-by-gpufinal6 ] || die "trace-cc not set"
  run guest-gpufinal6.sh "$A/payload-gpufinal6" | grep -E '^GPUFINAL6_FLAG'
  [ ! -e "$DBD/trace-cc" ] || die "trace-cc not cleared"
  run guest-gpufinal6.sh "$A/payload-gpufinal6-off" >/dev/null
  echo user > "$DBD/no-gamepipe"; echo user > "$DBD/pressure-on"
  run guest-gpufinal6.sh "$A/payload-gpufinal6" | grep -E '^GPUFINAL6_SWITCH'
  run guest-gpufinal6.sh "$A/payload-gpufinal6-off" >/dev/null
  run guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" | grep -E '^GPUFINAL6_FLAG'
  [ "$(cat "$DBD/no-gamepipe")" = user ] && [ "$(cat "$DBD/pressure-on")" = user ] || die "foreign flag touched"
  [ ! -e "$DBD/no-rt1" ] && [ ! -e "$DBD/no-singletarget" ] || die "our no-rt1/no-singletarget left by undo"
  rm "$DBD/no-gamepipe" "$DBD/pressure-on"
  run guest-gpufinal6.sh "$A/payload-gpufinal6" >/dev/null   # back to installed for cycle()'s own undo
  echo "  -off sets no-rt1 + no-singletarget, plain clears them, foreign switches kept by install and undo: ok"
}
N4=015f781d7f9dfa91ef012af8c903c11de0c8a313fd5b9ea895568bd895c5053f; L4=7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd

# 1. the mirror's base (gpufinal3 undone: thumbfix5 + libmerge6), full flag flow
fresh base; base; cycle "thumbfix5 + libmerge6 (base)" flagtest
# 2. gpufinal3 (cc-txn-gf3 state, plain and +no-final): gpufinal3-undo refused while gpufinal6 is installed; after gpufinal6's undo it works
for GP in payload-gpufinal3 payload-gpufinal3-off; do
  fresh "$GP"; gf3; run guest-gpufinal3.sh "$A/$GP" | grep -qE '^GPUFINAL3_ALREADY' || die "gpufinal3 not ALREADY"
  cycle "gpufinal3 via ${GP#payload-}" "eval refused guest-gpufinal3-undo.sh $A/payload-gpufinal3-undo neither 'gpufinal3-undo while gpufinal6 is installed'"
  if [ "$GP" = payload-gpufinal3-off ]; then [ "$(cat "$DBD/no-final")" = made-by-gpufinal3 ] || die "gpufinal3 flag touched"; fi
  python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));assert rc['gpufinal3']['installed']['Nord2Metal'].startswith('e31a9b07')" || die "gpufinal3 record touched"
  run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" | grep -E '^GPUFINAL3_UNDONE'
  [ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = fe59bddb ] || die "gpufinal3 undo after"
  echo "  gpufinal3's own undo still works after gpufinal6's undo (${GP#payload-}): ok"
done
# 3. THE PHONE: gpufinal4 over gpufinal3 (as cc-txn-gf4 left it; and with gpufinal4's -off): gpufinal4-undo refused while gpufinal6 is
#    installed; after gpufinal6's undo: gpufinal4-undo, then gpufinal3-undo -> thumbfix5 + libmerge6
for GP in payload-gpufinal4 payload-gpufinal4-off; do
  fresh "$GP"; run guest-gpufinal4.sh "$A/$GP" | grep -qE '^GPUFINAL4_ALREADY' || die "mirror gpufinal4 not ALREADY"
  [ "$(h Nord2Metal)" = $N4 ] && [ "$(h libredwoodair.dylib)" = $L4 ] || die "gpufinal4 setup pair"
  cycle "gpufinal4 via ${GP#payload-} over gpufinal3" "eval refused guest-gpufinal4-undo.sh $A/payload-gpufinal4-undo neither 'gpufinal4-undo while gpufinal6 is installed'"
  python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));assert rc['gpufinal4']['installed']['Nord2Metal']=='$N4' and rc['gpufinal3']['installed']['Nord2Metal'].startswith('e31a9b07')" || die "gpufinal4/3 records touched"
  if [ "$GP" = payload-gpufinal4-off ]; then [ "$(cat "$DBD/no-final")" = made-by-gpufinal4 ] || die "gpufinal4 flag touched"; fi
  run guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" | grep -E '^GPUFINAL4_UNDONE'
  [ "$(h Nord2Metal | cut -c1-8)" = e31a9b07 ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = 7fa81fcf ] || die "gpufinal4 undo after"
  run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" | grep -E '^GPUFINAL3_UNDONE'
  [ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = fe59bddb ] || die "gpufinal3 undo after"
  echo "  chain gpufinal6-undo -> gpufinal4-undo -> gpufinal3-undo (${GP#payload-}): ok"
done
# 3b. gpufinal5 installed over the phone's gpufinal4 (and with its -off): chain 6-undo -> 5-undo -> 4-undo -> 3-undo
for GP in payload-gpufinal5 payload-gpufinal5-off; do
  fresh "$GP-over4"; run guest-gpufinal5.sh "$A/$GP" | grep -qE '^GPUFINAL5_INSTALLED' || die "gpufinal5 setup"
  [ "$(h Nord2Metal | cut -c1-8)" = 29abe952 ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = 55de13de ] || die "gpufinal5 setup pair"
  cycle "gpufinal5 via ${GP#payload-} over gpufinal4" "eval refused guest-gpufinal5-undo.sh $A/payload-gpufinal5-undo neither 'gpufinal5-undo while gpufinal6 is installed'"
  python3 -c "import json;rc=json.load(open('$DBD/receipt.json'));assert rc['gpufinal5']['installed']['Nord2Metal'].startswith('29abe952') and rc['gpufinal4']['installed']['Nord2Metal']=='$N4'" || die "gpufinal5/4 records touched"
  if [ "$GP" = payload-gpufinal5-off ]; then [ "$(cat "$DBD/no-pressure")" = made-by-gpufinal5 ] || die "gpufinal5 flag touched"; fi
  run guest-gpufinal5-undo.sh "$A/payload-gpufinal5-undo" | grep -E '^GPUFINAL5_UNDONE'
  [ "$(h Nord2Metal)" = $N4 ] && [ "$(h libredwoodair.dylib)" = $L4 ] || die "gpufinal5 undo after"
  run guest-gpufinal4-undo.sh "$A/payload-gpufinal4-undo" | grep -E '^GPUFINAL4_UNDONE'
  run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" | grep -E '^GPUFINAL3_UNDONE'
  [ "$(h Nord2Metal | cut -c1-8)" = f90a091b ] && [ "$(h libredwoodair.dylib | cut -c1-8)" = fe59bddb ] || die "chain end"
  echo "  chain gpufinal6-undo -> gpufinal5-undo -> gpufinal4-undo -> gpufinal3-undo (${GP#payload-}): ok"
done
# 4. other combinations
for C in "payload-thumbfix4 payload-libmerge5" "payload-usecount1 payload-libmerge6"; do
  set -- $C; fresh "$1-$2"; base; setfile Nord2Metal "$A/$1/Nord2Metal"; setfile libredwoodair.dylib "$A/$2/libredwoodair.dylib"; cycle "${1#payload-} + ${2#payload-}"
done
fresh gpufinal2; base; run guest-gpufinal2.sh "$A/payload-gpufinal2" >/dev/null; [ "$(h Nord2Metal | cut -c1-8)" = be7a746d ] || die "gpufinal2 setup"
cycle "gpufinal2"
# 5. a user's no-pressure / pressure-dryrun: reported and kept by install and undo
fresh userflags; base; echo user > "$DBD/no-rt1"; echo user > "$DBD/pressure-on"
run guest-gpufinal6.sh "$A/payload-gpufinal6" | grep -E '^GPUFINAL6_(SWITCH|NOTE)' || die "user switches not reported"
run guest-gpufinal6.sh "$A/payload-gpufinal6-off" | grep -E '^GPUFINAL6_FLAG no-rt1 present \(not ours, kept\)' || die "-off over a user's no-rt1"
run guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" >/dev/null
[ "$(cat "$DBD/no-rt1")" = user ] && [ "$(cat "$DBD/pressure-on")" = user ] || die "user flag touched"
echo "  a user's no-rt1 / pressure-on reported and kept by install, -off and undo: ok"
# 6. undo fallbacks: over thumbfix5+libmerge6 with both backups lost -> fallbacks; over gpufinal4 / gpufinal3 with a backup lost -> refused
fresh fallback; base; S0=$(st); run guest-gpufinal6.sh "$A/payload-gpufinal6" >/dev/null; rm "$DBD"/*.pre-gpufinal6
run guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" | grep -E '^GPUFINAL6_UNDONE'; [ "$(st)" = "$S0" ] || die "fallback undo not byte-identical"
echo "  undo with both backups missing (fallbacks thumbfix5 + libmerge6): byte-identical: ok"
fresh fallback4; run guest-gpufinal6.sh "$A/payload-gpufinal6" >/dev/null; rm "$DBD/Nord2Metal.pre-gpufinal6"
refused guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" "no usable copy of the previous Nord2Metal" "undo with the gpufinal4 backup lost (no fallback for 015f781d)"
fresh fallback3; gf3; run guest-gpufinal6.sh "$A/payload-gpufinal6" >/dev/null; rm "$DBD/libredwoodair.dylib.pre-gpufinal6"
refused guest-gpufinal6-undo.sh "$A/payload-gpufinal6-undo" "no usable copy of the previous libredwoodair.dylib" "undo with the 7fa81fcf library backup lost (no fallback)"
# 7. refusals before any write
fresh refuse; base
echo foreign > "$F/foreign"; setfile Nord2Metal "$F/foreign"
refused guest-gpufinal6.sh "$A/payload-gpufinal6" "none of the known states" "foreign Nord2Metal"
setfile Nord2Metal "$A/payload-thumbfix5/Nord2Metal"; setfile libredwoodair.dylib "$F/foreign"
refused guest-gpufinal6.sh "$A/payload-gpufinal6" "none of the known states" "foreign library"
setfile libredwoodair.dylib "$A/payload-libmerge6/libredwoodair.dylib"; setfile Nord2Metal "$A/payload-gpufinal6/Nord2Metal"
refused guest-gpufinal6.sh "$A/payload-gpufinal6" "without its record" "y2s96 Nord2Metal without record"
setfile Nord2Metal "$A/payload-thumbfix5/Nord2Metal"; setfile libredwoodair.dylib "$A/payload-gpufinal6/libredwoodair.dylib"
refused guest-gpufinal6.sh "$A/payload-gpufinal6" "without its record" "y2s96 library without record"
setfile libredwoodair.dylib "$A/payload-libmerge6/libredwoodair.dylib"
python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc['gpufinal6']={'previous':{},'installed':{}};open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
refused guest-gpufinal6.sh "$A/payload-gpufinal6" "stale receipt record" "stale gpufinal6 record"
python3 -c "import json;p='$DBD/receipt.json';rc=json.load(open(p));rc.pop('gpufinal6');open(p,'w').write(json.dumps(rc,indent=2)+'\n')"
echo bad > "$R/$BU/libredwoodair.dylib.nord2tmp"
refused guest-gpufinal6.sh "$A/payload-gpufinal6" "differs from its receipt" "stray file in the bundle"
rm "$R/$BU/libredwoodair.dylib.nord2tmp"
for p in payload-gpufinal6 payload-gpufinal6-off payload-gpufinal6-tracecc payload-gpufinal6-undo; do python3 -c "import json;json.load(open('$A/$p/plan.json'))"; done
echo "REHEARSAL_OK gpufinal6 (Nord2Metal fe406e9c + library d9dd236e, one transaction) over gpufinal4 (mirror = phone since cc-txn-gf4, plain and +no-final; chain 6->4->3 undo), gpufinal5 over gpufinal4 (plain and -off; chain 6->5->4->3), gpufinal3 (plain and +no-final, its own undo works after), thumbfix5+libmerge6, thumbfix4+libmerge5, usecount1+libmerge6, gpufinal2; idempotent; -off no-rt1+no-singletarget and -tracecc trace-cc set/cleared; user switches kept; undo byte-identical (backups or fallbacks); refusals change nothing; owned"
