#!/bin/bash
# SPDX-License-Identifier: MIT
# Rehearsal of guest-libmerge3.sh / guest-libmerge3-undo.sh (y2s72-libmerge3: libredwoodair.dylib) on a FAKE root (the scripts' own Python with
# /work/root and /src replaced), inside `unshare -r` so chown(0,0) works. The fake root carries the REAL Nord2Metal.bundle entries, receipt.json
# and the Nord2Metal backups read from the rootfs mirror (read-only, autocycle/hfsx.py). Nothing touches the mirror or a phone.
# Part 1: on the mirror state as it is (the phone at 15:28: Nord2Metal y2s73-thumbfix over gpuview, base library).
# Part 2: thumbfix and gpuview undone (Nord2Metal vt23), then from camstats1 and from libmerge2 (the cam-lm2 test state).
# usage: rehearse-libmerge3.sh
set -euo pipefail
A=$(cd "$(dirname "$0")" && pwd); B=$(cd "$A/.." && pwd)
F=${N2_REHEARSE_DIR:-$B/tmp-y2s72/rehearse-libmerge3}; rm -rf "$F"; mkdir -p "$F/root"
R=$F/root; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"; BU=System/Library/Extensions/Nord2Metal.bundle; DB=private/var/db/nord2-metal
mkdir -p "$R/$BU" "$R/$DB"
$H get /$DB "$R/$DB" receipt.json Nord2Metal.pre-thumbfix Nord2Metal.pre-gpuview >/dev/null 2>&1 || true
[ -f "$R/$DB/receipt.json" ] || { echo "REHEARSAL FAILED: no receipt from the mirror"; exit 1; }
NAMES=$(python3 -c "import json;inv=json.load(open('$R/$DB/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v!='directory'))")
for d in $(python3 -c "import json;inv=json.load(open('$R/$DB/receipt.json'))['inventory']['$BU'];print(' '.join(k for k,v in inv.items() if v=='directory'))"); do mkdir -p "$R/$BU/$d"; done
$H get /$BU "$R/$BU" $NAMES >/dev/null 2>&1
chmod 755 "$R/$BU/Nord2Metal"
run() { sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|R='/work/root'|R='$R'|; s|'/src/'|'$2/'|" > "$F/$1.py"
        unshare -r python3 "$F/$1.py"; }
