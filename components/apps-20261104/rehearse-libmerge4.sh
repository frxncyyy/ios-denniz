#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-libmerge4.sh / guest-libmerge4-undo.sh (y2s75-libmerge4: libredwoodair.dylib) on a FAKE root (the scripts' own Python with
# /work/root and /src replaced), inside `unshare -r` so chown(0,0) works. The fake root carries the REAL Nord2Metal.bundle entries, receipt.json,
# libmerge3's backup/marker and the gnr1 switch read from the rootfs mirror (read-only, autocycle/hfsx.py). Nothing touches the mirror or a phone.
# Part 1: the mirror state as it is (Nord2Metal thumbcomp, libmerge3 over base): install, idempotent, undo x2 byte-identical, refusals,
#         undo with the backup gone (payload fallback), and libmerge3's own undo/reinstall after ours.
# Part 2: install+undo byte-identical under each accepted Nord2Metal (thumbcomp2, thumbfix2, thumbdiag, thumbfix, vt23).
# Part 3: from the base library and from libmerge2 (under vt23).
# usage: rehearse-libmerge4.sh      (scratch in $N2_REHEARSE_DIR or gpu/build/tmp-y2s75/rehearse-libmerge4, removed at the end)
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-$B/tmp-y2s75/rehearse-libmerge4}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"; BU=System/Library/Extensions/Nord2Metal.bundle; DB=private/var/db/nord2-metal
mkdir -p "$R/$BU" "$R/$DB" "$R/$DB/kept" "$R/$DB/refused"; chmod 1777 "$R/$DB/kept" "$R/$DB/refused"     # both exist on the mirror (contents not needed)
$H get /$DB "$R/$DB" receipt.json libredwoodair.dylib.pre-libmerge3 gnr1-trace.made-by-libmerge3 >/dev/null 2>&1 || true
$H get /private/var/db "$R/private/var/db" nord2-gpu-gnr1-trace >/dev/null 2>&1 || true
[ -f "$R/$DB/receipt.json" ] || { echo "REHEARSAL FAILED: no receipt from the mirror"; exit 1; }
NAMES=$(python3 -c "import json;inv=json.load(open('$R/$DB/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
for d in $(python3 -c "import json;inv=json.load(open('$R/$DB/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
$H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
chmod 755 "$R/$BU/Nord2Metal"
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|" > "$F/$1.py"
        unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f ! -path "./$DB/receipt.json" -exec sha256sum {} + | sort; find . | sort; sha256sum $DB/receipt.json) | sha256sum | cut -c1-16; }
m12() { sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12; }
setnm() { cp "$1" "$R/$BU/Nord2Metal"; chmod 755 "$R/$BU/Nord2Metal"
          python3 -c "import json,hashlib;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['Nord2Metal']=hashlib.sha256(open('$R/$BU/Nord2Metal','rb').read()).hexdigest();open(p,'w').write(json.dumps(r,indent=2)+'\n')"; }
