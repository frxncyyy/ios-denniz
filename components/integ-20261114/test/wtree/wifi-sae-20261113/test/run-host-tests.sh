#!/bin/bash
# 0432..0434 (q10jgy) host tests. usage: run-host-tests.sh LOG_DIR      (LOG_DIR must not exist)
# Sources under test: ../patched-sources-0432..0434 over wifi-20261113/patched-sources-0430 and the q10jgw2 stage (CI sources).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); W=$(dirname "$H"); B=$(dirname "$W"); L=$(realpath -m "$1"); [ ! -e "$L" ] || { echo "$L exists"; exit 1; }; mkdir -p "$L"
R=$H; while [ ! -d "$R/tools/fixtures/nord2-wifi-handover" ] || [ ! -d "$R/research/nord2/wifi/calibration" ]; do R=$(dirname "$R"); [ "$R" != / ] || { echo "repo root not found"; exit 1; }; done
BS=$R/research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl; S=$B/device-mmu-q10jgw2-candidate/k/kernel/stage/bsd/dev
T=$L/src; mkdir -p "$T"; cp "$S"/wifi_*.c "$S"/wifi_*.h "$S"/*.h "$T"/ 2>/dev/null || true; for d in "$B"/wifi-20261113/patched-sources-0430 "$W"/patched-sources-043*; do cp "$d"/bsd/dev/* "$T"/; done
CF="-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -DNORD2 -I$H/include -I$T -I$BS/inc -I$BS/src"
BSRC="$BS/src/hash/sha1.c $BS/src/symcipher/aes_ct.c $BS/src/symcipher/aes_ct_enc.c $BS/src/symcipher/aes_ct_dec.c $BS/src/symcipher/aes_ct_cbcenc.c $BS/src/symcipher/aes_ct_cbcdec.c $(echo $BS/src/codec/*.c)"
# 1. known-answer tests: IEEE 802.11-2020 Annex J.10 (SAE group 19, both password element methods), RFC 4231, RFC 4493
clang $CF "$H/sae/kat.c" "$T/wifi_sae.c" "$T/wifi_sae_crypto.c" $BSRC -o "$L/kat" 2> "$L/kat-compile.log" || { cat "$L/kat-compile.log"; exit 1; }
"$L/kat" | tee "$L/kat.log" | tail -1
# 2. the independent reference (Python integers, hashlib, cryptography) reproduces the same Annex J.10 vectors
python3 "$H/stc/ref.py"
# 3. real station transaction (CI wifi_wpa2.c + new wifi_wpa3.c/wifi_sae.c) against the reference access point
clang $CF "$H/stc/harness.c" "$T/wifi_station_transaction.c" "$T/wifi_wpa3.c" "$T/wifi_sae.c" "$T/wifi_sae_crypto.c" "$T/wifi_station_wire.c" "$T/wifi_station_wire2.c" "$T/wifi_wpa2.c" "$T/wifi_wpa_crypto.c" "$T/wifi_wpa_diag.c" $BSRC -o "$L/stc-harness" 2> "$L/stc-compile.log" || { cat "$L/stc-compile.log"; exit 1; }
python3 "$H/stc/run.py" "$L/stc-harness" | tee "$L/stc.log" | tail -1
# 4. join decoder, secret store, BSS selection, negotiation, capability bits
clang $CF "$H/join/test.c" "$T/wifi_ios_join.c" "$T/wifi_ios_card.c" "$T/wifi_ios_control.c" "$T/wifi_wpa3.c" "$T/wifi_sae_crypto.c" "$T/wifi_wpa_crypto.c" $BSRC -o "$L/join" 2> "$L/join-compile.log" || { cat "$L/join-compile.log"; exit 1; }
"$L/join" > "$L/join.log" 2>&1 || { tail -3 "$L/join.log"; exit 1; }; grep PASS "$L/join.log"
# 5. regression: the q10jgw2 suites (0430 decoder test, real owner loop with the unmodified binders) on the candidates
python3 "$H/mk-candidate.py" "$L/cand" > /dev/null; python3 "$H/mk-candidate.py" "$L/cand-sae0" --sae0 > /dev/null; python3 "$H/mk-candidate.py" "$L/cand-legacy" --legacy > /dev/null
# (The 0430 decoder test of wifi-20261113 moved on to the 0436 chain (niwj_take_self) at 16:28; its q10jgw2 version passed on the
#  n2wifi_sae=0 candidate before that: decoder-0430v2-on-sae0-candidate-1555.log. The mode-0 decoder cases are in join/test.c.)
for c in cand cand-sae0; do
 ( cd "$R" && python3 tools/test-nord2-wifi-handover.py --candidate "$L/$c" --board "$R/research/nord2/wifi/calibration/WIFI" --log-dir "$L/$c-handover/log" | tail -1 | cut -c1-150 )
 N=$(grep -a -c 'NORD2 WIFI IOS SCAN: .*status=0 .*bss=[1-9][0-9]* done=1 ' "$L/$c-handover/log/owning/test-native.log"); X=$(grep -a -c 'NORD2 WIFI IOS SCAN: .*status=3 ' "$L/$c-handover/log/owning/test-native.log" || true)
 [ "$N" -ge 2 ] && [ "$X" = 0 ] || { echo "PLAIN SCAN CHECK FAILED ($c): complete=$N refused=$X"; exit 1; }; echo "PASS $c plain passive scans: $N complete with BSS, 0 refused"
 ( cd "$R" && python3 tools/test-nord2-wifi-lifecycle.py --candidate "$L/$c" --log-dir "$L/$c-lifecycle/log" | tail -1 | cut -c1-150 )
 ( cd "$R" && python3 tools/test-nord2-wifi-open.py --candidate "$L/$c" --output "$L/$c-open-codec" --only codec | tail -1 )
 ( cd "$R" && python3 tools/test-nord2-wifi-open.py --candidate "$L/$c" --output "$L/$c-open-legacy" --only legacy-codec | tail -1 )
done
echo HOST_TESTS_OK
