#!/bin/bash
# Rehearsal of guest-thumbgrid1.sh / -flags.sh (off, safe, on, a refused name) / -undo.sh on a fake root made from the mirror's bundle +
# receipt (= the phone: Nord2Metal thumbfix5 f90a091b, library libmerge6 fe59bddb); unshare -r; nothing touches the mirror or a phone.
# Every guest write is checked against owned-thumbgrid1.json. Scratch under $T (default: a new directory below /tmp/claude-1000, removed).
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd); BU=System/Library/Extensions/Nord2Metal.bundle
H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
T=${T:-$(mktemp -d -p /tmp/claude-1000 rehearse-thumbgrid1.XXXXXX)}; trap 'rm -rf "$T"' EXIT
NM=5752a629cb3df4109138a93ad77aef4b132caf8cfcb44d455454c3cfffb2ccbf; LB=7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/|'$2/|g" > "$F/$1.py"; unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
owned() { python3 - "$R" "$A/owned-thumbgrid1.json" "$F/before.lst" <<'PY'
import json,os,sys
R,own,before=sys.argv[1:]; own=set(json.load(open(own))); old=set(open(before).read().split('\n'))
now=set()
for b,ds,fs in os.walk(R):
    for n in ds+fs: now.add('/'+os.path.relpath(os.path.join(b,n),R))
bad=[p for p in (now^old) if p and p not in own]
assert not bad, 'written outside owned-thumbgrid1.json: %r'%bad
PY
}
fail() { echo "REHEARSAL FAILED: $*"; exit 1; }
F=$T/mirror; mkdir -p "$F/root"; R=$F/root
mkdir -p "$R/$BU" "$R/private/var/db/nord2-metal"
$H get /private/var/db/nord2-metal "$R/private/var/db/nord2-metal" receipt.json >/dev/null 2>&1
NAMES=$(python3 -c "import json;inv=json.load(open('$R/private/var/db/nord2-metal/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
$H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
for d in $(python3 -c "import json;inv=json.load(open('$R/private/var/db/nord2-metal/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
chmod 755 "$R/$BU/Nord2Metal"
[ "$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12)" = f90a091bb7ed ] || fail "mirror Nord2Metal is not thumbfix5"
[ "$(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-12)" = fe59bddbb8e6 ] || fail "mirror library is not libmerge6"
(cd "$R" && find . -mindepth 1 | sed 's|^\.||') > "$F/before.lst"
S0=$(st); echo "== over the mirror: Nord2Metal thumbfix5 f90a091bb7ed, library libmerge6 fe59bddbb8e6"
if run guest-thumbgrid1-flags.sh "$A/payload-thumbgrid1-off" > "$F/o.log" 2>&1; then fail "flags accepted without thumbgrid1"; fi
echo "  flags refused before install: ok"
run guest-thumbgrid1.sh "$A/payload-thumbgrid1" | grep -E '^THUMBGRID1_INSTALLED|^CC_'
run guest-thumbgrid1.sh "$A/payload-thumbgrid1" | grep -E '^THUMBGRID1_ALREADY'
[ "$(sha256sum < "$R/$BU/Nord2Metal" | cut -c1-64)" = $NM ] || fail "wrong Nord2Metal"
[ "$(sha256sum < "$R/$BU/libredwoodair.dylib" | cut -c1-64)" = $LB ] || fail "wrong library"
owned
run guest-thumbgrid1-flags.sh "$A/payload-thumbgrid1-off" | grep -E '^THUMBGRID1_FLAG'
[ "$(cat "$R/private/var/db/nord2-metal/no-thumbgrid")" = "made-by-thumbgrid1" ] || fail "flag"
run guest-thumbgrid1.sh "$A/payload-thumbgrid1" | grep -E '^THUMBGRID1_SWITCH no-thumbgrid present'
run guest-thumbgrid1-flags.sh "$A/payload-thumbgrid1-safe" | grep -E '^THUMBGRID1_FLAG'
[ "$(ls "$R/private/var/db/nord2-metal" | grep -c thumbgrid- )" -ge 4 ] || fail "safe flags"
mkdir -p "$F/bad"; printf '+no-thumbgrid-everything\n' > "$F/bad/flags"
if run guest-thumbgrid1-flags.sh "$F/bad" > "$F/b.log" 2>&1; then fail "unknown switch accepted"; fi
echo "  unknown switch name refused: ok"
echo "someone else" > "$R/private/var/db/nord2-metal/no-thumbgrid-log"
run guest-thumbgrid1-flags.sh "$A/payload-thumbgrid1-on" | grep -E '^THUMBGRID1_FLAG|^CC_JOB'
[ -e "$R/private/var/db/nord2-metal/no-thumbgrid-log" ] || fail "a flag not made by thumbgrid1 was removed"
[ ! -e "$R/private/var/db/nord2-metal/no-thumbgrid" ] || fail "flag not removed"
rm "$R/private/var/db/nord2-metal/no-thumbgrid-log"
echo "  -* removes only its own flags: ok"
run guest-thumbgrid1-flags.sh "$A/payload-thumbgrid1-off" | grep -E '^THUMBGRID1_FLAG'
owned
run guest-thumbgrid1-undo.sh "$A/payload-thumbgrid1-undo" | grep -E '^THUMBGRID1_UNDONE|^CC_RECEIPT'
run guest-thumbgrid1-undo.sh "$A/payload-thumbgrid1-undo" | grep -E '^THUMBGRID1_UNDONE'
S1=$(st); [ "$S0" = "$S1" ] && echo "  byte-identical after install+off+safe+on+off+undo: ok" || fail "not byte-identical after undo"
cp "$A/payload-thumbfix4/Nord2Metal" "$R/$BU/Nord2Metal"
python3 - "$R" <<'PY'
import hashlib,json,sys
R=sys.argv[1]; p=R+'/private/var/db/nord2-metal/receipt.json'; rc=json.load(open(p)); BU='System/Library/Extensions/Nord2Metal.bundle'
rc['inventory'][BU]['Nord2Metal']=hashlib.sha256(open(R+'/'+BU+'/Nord2Metal','rb').read()).hexdigest(); open(p,'w').write(json.dumps(rc,indent=2)+'\n')
PY
if run guest-thumbgrid1.sh "$A/payload-thumbgrid1" > "$F/f.log" 2>&1; then fail "thumbfix4 accepted"; fi
grep -q "not y2s83-thumbfix5" "$F/f.log" || fail "thumbfix4 refused for another reason: $(tail -1 "$F/f.log")"
echo "  other Nord2Metal (thumbfix4) refused: ok"
echo foreign > "$R/$BU/libredwoodair.dylib"; if run guest-thumbgrid1.sh "$A/payload-thumbgrid1" > "$F/f2.log" 2>&1; then fail "foreign library accepted"; fi
echo "  foreign library refused: ok"
for p in payload-thumbgrid1 payload-thumbgrid1-undo payload-thumbgrid1-off payload-thumbgrid1-on payload-thumbgrid1-safe; do
  python3 -c "import json;json.load(open('$A/$p/plan.json'))"
done
echo "REHEARSAL_OK thumbgrid1 over the mirror (thumbfix5 + libmerge6): install, idempotent, switches off/safe/on, unknown switch refused, foreign flag kept, undo byte-identical, other Nord2Metal / foreign library refused, owned"