L="$R/$BU/libredwoodair.dylib"; NEW=$(python3 -c "import json;print(json.load(open('$A/payload-libmerge4/plan.json'))['new_sha256']['libredwoodair.dylib'])")
lib() { sha256sum < "$L" | cut -c1-64; }
fail() { echo "REHEARSAL FAILED: $*"; exit 1; }
cp "$R/$BU/Nord2Metal" "$F/nm.mirror"; M0=$(m12)
S0=$(st); echo "start (mirror): $S0 Nord2Metal $(m12) lib $(lib | cut -c1-12) gnr1 switch: $([ -e "$R/private/var/db/nord2-gpu-gnr1-trace" ] && echo yes || echo no)"
echo "== 1. mirror state: install"; run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_(NORD2METAL|FROM|INSTALLED|GNR1)|^CC_'
[ "$(lib)" = "$NEW" ] && echo "  library = y2s75-libmerge4: ok" || fail "not installed"
echo "== again (idempotent)"; SI=$(st); run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_ALREADY'
[ "$(st)" = "$SI" ] && echo "  reinstall changed nothing: ok" || fail "reinstall changed the root"
echo "== undo"; run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE|^CC_RECEIPT'
S1=$(st); [ "$S0" = "$S1" ] && echo "  root byte-identical after install+undo: ok" || fail "$S0 != $S1"
echo "== undo again"; run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
S1u=$(st); [ "$S0" = "$S1u" ] && echo "  root byte-identical after the second undo: ok" || fail "$S0 != $S1u"
echo "== 1a. undo with the backup gone: libmerge3 from the undo payload"
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge4"
run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
[ "$(st)" = "$S0" ] && echo "  byte-identical: ok" || fail "fallback undo differs"
echo "== 1b. a foreign library must stop the install and change nothing"
cp "$L" "$F/l.ours"; cp "$R/$DB/receipt.json" "$F/rc.ours"; echo foreign > "$L"
python3 -c "import json,hashlib;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['libredwoodair.dylib']=hashlib.sha256(open('$L','rb').read()).hexdigest();open(p,'w').write(json.dumps(r,indent=2)+'\\n')"; SF=$(st)     # the receipt records the foreign file too: the library check itself must refuse
if run guest-libmerge4.sh "$A/payload-libmerge4" > "$F/foreign.log" 2>&1; then fail "foreign accepted"; fi
tail -1 "$F/foreign.log" | cut -c1-140; [ "$(st)" = "$SF" ] && echo "  nothing changed: ok" || fail "foreign changed something"
if run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" > "$F/foreign-u.log" 2>&1; then fail "undo accepted a foreign library"; fi
tail -1 "$F/foreign-u.log" | cut -c1-140; [ "$(st)" = "$SF" ] && echo "  undo refused, nothing changed: ok" || fail "undo changed something"
cp "$F/l.ours" "$L"; cp "$F/rc.ours" "$R/$DB/receipt.json"
echo "== 1c. an unknown Nord2Metal must stop the install and change nothing"
cp "$R/$BU/Nord2Metal" "$F/n.foreign"; echo foreign-nm >> "$F/n.foreign"; setnm "$F/n.foreign"; SN=$(st)
if run guest-libmerge4.sh "$A/payload-libmerge4" > "$F/nm.log" 2>&1; then fail "unknown Nord2Metal accepted"; fi
tail -1 "$F/nm.log" | cut -c1-140; [ "$(st)" = "$SN" ] && echo "  nothing changed: ok" || fail "unknown Nord2Metal changed something"
setnm "$F/nm.mirror"; [ "$(st)" = "$S0" ] && echo "  back to the mirror state: ok" || fail "not back to mirror"
echo "== 1d. libmerge3's own undo and reinstall after ours (its backup/record untouched by libmerge4); Nord2Metal thumbcomp here because guest-libmerge3-*-tc.sh do not accept thumbcomp2"
setnm "$A/payload-thumbcomp/Nord2Metal"
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_INSTALLED'; run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
run guest-libmerge3-undo-tc.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'
[ "$(lib | cut -c1-8)" = 6df1af45 ] || fail "libmerge3 undo did not give the base"
SB=$(st); echo "  base (thumbcomp): $SB"
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_(FROM|INSTALLED|GNR1)'
[ "$(lib)" = "$NEW" ] || fail "not installed over base"
run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
[ "$(st)" = "$SB" ] && echo "  over base install+undo byte-identical: ok" || fail "base differs"
echo "== 1e. undo with the backup gone, previous = base: base from the undo payload"
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge4"
run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
[ "$(st)" = "$SB" ] && echo "  byte-identical: ok" || fail "fallback (base) undo differs"
run guest-libmerge3-tc.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_INSTALLED'
echo "  libmerge3 reinstalled: $(st) (mirror $S0; the gnr1 switch/marker were recreated by libmerge3 like on the phone)"
[ "$(lib | cut -c1-8)" = 3c71650a ] || fail "libmerge3 not back"
setnm "$F/nm.mirror"
echo "== 2. under each accepted Nord2Metal (library libmerge3)"
for v in thumbcomp2 thumbfix2 thumbdiag thumbfix vt23; do
    setnm "$A/payload-$v/Nord2Metal"; T=$(st)
    run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_INSTALLED' >/dev/null || fail "$v: install"
    [ "$(lib)" = "$NEW" ] || fail "$v: not installed"
    run guest-libmerge4.sh "$A/payload-libmerge4" | grep -qE '^LIBMERGE4_ALREADY' || fail "$v: idempotent"
    run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -qE '^LIBMERGE4_UNDONE' || fail "$v: undo"
    [ "$(st)" = "$T" ] && echo "  $v ($(m12)): install/again/undo byte-identical: ok" || fail "$v: differs"
done
echo "== 3. from libmerge2 (Nord2Metal vt23)"
run guest-libmerge3-undo-tc.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'; T0=$(st)
run guest-libmerge2.sh "$A/payload-libmerge2" | grep -E '^LIBMERGE2_INSTALLED'; S5=$(st)
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_(FROM|INSTALLED)'
[ "$(lib)" = "$NEW" ] && echo "  library = y2s75-libmerge4 over libmerge2: ok" || fail "not over libmerge2"
run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
[ "$(st)" = "$S5" ] && echo "  back to libmerge2 byte-identical: ok" || fail "libmerge2 differs"
run guest-libmerge2-undo.sh "$A/payload-libmerge2-undo" | grep -E '^LIBMERGE2_UNDONE'
[ "$(st)" = "$T0" ] && echo "  libmerge2 undone after: back to base byte-identical: ok" || fail "base after libmerge2 differs"
echo "REHEARSAL_OK libmerge4: mirror state ($S0, Nord2Metal $M0, libmerge3) install/idempotent/undo x2 byte-identical, backup-gone undo (libmerge3 and base) byte-identical, 3 refusals; libmerge3 undo/reinstall after ours; thumbcomp2/thumbfix2/thumbdiag/thumbfix/vt23, base, libmerge2 byte-identical"
rm -rf "$F"; rmdir "$(dirname "$F")" 2>/dev/null || true
