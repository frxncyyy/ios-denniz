#!/bin/bash
# 0450/0451 host tests. usage: run-host-tests.sh LOG_DIR
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); L=$(realpath -m "$1"); mkdir -p "$L"; V2=$(dirname "$(dirname "$H")")/wifi-20261113/test
R=$H; while [ ! -d "$R/tools/fixtures/nord2-wifi-handover" ] || [ ! -d "$R/research/nord2/wifi/calibration" ]; do R=$(dirname "$R"); [ "$R" != / ] || { echo "repo root not found"; exit 1; }; done
BS=$R/research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl
CC="clang -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -fno-sanitize-recover=all"
C=$L/cand; rm -rf "$C" "$L/cand-off"; python3 "$H/mk-candidate.py" "$C" > /dev/null; python3 "$H/mk-candidate.py" "$L/cand-off" --off > /dev/null
T=$(mktemp -d "$L/unit.XXXXXX"); cp "$R"/shim/nord2-xnu/*.h "$R"/shim/nord2-xnu/wifi_wpa_crypto.c "$T"/; cp "$C"/* "$T"/
$CC -O2 -I"$T" "$H/crypto/test.c" -lcrypto -o "$T/crypto" && "$T/crypto"
$CC -I"$T" "$H/decoder/test.c" "$T/wifi_ios_join.c" -o "$T/decoder" && "$T/decoder" | grep PASS
$CC -I"$T" -I"$H/engine" -I"$BS/inc" -I"$BS/src" "$H/engine/test.c" "$T/wifi_8021x_wpa2.c" "$T/wifi_wpa_crypto.c" "$BS"/src/hash/sha1.c "$BS"/src/codec/dec32be.c "$BS"/src/codec/enc32be.c "$BS"/src/symcipher/aes_ct.c "$BS"/src/symcipher/aes_ct_dec.c "$BS"/src/symcipher/aes_ct_cbcdec.c -lcrypto -o "$T/engine" && "$T/engine"
for v in "1 1" "5 1" "1 2" "5 2"; do set -- $v; python3 "$H/eap/make.py" --candidate "$C" --log-dir "$L/eap-akm$1-case$2" --akm $1 --case $2 | tail -1 | cut -c1-420; done
python3 "$H/eap/make.py" --candidate "$C" --log-dir "$L/psk-through-wrappers" --psk | tail -1 | cut -c1-200
[ "$(cat "$L"/psk-through-wrappers/owning/*.log | grep -a -c 'NORD2 WIFI 8021X')" = 0 ] && echo "PASS the fixture's two WPA2-PSK joins through the nwn_* wrappers: no 802.1X activity"
# repo suites (unmodified runners) on both candidates: kern.n2wifi_8021x=0 and the default. (wifi-20261113/test now belongs to a
# later decoder of the Wi-Fi agent, 0436, and is not run here; its q10jgw2 version passed on the q10jhe candidate, host-tests-20261113.log.)
for c in cand-off cand; do
 ( cd "$R" && python3 tools/test-nord2-wifi-handover.py --candidate "$L/$c" --board "$R/research/nord2/wifi/calibration/WIFI" --log-dir "$L/repo-$c/handover/log" | tail -1 | cut -c1-160 )
 N=$(grep -a -c 'NORD2 WIFI IOS SCAN: .*status=0 .*bss=[1-9][0-9]* done=1 ' "$L/repo-$c/handover/log/owning/test-native.log"); X=$(grep -a -c 'NORD2 WIFI IOS SCAN: .*status=3 ' "$L/repo-$c/handover/log/owning/test-native.log" || true)
 [ "$N" -ge 2 ] && [ "$X" = 0 ] || { echo "PLAIN SCAN CHECK FAILED: complete=$N refused=$X"; exit 1; }; echo "PASS plain passive scans ($c): $N complete with BSS, 0 refused"
 ( cd "$R" && python3 tools/test-nord2-wifi-lifecycle.py --candidate "$L/$c" --log-dir "$L/repo-$c/lifecycle/log" | tail -1 | cut -c1-160 )
 ( cd "$R" && python3 tools/test-nord2-wifi-open.py --candidate "$L/$c" --output "$L/repo-$c/open-codec" --only codec | tail -1 )
 ( cd "$R" && python3 tools/test-nord2-wifi-open.py --candidate "$L/$c" --output "$L/repo-$c/open-legacy" --only legacy-codec | tail -1 )
done
echo HOST_TESTS_8021X_OK
