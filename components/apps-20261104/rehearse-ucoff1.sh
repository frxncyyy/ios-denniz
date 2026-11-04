#!/bin/bash
# Rehearsal of guest-ucoff1.sh / -undo.sh (payload-gpufinal3-usecountoff{,-undo}: ONLY /var/db/nord2-metal/usecount-off, marker
# 'made-by-ucoff1') on fake roots = the mirror's bundle + receipt + .pre-gpufinal3 backups (= phone: gpufinal3). unshare -r; nothing touches
# the mirror or a phone. Checks: set / again / undo / again byte-identical and owned (owned-ucoff1.json); over gpufinal4 too; a foreign
# usecount-off kept by set and undo; refused (root unchanged) over thumbfix5, a foreign Nord2Metal, a receipt mismatch.
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
NEWN=015f781d7f9dfa91ef012af8c903c11de0c8a313fd5b9ea895568bd895c5053f; NEWL=7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd
TOP=$B/.rehearse-ucoff1; rm -rf "$TOP"; trap 'rm -rf "$TOP"' EXIT
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
die() { echo "REHEARSAL FAILED: $*"; exit 1; }
h() { sha256sum < "$R/$BU/$1" | cut -c1-64; }
lst() { (cd "$R" && find . -mindepth 1 | sed 's|^\.||' | sort) > "$1"; }
owned() { python3 - "$R" "$A/owned-ucoff1.json" "$1" <<'PY'
import json,os,sys
R,own,before=sys.argv[1:]; own=set(json.load(open(own))); old=set(open(before).read().split('\n'))
now=set()
for b,ds,fs in os.walk(R):
    for n in ds+fs: now.add('/'+os.path.relpath(os.path.join(b,n),R))
bad=[p for p in (now^old) if p and p not in own]
assert not bad, 'written outside owned-ucoff1.json: %r'%bad
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

U=$A/payload-gpufinal3-usecountoff; UU=$A/payload-gpufinal3-usecountoff-undo
ucyc() {
  local s0; s0=$(st); lst "$F/before.lst"
  run guest-ucoff1.sh "$U" | grep -E '^UCOFF1_SET' || die "$1: not set"
  [ "$(cat "$DBD/usecount-off")" = made-by-ucoff1 ] || die "$1: flag content"
  run guest-ucoff1.sh "$U" | grep -qE '^UCOFF1_ALREADY' || die "$1: not idempotent"
  owned "$F/before.lst"
  run guest-ucoff1-undo.sh "$UU" | grep -E '^UCOFF1_UNDONE usecount-off removed' || die "$1: undo"
  run guest-ucoff1-undo.sh "$UU" | grep -qE '^UCOFF1_UNDONE nothing to do' || die "$1: undo not idempotent"
  [ "$(st)" = "$s0" ] && echo "  $1: set+again+undo+again byte-identical, owned: ok" || die "$1 not byte-identical"
}
fresh phone; ucyc "gpufinal3 (= phone)"
run guest-gpufinal4.sh "$A/payload-gpufinal4" >/dev/null; [ "$(h Nord2Metal | cut -c1-8)" = 015f781d ] || die "gpufinal4 setup"; ucyc "gpufinal4"
fresh foreign; echo user > "$DBD/usecount-off"; S0=$(st)
run guest-ucoff1.sh "$U" | grep -E '^UCOFF1_KEPT' || die "foreign flag not reported"
run guest-ucoff1-undo.sh "$UU" | grep -E '^UCOFF1_UNDONE nothing to do' || die "foreign undo"
[ "$(st)" = "$S0" ] && [ "$(cat "$DBD/usecount-off")" = user ] && echo "  a foreign usecount-off is kept by set and undo: ok" || die "foreign flag touched"
fresh old; run guest-gpufinal3-undo.sh "$A/payload-gpufinal3-undo" >/dev/null
refused guest-ucoff1.sh "$U" "neither gpufinal3 nor gpufinal4" "over thumbfix5"
fresh mism; echo x > "$F/x"; cp "$F/x" "$R/$BU/Nord2Metal"
refused guest-ucoff1.sh "$U" "receipt/bundle mismatch" "Nord2Metal differs from its receipt"
for p in "$U" "$UU"; do python3 -c "import json;json.load(open('$p/plan.json'))"; done
echo "REHEARSAL_OK ucoff1 (payload-gpufinal3-usecountoff: only /var/db/nord2-metal/usecount-off 'made-by-ucoff1') over gpufinal3 (mirror = phone) and gpufinal4; idempotent; undo byte-identical; foreign flag kept; refusals change nothing; owned"
