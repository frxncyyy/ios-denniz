#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-libmerge5.sh / guest-libmerge5-undo.sh (libmerge5: libredwoodair.dylib 1eae024e) and of guest-ispfeat-{fold,r32,mrt,all}.sh /
# guest-ispfeat-undo.sh (/private/var/db/nord2-isp-features) on a FAKE root (the scripts' own Python with /work/root and /src replaced), inside
# `unshare -r` so chown(0,0) works. The fake root carries the REAL Nord2Metal.bundle entries, receipt.json, libmerge3's/libmerge4's backups, the
# gnr1 switch and marker read from the rootfs mirror (read-only, autocycle/hfsx.py). Nothing touches the mirror or a phone.
# Part 1: the mirror state (Nord2Metal thumbcomp2, libmerge4 over libmerge3): install, idempotent, undo x2 byte-identical, backup-gone undo,
#         refusals (foreign library, unknown Nord2Metal, libmerge4's undo while libmerge5 is on top).
# Part 1d: libmerge4's own undo after ours -> libmerge3; libmerge5 over libmerge3 and (libmerge3 undone, Nord2Metal thumbcomp) over base,
#         backup-gone fallbacks to libmerge3 and base; libmerge3 + libmerge4 reinstalled.
# Part 2: install/again/undo byte-identical under each accepted Nord2Metal (thumbcomp3 ... thumbsync).
# Part 3: from libmerge2 (under thumbsync, left by part 2).
# Part 4: ispfeat: each variant, swaps between variants, undo x2 byte-identical, foreign/changed/stray-marker cases, together with libmerge5.
# usage: rehearse-libmerge5.sh      (scratch in $N2_REHEARSE_DIR or gpu/build/tmp-y2s75/rehearse-libmerge5, removed at the end)
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-$B/tmp-y2s75/rehearse-libmerge5}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"; BU=System/Library/Extensions/Nord2Metal.bundle; DB=private/var/db/nord2-metal
G=private/var/db/nord2-isp-features; MKI=$DB/isp-features.made-by-ispfeat
mkdir -p "$R/$BU" "$R/$DB" "$R/$DB/kept" "$R/$DB/refused"; chmod 1777 "$R/$DB/kept" "$R/$DB/refused"     # both exist on the mirror (contents not needed)
$H get /$DB "$R/$DB" receipt.json libredwoodair.dylib.pre-libmerge3 libredwoodair.dylib.pre-libmerge4 gnr1-trace.made-by-libmerge3 gnr1-trace.made-by-libmerge4 >/dev/null 2>&1 || true
ISPS=$($H ls /private/var/db 2>/dev/null | awk '{print $NF}' | grep '^nord2-isp' | tr '\n' ' ' || true)
$H get /private/var/db "$R/private/var/db" nord2-gpu-gnr1-trace $ISPS >/dev/null 2>&1 || true
[ -f "$R/$DB/receipt.json" ] || { echo "REHEARSAL FAILED: no receipt from the mirror"; exit 1; }
NAMES=$(python3 -c "import json;inv=json.load(open('$R/$DB/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
for d in $(python3 -c "import json;inv=json.load(open('$R/$DB/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
$H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
chmod 755 "$R/$BU/Nord2Metal"
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/|'${2:-/nonexistent}/|" > "$F/$1.py"
        unshare -r python3 "$F/$1.py"; }
# state: every path with type, mode, owner (as root in the namespace), size and content hash
st() { (cd "$R" && unshare -r find . -printf '%p %y %m %U:%G %s\n' | sort; find . -type f -exec sha256sum {} + | sort) | sha256sum | cut -c1-16; }
m12() { sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12; }
setnm() { cp "$1" "$R/$BU/Nord2Metal"; chmod 755 "$R/$BU/Nord2Metal"
          python3 -c "import json,hashlib;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['Nord2Metal']=hashlib.sha256(open('$R/$BU/Nord2Metal','rb').read()).hexdigest();open(p,'w').write(json.dumps(r,indent=2)+'\n')"; }
L="$R/$BU/libredwoodair.dylib"; NEW=$(python3 -c "import json;print(json.load(open('$A/payload-libmerge5/plan.json'))['new_sha256']['libredwoodair.dylib'])")
lib() { sha256sum < "$L" | cut -c1-64; }
fail() { echo "REHEARSAL FAILED: $*"; exit 1; }
I5="guest-libmerge5.sh $A/payload-libmerge5"; U5="guest-libmerge5-undo.sh $A/payload-libmerge5-undo"
cp "$R/$BU/Nord2Metal" "$F/nm.mirror"; M0=$(m12)
S0=$(st); echo "start (mirror): $S0 Nord2Metal $M0 lib $(lib | cut -c1-12) gnr1 switch: $([ -e "$R/private/var/db/nord2-gpu-gnr1-trace" ] && echo yes || echo no) isp switches: ${ISPS:-none}"
[ "$(lib | cut -c1-8)" = 3d2ed15a ] || echo "  (note: the mirror library is not libmerge4)"
[ -e "$R/$G" ] && fail "the mirror already has $G: the rehearsal expects it absent"
echo "== 1. mirror state: install"; run $I5 | grep -E '^LIBMERGE5_(NORD2METAL|FROM|INSTALLED|GNR1|ISP)|^CC_'
[ "$(lib)" = "$NEW" ] && echo "  library = libmerge5: ok" || fail "not installed"
echo "== again (idempotent)"; SI=$(st); run $I5 | grep -E '^LIBMERGE5_ALREADY'
[ "$(st)" = "$SI" ] && echo "  reinstall changed nothing: ok" || fail "reinstall changed the root"
echo "== libmerge4's undo while libmerge5 is on top must refuse and change nothing"
if run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" > "$F/lm4u.log" 2>&1; then fail "libmerge4 undo accepted libmerge5"; fi
tail -1 "$F/lm4u.log" | cut -c1-140; [ "$(st)" = "$SI" ] && echo "  nothing changed: ok" || fail "libmerge4 undo changed something"
echo "== undo"; run $U5 | grep -E '^LIBMERGE5_UNDONE|^CC_RECEIPT'
[ "$(st)" = "$S0" ] && echo "  root byte-identical after install+undo: ok" || fail "undo differs"
echo "== undo again"; run $U5 | grep -E '^LIBMERGE5_UNDONE'
[ "$(st)" = "$S0" ] && echo "  root byte-identical after the second undo: ok" || fail "second undo differs"
echo "== 1a. undo with the backup gone: libmerge4 from the undo payload"
run $I5 | grep -E '^LIBMERGE5_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge5"
run $U5 | grep -E '^LIBMERGE5_UNDONE'
[ "$(st)" = "$S0" ] && echo "  byte-identical: ok" || fail "fallback undo differs"
echo "== 1b. a foreign library must stop the install and the undo and change nothing"
cp "$L" "$F/l.ours"; cp "$R/$DB/receipt.json" "$F/rc.ours"; echo foreign > "$L"
python3 -c "import json,hashlib;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['libredwoodair.dylib']=hashlib.sha256(open('$L','rb').read()).hexdigest();open(p,'w').write(json.dumps(r,indent=2)+'\\n')"; SF=$(st)
if run $I5 > "$F/foreign.log" 2>&1; then fail "foreign accepted"; fi
tail -1 "$F/foreign.log" | cut -c1-140; [ "$(st)" = "$SF" ] && echo "  nothing changed: ok" || fail "foreign changed something"
if run $U5 > "$F/foreign-u.log" 2>&1; then fail "undo accepted a foreign library"; fi
tail -1 "$F/foreign-u.log" | cut -c1-140; [ "$(st)" = "$SF" ] && echo "  undo refused, nothing changed: ok" || fail "undo changed something"
cp "$F/l.ours" "$L"; cp "$F/rc.ours" "$R/$DB/receipt.json"; [ "$(st)" = "$S0" ] || fail "not back to mirror"
echo "== 1c. an unknown Nord2Metal must stop the install and change nothing"
cp "$R/$BU/Nord2Metal" "$F/n.foreign"; echo foreign-nm >> "$F/n.foreign"; setnm "$F/n.foreign"; SN=$(st)
if run $I5 > "$F/nm.log" 2>&1; then fail "unknown Nord2Metal accepted"; fi
tail -1 "$F/nm.log" | cut -c1-140; [ "$(st)" = "$SN" ] && echo "  nothing changed: ok" || fail "unknown Nord2Metal changed something"
setnm "$F/nm.mirror"; [ "$(st)" = "$S0" ] && echo "  back to the mirror state: ok" || fail "not back to mirror"
echo "== 1d. libmerge4's own undo after ours (its backup/record untouched by libmerge5) -> libmerge3"
run $I5 | grep -E '^LIBMERGE5_INSTALLED'; run $U5 | grep -E '^LIBMERGE5_UNDONE'
run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
[ "$(lib | cut -c1-8)" = 3c71650a ] || fail "libmerge4 undo did not give libmerge3"
S3=$(st); echo "  libmerge3: $S3"
run $I5 | grep -E '^LIBMERGE5_(FROM|INSTALLED)'; [ "$(lib)" = "$NEW" ] || fail "not installed over libmerge3"
run $U5 | grep -E '^LIBMERGE5_UNDONE'; [ "$(st)" = "$S3" ] && echo "  over libmerge3 install+undo byte-identical: ok" || fail "libmerge3 differs"
run $I5 | grep -E '^LIBMERGE5_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge5"
run $U5 | grep -E '^LIBMERGE5_UNDONE'; [ "$(st)" = "$S3" ] && echo "  backup gone, libmerge3 from the undo payload: byte-identical: ok" || fail "fallback (libmerge3) differs"
echo "== 1e. over base (libmerge3 undone; Nord2Metal thumbcomp because guest-libmerge3-*-tc.sh do not accept thumbcomp2)"
setnm "$A/payload-thumbcomp/Nord2Metal"
run guest-libmerge3-undo-tc.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'
[ "$(lib | cut -c1-8)" = 6df1af45 ] || fail "libmerge3 undo did not give the base"
SB=$(st); echo "  base (thumbcomp): $SB"
run $I5 | grep -E '^LIBMERGE5_(FROM|INSTALLED|GNR1)'; [ "$(lib)" = "$NEW" ] || fail "not installed over base"
run $U5 | grep -E '^LIBMERGE5_UNDONE'; [ "$(st)" = "$SB" ] && echo "  over base install+undo byte-identical: ok" || fail "base differs"
run $I5 | grep -E '^LIBMERGE5_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge5"
run $U5 | grep -E '^LIBMERGE5_UNDONE'; [ "$(st)" = "$SB" ] && echo "  backup gone, base from the undo payload: byte-identical: ok" || fail "fallback (base) differs"
run guest-libmerge3-tc.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_INSTALLED'
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_INSTALLED'
setnm "$F/nm.mirror"; [ "$(lib | cut -c1-8)" = 3d2ed15a ] || fail "libmerge4 not back"
S4=$(st); echo "  libmerge3 + libmerge4 reinstalled: $S4 (mirror $S0; gnr1 switch/marker recreated by libmerge3 as on the phone)"
echo "== 2. under each accepted Nord2Metal (library libmerge4)"
for v in thumbcomp3 thumbcomp2 thumbcomp thumbfix2 thumbdiag thumbfix gpuview thumbstage vt23 thumbsync; do
    setnm "$A/payload-$v/Nord2Metal"; T=$(st)
    run $I5 | grep -qE '^LIBMERGE5_INSTALLED' || fail "$v: install"
    [ "$(lib)" = "$NEW" ] || fail "$v: not installed"
    run $I5 | grep -qE '^LIBMERGE5_ALREADY' || fail "$v: idempotent"
    run $U5 | grep -qE '^LIBMERGE5_UNDONE' || fail "$v: undo"
    [ "$(st)" = "$T" ] && echo "  $v ($(m12)): install/again/undo byte-identical: ok" || fail "$v: differs"
done
echo "== 3. from libmerge2 (Nord2Metal thumbsync, left by part 2)"
run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
run guest-libmerge3-undo-tc.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'; T0=$(st)
run guest-libmerge2.sh "$A/payload-libmerge2" | grep -E '^LIBMERGE2_INSTALLED'; S5=$(st)
run $I5 | grep -E '^LIBMERGE5_(FROM|INSTALLED)'; [ "$(lib)" = "$NEW" ] && echo "  library = libmerge5 over libmerge2: ok" || fail "not over libmerge2"
run $U5 | grep -E '^LIBMERGE5_UNDONE'; [ "$(st)" = "$S5" ] && echo "  back to libmerge2 byte-identical: ok" || fail "libmerge2 differs"
run guest-libmerge2-undo.sh "$A/payload-libmerge2-undo" | grep -E '^LIBMERGE2_UNDONE'
[ "$(st)" = "$T0" ] && echo "  libmerge2 undone after: back to base byte-identical: ok" || fail "base after libmerge2 differs"
setnm "$A/payload-thumbcomp/Nord2Metal"; run guest-libmerge3-tc.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_INSTALLED'
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_INSTALLED'; setnm "$F/nm.mirror"
[ "$(st)" = "$S4" ] && echo "  back to the 1e state (libmerge4 over libmerge3, thumbcomp2): ok" || fail "not back to $S4"
echo "== 4. ispfeat"
P0=$(st)
for v in fold r32 mrt all; do
    run guest-ispfeat-$v.sh "$A/payload-ispfeat-$v" | grep -E '^ISPFEAT_SWITCH'
    [ "$(cat "$R/$G")" = "$v" ] && [ "$(cd "$R" && unshare -r stat -c '%a %u:%g %s' $G)" = "644 0:0 $((${#v}+1))" ] && echo "  $v: '$v\\n' 644 root:wheel: ok" || fail "$v switch wrong"
    SI=$(st); run guest-ispfeat-$v.sh "$A/payload-ispfeat-$v" | grep -qE '^ISPFEAT_SWITCH already' || fail "$v idempotent"; [ "$(st)" = "$SI" ] || fail "$v reinstall changed the root"
done
echo "  fold -> r32 -> mrt -> all swaps, each idempotent: ok"
run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" | grep -E '^ISPFEAT_UNDONE'; [ "$(st)" = "$P0" ] && echo "  undo byte-identical: ok" || fail "ispfeat undo differs"
run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" | grep -E '^ISPFEAT_UNDONE'; [ "$(st)" = "$P0" ] && echo "  undo again byte-identical: ok" || fail "second ispfeat undo differs"
for v in fold r32 mrt all; do run guest-ispfeat-$v.sh "$A/payload-ispfeat-$v" >/dev/null; run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" >/dev/null; [ "$(st)" = "$P0" ] || fail "$v alone: undo differs"; done
echo "  each variant alone install+undo byte-identical: ok"
echo "== 4a. wrong payload for the guest must refuse"
if run guest-ispfeat-fold.sh "$A/payload-ispfeat-r32" > "$F/wp.log" 2>&1; then fail "fold guest took the r32 payload"; fi
tail -1 "$F/wp.log" | cut -c1-140; [ "$(st)" = "$P0" ] && echo "  nothing changed: ok" || fail "wrong payload changed something"
echo "== 4b. a foreign switch: same text left alone, different text refused, undo leaves it"
printf 'mrt\n' > "$R/$G"; chmod 644 "$R/$G"; SF=$(st)
run guest-ispfeat-mrt.sh "$A/payload-ispfeat-mrt" | grep -E '^ISPFEAT_SWITCH'; [ "$(st)" = "$SF" ] && echo "  same text: left as is: ok" || fail "touched the foreign switch"
if run guest-ispfeat-fold.sh "$A/payload-ispfeat-fold" > "$F/ff.log" 2>&1; then fail "foreign different switch accepted"; fi
tail -1 "$F/ff.log" | cut -c1-140; [ "$(st)" = "$SF" ] && echo "  different text refused, nothing changed: ok" || fail "refused install changed something"
run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" | grep -E '^ISPFEAT_UNDONE'; [ "$(st)" = "$SF" ] && echo "  undo left it: ok" || fail "undo removed the foreign switch"
rm "$R/$G"; [ "$(st)" = "$P0" ] || fail "not back"
echo "== 4c. our switch changed afterwards must stop install and undo and change nothing"
run guest-ispfeat-fold.sh "$A/payload-ispfeat-fold" >/dev/null; printf 'fold r32\n' > "$R/$G"; SC=$(st)
if run guest-ispfeat-r32.sh "$A/payload-ispfeat-r32" > "$F/c1.log" 2>&1; then fail "install over a changed switch"; fi
tail -1 "$F/c1.log" | cut -c1-140; [ "$(st)" = "$SC" ] || fail "refused install changed something"
if run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" > "$F/c2.log" 2>&1; then fail "undo removed a changed switch"; fi
tail -1 "$F/c2.log" | cut -c1-140; [ "$(st)" = "$SC" ] && echo "  both refused, nothing changed: ok" || fail "refused undo changed something"
printf 'fold\n' > "$R/$G"; run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" >/dev/null; [ "$(st)" = "$P0" ] && echo "  restored text, undo byte-identical: ok" || fail "final undo differs"
echo "== 4d. a stray marker without the switch must stop the install and change nothing"
printf 'ispfeat created /private/var/db/nord2-isp-features with:\nfold\n' > "$R/$MKI"; SM=$(st)
if run guest-ispfeat-fold.sh "$A/payload-ispfeat-fold" > "$F/sm.log" 2>&1; then fail "install accepted a stray marker"; fi
tail -1 "$F/sm.log" | cut -c1-140; [ "$(st)" = "$SM" ] && echo "  nothing changed: ok" || fail "refused install changed something"
rm "$R/$MKI"; [ "$(st)" = "$P0" ] || fail "not back"
echo "== 4e. together: libmerge5 then ispfeat-all, then both undone in either order"
run $I5 | grep -E '^LIBMERGE5_INSTALLED'; run guest-ispfeat-all.sh "$A/payload-ispfeat-all" | grep -E '^ISPFEAT_SWITCH'
run $I5 | grep -E '^LIBMERGE5_ISP_FEATURES'
run $U5 >/dev/null; run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" >/dev/null; [ "$(st)" = "$P0" ] || fail "lm5-undo then ispfeat-undo differs"
run guest-ispfeat-r32.sh "$A/payload-ispfeat-r32" >/dev/null; run $I5 >/dev/null; run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" >/dev/null; run $U5 >/dev/null
[ "$(st)" = "$P0" ] && echo "  both orders byte-identical: ok" || fail "ispfeat-undo then lm5-undo differs"
echo "REHEARSAL_OK libmerge5: mirror state ($S0, Nord2Metal $M0, libmerge4) install/idempotent/undo x2 byte-identical, backup-gone undo (libmerge4, libmerge3, base) byte-identical, refusals (foreign library, unknown Nord2Metal, libmerge4 undo under libmerge5); libmerge4 undo after ours; over libmerge3/base/libmerge2; 10 Nord2Metal incl thumbcomp3; ispfeat fold/r32/mrt/all install/swap/idempotent/undo x2 byte-identical, foreign/changed/stray-marker/wrong-payload handled"
rm -rf "$F"