st() { (cd "$R" && find . -type f ! -path "./$DB/receipt.json" -exec sha256sum {} + | sort; sha256sum $DB/receipt.json) | sha256sum | cut -c1-16; }
m12() { sha256sum < "$R/$BU/Nord2Metal" | cut -c1-12; }
L="$R/$BU/libredwoodair.dylib"; NEW=$(python3 -c "import json;print(json.load(open('$A/payload-libmerge3/plan.json'))['new_sha256']['libredwoodair.dylib'])")
lib() { sha256sum < "$L" | cut -c1-64; }
S0=$(st); echo "start (mirror): $S0 Nord2Metal $(m12) lib $(lib | cut -c1-12) Nord2Metal backups: $(ls "$R/$DB" | grep -c Nord2Metal.pre || true)"
echo "== 1. mirror state: install"; run guest-libmerge3.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_(FROM|INSTALLED|GNR1)|^CC_'
[ "$(lib)" = "$NEW" ] && echo "  library = y2s72-libmerge3: ok" || { echo "REHEARSAL FAILED: not installed"; exit 1; }
echo "== again (idempotent)"; run guest-libmerge3.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_ALREADY'
echo "== undo"; run guest-libmerge3-undo.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE|^CC_RECEIPT'
echo "== undo again"; run guest-libmerge3-undo.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'
S1=$(st); [ "$S0" = "$S1" ] && echo "  root byte-identical after install+undo: ok" || { echo "REHEARSAL FAILED: $S0 != $S1"; exit 1; }
echo "== 1b. a foreign library must stop the install and change nothing"
cp "$L" "$F/l.ours"; echo foreign > "$L"
if run guest-libmerge3.sh "$A/payload-libmerge3" > "$F/foreign.log" 2>&1; then echo "REHEARSAL FAILED: foreign accepted"; exit 1; fi
tail -1 "$F/foreign.log" | cut -c1-140; [ "$(cat "$L")" = foreign ] && [ ! -e "$R/$DB/libredwoodair.dylib.pre-libmerge3" ] && echo "  nothing changed: ok" || { echo "REHEARSAL FAILED: foreign changed something"; exit 1; }
cp "$F/l.ours" "$L"
echo "== 1c. an unknown Nord2Metal must stop the install and change nothing"
cp "$R/$BU/Nord2Metal" "$F/n.ours"; echo foreign-nm >> "$R/$BU/Nord2Metal"
python3 -c "import json,hashlib;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['Nord2Metal']=hashlib.sha256(open('$R/$BU/Nord2Metal','rb').read()).hexdigest();open(p,'w').write(json.dumps(r,indent=2)+'\n')"
cp "$R/$DB/receipt.json" "$F/rc.nm"
if run guest-libmerge3.sh "$A/payload-libmerge3" > "$F/nm.log" 2>&1; then echo "REHEARSAL FAILED: unknown Nord2Metal accepted"; exit 1; fi
tail -1 "$F/nm.log" | cut -c1-140; cmp -s "$R/$DB/receipt.json" "$F/rc.nm" && [ ! -e "$R/$DB/libredwoodair.dylib.pre-libmerge3" ] && echo "  nothing changed: ok" || { echo "REHEARSAL FAILED"; exit 1; }
cp "$F/n.ours" "$R/$BU/Nord2Metal"
python3 -c "import json,hashlib;p='$R/$DB/receipt.json';r=json.load(open(p));r['inventory']['$BU']['Nord2Metal']=hashlib.sha256(open('$R/$BU/Nord2Metal','rb').read()).hexdigest();open(p,'w').write(json.dumps(r,indent=2)+'\n')"
S1b=$(st); [ "$S0" = "$S1b" ] && echo "  back to the mirror state: ok" || { echo "REHEARSAL FAILED: $S0 != $S1b"; exit 1; }
echo "== 2. Nord2Metal back to vt23 (thumbfix and gpuview undone on the fake root)"
if [ "$(m12)" = 15f8c23ecfe3 ]; then run guest-thumbfix-undo.sh "$A/payload-thumbfix-undo" | grep -E 'UNDONE|RESTORED' | cut -c1-160; fi
if [ "$(m12)" = d86aede99ec8 ]; then run guest-gpuview-undo.sh "$A/payload-gpuview-undo" | grep -E 'UNDONE|RESTORED' | cut -c1-160; fi
[ "$(m12)" = 59212d387b54 ] || { echo "REHEARSAL FAILED: Nord2Metal is $(m12), not vt23"; exit 1; }
T0=$(st); echo "  vt23: $T0"
run guest-libmerge3.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_INSTALLED'; run guest-libmerge3-undo.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'
[ "$(st)" = "$T0" ] && echo "  over vt23 install+undo byte-identical: ok" || { echo "REHEARSAL FAILED"; exit 1; }
echo "== 2a. from camstats1"; run guest-camstats1.sh "$A/payload-camstats1" | grep -E '^CAMSTATS1_INSTALLED'
S2=$(st)
run guest-libmerge3.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_(FROM|INSTALLED)'
[ "$(lib)" = "$NEW" ] && echo "  library = y2s72-libmerge3 over camstats1: ok" || { echo "REHEARSAL FAILED"; exit 1; }
run guest-libmerge3-undo.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'
S3=$(st); [ "$S2" = "$S3" ] && echo "  back to camstats1 byte-identical: ok" || { echo "REHEARSAL FAILED: $S2 != $S3"; exit 1; }
run guest-camstats1-undo.sh "$A/payload-camstats1-undo" | grep -E '^CAMSTATS1_UNDONE'
[ "$(st)" = "$T0" ] && echo "  camstats1 undone after: back to vt23 byte-identical: ok" || { echo "REHEARSAL FAILED"; exit 1; }
echo "== 2b. from libmerge2 (the state of the cam-lm2 test)"; run guest-libmerge2.sh "$A/payload-libmerge2" | grep -E '^LIBMERGE2_INSTALLED'
S5=$(st)
run guest-libmerge3.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_(FROM|INSTALLED)'
[ "$(lib)" = "$NEW" ] && echo "  library = y2s72-libmerge3 over libmerge2: ok" || { echo "REHEARSAL FAILED"; exit 1; }
run guest-libmerge3.sh "$A/payload-libmerge3" | grep -E '^LIBMERGE3_ALREADY'
run guest-libmerge3-undo.sh "$A/payload-libmerge3-undo" | grep -E '^LIBMERGE3_UNDONE'
S6=$(st); [ "$S5" = "$S6" ] && echo "  back to libmerge2 byte-identical: ok" || { echo "REHEARSAL FAILED: $S5 != $S6"; exit 1; }
run guest-libmerge2-undo.sh "$A/payload-libmerge2-undo" | grep -E '^LIBMERGE2_UNDONE'
[ "$(st)" = "$T0" ] && echo "  libmerge2 undone after: back to vt23 byte-identical: ok" || { echo "REHEARSAL FAILED"; exit 1; }
echo "REHEARSAL_OK libmerge3: mirror state ($S0, Nord2Metal thumbfix) install/idempotent/undo byte-identical + 2 refusals; over vt23, camstats1, libmerge2 byte-identical"
