#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-libmerge6.sh / guest-libmerge6-undo.sh (libmerge6: libredwoodair.dylib fe59bddb) and of guest-ispnoexpand.sh /
# guest-ispnoexpand-undo.sh (/private/var/db/nord2-isp-noexpand) on a FAKE root (the scripts' own Python with /work/root and /src replaced), inside
# `unshare -r` so chown(0,0) works. The fake root carries the REAL Nord2Metal.bundle entries, receipt.json, libmerge3's/libmerge4's/libmerge5's
# backups, the gnr1 switch and markers read from the rootfs mirror (read-only, autocycle/hfsx.py). Nothing touches the mirror or a phone.
# Part 1: the mirror state (Nord2Metal thumbcomp5, libmerge5 over libmerge4 over libmerge3): install, idempotent, undo x2 byte-identical,
#         backup-gone undo (libmerge5 from the payload), refusals (foreign library, unknown Nord2Metal).
# Part 1d: (Nord2Metal thumbcomp2, which the libmerge4/5 guests accept) libmerge5's undo refused while libmerge6 is on top; libmerge5's own undo
#         after ours -> libmerge4; libmerge6 over libmerge4, libmerge3 and (Nord2Metal thumbcomp) base, with backup-gone fallbacks to each;
#         libmerge3 + libmerge4 + libmerge5 reinstalled.
# Part 2: install/again/undo byte-identical under each accepted Nord2Metal (thumbcomp5 ... thumbsync), library libmerge5.
# Part 3: from libmerge2 196e1dcd (under thumbsync, left by part 2).
# Part 4: ispnoexpand: install/idempotent/undo x2 byte-identical, foreign/changed/stray-marker cases, together with libmerge6 and ispfeat.
# usage: rehearse-libmerge6.sh      (scratch in $N2_REHEARSE_DIR or gpu/build/tmp-y2s75/rehearse-libmerge6, removed at the end)
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-$B/tmp-y2s75/rehearse-libmerge6}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"; BU=System/Library/Extensions/Nord2Metal.bundle; DB=private/var/db/nord2-metal
G=private/var/db/nord2-isp-noexpand; MKN=$DB/isp-noexpand.made-by-ispnoexpand; GF=private/var/db/nord2-isp-features
mkdir -p "$R/$BU" "$R/$DB" "$R/$DB/kept" "$R/$DB/refused"; chmod 1777 "$R/$DB/kept" "$R/$DB/refused"     # both exist on the mirror (contents not needed)
$H get /$DB "$R/$DB" receipt.json libredwoodair.dylib.pre-libmerge3 libredwoodair.dylib.pre-libmerge4 libredwoodair.dylib.pre-libmerge5 \
   gnr1-trace.made-by-libmerge3 gnr1-trace.made-by-libmerge4 gnr1-trace.made-by-libmerge5 isp-features.made-by-ispfeat >/dev/null 2>&1 || true
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
L="$R/$BU/libredwoodair.dylib"; NEW=$(python3 -c "import json;print(json.load(open('$A/payload-libmerge6/plan.json'))['new_sha256']['libredwoodair.dylib'])")
lib() { sha256sum < "$L" | cut -c1-64; }
fail() { echo "REHEARSAL FAILED: $*"; exit 1; }
I6="guest-libmerge6.sh $A/payload-libmerge6"; U6="guest-libmerge6-undo.sh $A/payload-libmerge6-undo"
I5="guest-libmerge5.sh $A/payload-libmerge5"; U5="guest-libmerge5-undo.sh $A/payload-libmerge5-undo"
IN="guest-ispnoexpand.sh $A/payload-ispnoexpand"; UN="guest-ispnoexpand-undo.sh $A/payload-ispnoexpand-undo"
cp "$R/$BU/Nord2Metal" "$F/nm.mirror"; M0=$(m12)
S0=$(st); echo "start (mirror): $S0 Nord2Metal $M0 lib $(lib | cut -c1-12) gnr1 switch: $([ -e "$R/private/var/db/nord2-gpu-gnr1-trace" ] && echo yes || echo no) isp switches: ${ISPS:-none}"
[ "$(lib | cut -c1-8)" = 1eae024e ] || echo "  (note: the mirror library is not libmerge5)"
[ "$M0" = ac4e92b787d2 ] || echo "  (note: the mirror Nord2Metal is not thumbcomp5)"
[ -e "$R/$G" ] && fail "the mirror already has $G: the rehearsal expects it absent"
echo "== 1. mirror state: install"; run $I6 | grep -E '^LIBMERGE6_(NORD2METAL|FROM|INSTALLED|GNR1|ISP)|^CC_'
[ "$(lib)" = "$NEW" ] && echo "  library = libmerge6: ok" || fail "not installed"
echo "== again (idempotent)"; SI=$(st); run $I6 | grep -E '^LIBMERGE6_ALREADY'
[ "$(st)" = "$SI" ] && echo "  reinstall changed nothing: ok" || fail "reinstall changed the root"
echo "== undo"; run $U6 | grep -E '^LIBMERGE6_UNDONE|^CC_RECEIPT'
[ "$(st)" = "$S0" ] && echo "  root byte-identical after install+undo: ok" || fail "undo differs"
echo "== undo again"; run $U6 | grep -E '^LIBMERGE6_UNDONE'
[ "$(st)" = "$S0" ] && echo "  root byte-identical after the second undo: ok" || fail "second undo differs"
echo "== 1a. undo with the backup gone: libmerge5 from the undo payload"
run $I6 | grep -E '^LIBMERGE6_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge6"
run $U6 | grep -E '^LIBMERGE6_UNDONE'
[ "$(st)" = "$S0" ] && echo "  byte-identical: ok" || fail "fallback undo differs"
echo "== 1b. a foreign library must stop the install and the undo and change nothing"
cp "$L" "$F/l.ours"; cp "$R/$DB/receipt.json" "$F/rc.ours"; echo foreign > "$L"
python3 -c "import json,hashlib;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['libredwoodair.dylib']=hashlib.sha256(open('$L','rb').read()).hexdigest();open(p,'w').write(json.dumps(r,indent=2)+'\\n')"; SF=$(st)
if run $I6 > "$F/foreign.log" 2>&1; then fail "foreign accepted"; fi
tail -1 "$F/foreign.log" | cut -c1-140; [ "$(st)" = "$SF" ] && echo "  nothing changed: ok" || fail "foreign changed something"
if run $U6 > "$F/foreign-u.log" 2>&1; then fail "undo accepted a foreign library"; fi
tail -1 "$F/foreign-u.log" | cut -c1-140; [ "$(st)" = "$SF" ] && echo "  undo refused, nothing changed: ok" || fail "undo changed something"
cp "$F/l.ours" "$L"; cp "$F/rc.ours" "$R/$DB/receipt.json"; [ "$(st)" = "$S0" ] || fail "not back to mirror"
echo "== 1c. an unknown Nord2Metal must stop the install and change nothing"
cp "$R/$BU/Nord2Metal" "$F/n.foreign"; echo foreign-nm >> "$F/n.foreign"; setnm "$F/n.foreign"; SN=$(st)
if run $I6 > "$F/nm.log" 2>&1; then fail "unknown Nord2Metal accepted"; fi
tail -1 "$F/nm.log" | cut -c1-140; [ "$(st)" = "$SN" ] && echo "  nothing changed: ok" || fail "unknown Nord2Metal changed something"
setnm "$F/nm.mirror"; [ "$(st)" = "$S0" ] && echo "  back to the mirror state: ok" || fail "not back to mirror"
echo "== 1d. (Nord2Metal thumbcomp2) libmerge5's undo while libmerge6 is on top must refuse and change nothing"
setnm "$A/payload-thumbcomp2/Nord2Metal"; SC2=$(st)
run $I6 | grep -E '^LIBMERGE6_INSTALLED'; SI=$(st)
if run $U5 > "$F/lm5u.log" 2>&1; then fail "libmerge5 undo accepted libmerge6"; fi
tail -1 "$F/lm5u.log" | cut -c1-140; [ "$(st)" = "$SI" ] && echo "  nothing changed: ok" || fail "libmerge5 undo changed something"
run $U6 | grep -E '^LIBMERGE6_UNDONE'; [ "$(st)" = "$SC2" ] || fail "thumbcomp2 undo differs"
echo "== 1d. libmerge5's own undo after ours (its backup/record untouched by libmerge6) -> libmerge4"
run $U5 | grep -E '^LIBMERGE5_UNDONE'
[ "$(lib | cut -c1-8)" = 3d2ed15a ] || fail "libmerge5 undo did not give libmerge4"
S4=$(st); echo "  libmerge4: $S4"
run $I6 | grep -E '^LIBMERGE6_(FROM|INSTALLED)'; [ "$(lib)" = "$NEW" ] || fail "not installed over libmerge4"
run $U6 | grep -E '^LIBMERGE6_UNDONE'; [ "$(st)" = "$S4" ] && echo "  over libmerge4 install+undo byte-identical: ok" || fail "libmerge4 differs"
run $I6 | grep -E '^LIBMERGE6_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge6"
run $U6 | grep -E '^LIBMERGE6_UNDONE'; [ "$(st)" = "$S4" ] && echo "  backup gone, libmerge4 from the undo payload: byte-identical: ok" || fail "fallback (libmerge4) differs"
run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
[ "$(lib | cut -c1-8)" = 3c71650a ] || fail "libmerge4 undo did not give libmerge3"
S3=$(st); echo "  libmerge3: $S3"
run $I6 | grep -E '^LIBMERGE6_(FROM|INSTALLED)'; [ "$(lib)" = "$NEW" ] || fail "not installed over libmerge3"
run $U6 | grep -E '^LIBMERGE6_UNDONE'; [ "$(st)" = "$S3" ] && echo "  over libmerge3 install+undo byte-identical: ok" || fail "libmerge3 differs"
run $I6 | grep -E '^LIBMERGE6_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge6"
run $U6 | grep -E '^LIBMERGE6_UNDONE'; [ "$(st)" = "$S3" ] && echo "  backup gone, libmerge3 from the undo payload: byte-identical: ok" || fail "fallback (libmerge3) differs"
echo "== 1e. over base (libmerge3 undone; Nord2Metal thumbcomp because guest-libmerge3-*-tc.sh accept only that)"
setnm "$A/payload-thumbcomp/Nord2Metal"
run guest-libmerge3-undo-tc.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'
[ "$(lib | cut -c1-8)" = 6df1af45 ] || fail "libmerge3 undo did not give the base"
SB=$(st); echo "  base (thumbcomp): $SB"
run $I6 | grep -E '^LIBMERGE6_(FROM|INSTALLED|GNR1)'; [ "$(lib)" = "$NEW" ] || fail "not installed over base"
run $U6 | grep -E '^LIBMERGE6_UNDONE'; [ "$(st)" = "$SB" ] && echo "  over base install+undo byte-identical: ok" || fail "base differs"
run $I6 | grep -E '^LIBMERGE6_INSTALLED'; rm "$R/$DB/libredwoodair.dylib.pre-libmerge6"
run $U6 | grep -E '^LIBMERGE6_UNDONE'; [ "$(st)" = "$SB" ] && echo "  backup gone, base from the undo payload: byte-identical: ok" || fail "fallback (base) differs"
run guest-libmerge3-tc.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_INSTALLED'
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_INSTALLED'
run $I5 | grep -E '^LIBMERGE5_INSTALLED'
setnm "$F/nm.mirror"; [ "$(lib | cut -c1-8)" = 1eae024e ] || fail "libmerge5 not back"
S5=$(st); echo "  libmerge3 + libmerge4 + libmerge5 reinstalled: $S5 (mirror $S0; gnr1 switch/marker as recreated by libmerge3)"
echo "== 2. under each accepted Nord2Metal (library libmerge5)"
for v in thumbcomp5 thumbcomp4 thumbcomp3 thumbcomp2 thumbcomp thumbfix2 thumbdiag thumbfix gpuview thumbstage vt23 thumbsync; do
    setnm "$A/payload-$v/Nord2Metal"; T=$(st)
    run $I6 | grep -qE '^LIBMERGE6_INSTALLED' || fail "$v: install"
    [ "$(lib)" = "$NEW" ] || fail "$v: not installed"
    run $I6 | grep -qE '^LIBMERGE6_ALREADY' || fail "$v: idempotent"
    run $U6 | grep -qE '^LIBMERGE6_UNDONE' || fail "$v: undo"
    [ "$(st)" = "$T" ] && echo "  $v ($(m12)): install/again/undo byte-identical: ok" || fail "$v: differs"
done
echo "== 3. from libmerge2 (Nord2Metal thumbsync, left by part 2)"
run $U5 | grep -E '^LIBMERGE5_UNDONE'
run guest-libmerge4-undo.sh "$A/payload-libmerge4-undo" | grep -E '^LIBMERGE4_UNDONE'
run guest-libmerge3-undo-tc.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'; T0=$(st)
run guest-libmerge2.sh "$A/payload-libmerge2" | grep -E '^LIBMERGE2_INSTALLED'; SL2=$(st)
[ "$(lib | cut -c1-8)" = 196e1dcd ] || fail "libmerge2 not installed"
run $I6 | grep -E '^LIBMERGE6_(FROM|INSTALLED)'; [ "$(lib)" = "$NEW" ] && echo "  library = libmerge6 over libmerge2: ok" || fail "not over libmerge2"
run $I6 | grep -qE '^LIBMERGE6_ALREADY' || fail "libmerge2: idempotent"
run $U6 | grep -E '^LIBMERGE6_UNDONE'; [ "$(st)" = "$SL2" ] && echo "  back to libmerge2 byte-identical: ok" || fail "libmerge2 differs"
run guest-libmerge2-undo.sh "$A/payload-libmerge2-undo" | grep -E '^LIBMERGE2_UNDONE'
[ "$(st)" = "$T0" ] && echo "  libmerge2 undone after: back to base byte-identical: ok" || fail "base after libmerge2 differs"
setnm "$A/payload-thumbcomp/Nord2Metal"; run guest-libmerge3-tc.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_INSTALLED'
run guest-libmerge4.sh "$A/payload-libmerge4" | grep -E '^LIBMERGE4_INSTALLED'; run $I5 | grep -E '^LIBMERGE5_INSTALLED'; setnm "$F/nm.mirror"
[ "$(st)" = "$S5" ] && echo "  back to the 1e state (libmerge5 over libmerge4 over libmerge3, mirror Nord2Metal): ok" || fail "not back to $S5"
echo "== 4. ispnoexpand"
P0=$(st)
run $IN | grep -E '^ISPNOEXPAND_SWITCH'
[ -f "$R/$G" ] && [ "$(cd "$R" && unshare -r stat -c '%a %u:%g %s' $G)" = "644 0:0 0" ] && echo "  switch: empty 644 root:wheel: ok" || fail "switch wrong"
[ "$(cat "$R/$MKN")" = "ispnoexpand created /private/var/db/nord2-isp-noexpand" ] && echo "  marker: ok" || fail "marker wrong"
SI=$(st); run $IN | grep -E '^ISPNOEXPAND_SWITCH already'; [ "$(st)" = "$SI" ] && echo "  idempotent: ok" || fail "reinstall changed the root"
run $UN | grep -E '^ISPNOEXPAND_UNDONE'; [ "$(st)" = "$P0" ] && echo "  undo byte-identical: ok" || fail "undo differs"
run $UN | grep -E '^ISPNOEXPAND_UNDONE'; [ "$(st)" = "$P0" ] && echo "  undo again byte-identical: ok" || fail "second undo differs"
echo "== 4a. a foreign switch: install leaves it, undo leaves it"
: > "$R/$G"; chmod 644 "$R/$G"; SF=$(st)
run $IN | grep -E '^ISPNOEXPAND_SWITCH'; [ "$(st)" = "$SF" ] && echo "  install left it: ok" || fail "install touched the foreign switch"
run $UN | grep -E '^ISPNOEXPAND_UNDONE'; [ "$(st)" = "$SF" ] && echo "  undo left it: ok" || fail "undo removed the foreign switch"
rm "$R/$G"; [ "$(st)" = "$P0" ] || fail "not back"
echo "== 4b. our switch written afterwards (not empty) must stop the undo and change nothing"
run $IN >/dev/null; echo something > "$R/$G"; SC=$(st)
if run $UN > "$F/c.log" 2>&1; then fail "undo removed a changed switch"; fi
tail -1 "$F/c.log" | cut -c1-140; [ "$(st)" = "$SC" ] && echo "  refused, nothing changed: ok" || fail "refused undo changed something"
: > "$R/$G"; run $UN >/dev/null; [ "$(st)" = "$P0" ] && echo "  emptied again, undo byte-identical: ok" || fail "final undo differs"
echo "== 4c. a stray marker without the switch must stop the install and change nothing"
printf 'ispnoexpand created /private/var/db/nord2-isp-noexpand\n' > "$R/$MKN"; SM=$(st)
if run $IN > "$F/sm.log" 2>&1; then fail "install accepted a stray marker"; fi
tail -1 "$F/sm.log" | cut -c1-140; [ "$(st)" = "$SM" ] && echo "  nothing changed: ok" || fail "refused install changed something"
rm "$R/$MKN"; [ "$(st)" = "$P0" ] || fail "not back"
echo "== 4d. together: libmerge6 + ispnoexpand (+ ispfeat-all), undone in either order"
run $I6 | grep -E '^LIBMERGE6_INSTALLED'; run $IN | grep -E '^ISPNOEXPAND_SWITCH'; run $I6 | grep -E '^LIBMERGE6_ISP_NOEXPAND'
run $U6 >/dev/null; run $UN >/dev/null; [ "$(st)" = "$P0" ] || fail "lm6-undo then noexpand-undo differs"
run $IN >/dev/null; run $I6 >/dev/null; run $UN >/dev/null; run $U6 >/dev/null; [ "$(st)" = "$P0" ] || fail "noexpand-undo then lm6-undo differs"
run $I6 >/dev/null; run guest-ispfeat-all.sh "$A/payload-ispfeat-all" >/dev/null; run $IN >/dev/null; run $I6 | grep -E '^LIBMERGE6_ISP_(FEATURES|NOEXPAND)'
[ "$(cat "$R/$GF")" = all ] || fail "ispfeat not there"
run $UN >/dev/null; run guest-ispfeat-undo.sh "$A/payload-ispfeat-undo" >/dev/null; run $U6 >/dev/null
[ "$(st)" = "$P0" ] && echo "  both orders, and with ispfeat-all, byte-identical: ok" || fail "together differs"
echo "REHEARSAL_OK libmerge6: mirror state ($S0, Nord2Metal $M0, lib libmerge5) install/idempotent/undo x2 byte-identical, backup-gone undo (libmerge5, libmerge4, libmerge3, base) byte-identical, refusals (foreign library, unknown Nord2Metal, libmerge5 undo under libmerge6); libmerge5 undo after ours; over libmerge4/libmerge3/base/libmerge2; 12 Nord2Metal incl thumbcomp4, thumbcomp5; ispnoexpand install/idempotent/undo x2 byte-identical, foreign/changed/stray-marker handled, with libmerge6 and ispfeat in either order"
rm -rf "$F"
