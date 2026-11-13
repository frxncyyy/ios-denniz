#!/bin/bash
# 0430 + 0436 host tests. usage: run-host-tests.sh CANDIDATE_DIR LOG_DIR   (CANDIDATE from mk-candidate.py)
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); C=$(realpath "$1"); L=$(realpath -m "$2"); mkdir -p "$L"
R=$H; while [ ! -d "$R/tools/fixtures/nord2-wifi-handover" ] || [ ! -d "$R/research/nord2/wifi/calibration" ]; do R=$(dirname "$R"); [ "$R" != / ] || { echo "repo root not found"; exit 1; }; done
T=$(mktemp -d "$L/dec.XXXXXX"); cp "$R"/shim/nord2-xnu/*.h "$T"/; cp "$R"/shim/nord2-xnu/wifi_ios_rates.c "$R"/shim/nord2-xnu/wifi_ios_mapping.c "$T"/; cp "$C"/* "$T"/
clang -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -fno-sanitize-recover=all -I"$T" "$H/decoder/test.c" "$T/wifi_ios_join.c" "$T/wifi_ios_policy.c" "$T/wifi_ios_rates.c" "$T/wifi_ios_mapping.c" -o "$T/test" 2> "$L/decoder-compile.log" || { cat "$L/decoder-compile.log"; exit 1; }
"$T/test" > "$L/decoder.log" 2>&1 || { tail -3 "$L/decoder.log"; exit 1; }; grep PASS "$L/decoder.log" | tail -1 | cut -c1-150
scans(){ # LOG MIN: ordinary + activation scans must end done with BSS rows, none refused, no SCANDIAG line
  N=$(grep -a -c 'NORD2 WIFI IOS SCAN: .*status=0 .*bss=[1-9][0-9]* done=1 ' "$1"); X=$(grep -a -c -E 'NORD2 WIFI IOS SCAN: .*status=3 |SCANDIAG' "$1" || true)
  [ "$N" -ge "$2" ] && [ "$X" = 0 ] || { echo "SCAN CHECK FAILED ($1): complete=$N refused/diag=$X"; exit 1; }; echo "PASS scans in $(basename $(dirname $(dirname "$1"))): $N complete with BSS rows, 0 refused, 0 SCANDIAG"; }
( cd "$R" && python3 tools/test-nord2-wifi-handover.py --candidate "$C" --board "$R/research/nord2/wifi/calibration/WIFI" --log-dir "$L/handover/log" | tail -1 | cut -c1-120 )
scans "$L/handover/log/owning/test-native.log" 2                                       # no address change pending at all
for m in 1 2; do python3 "$H/privmac/make.py" --candidate "$C" --log-dir "$L/privmac-mode$m" --mode $m | tail -1 | cut -c240-420
  grep -a -q "NORD2 WIFI IOS MAC: activated=1 commands=$m private=1 result=" "$L/privmac-mode$m/owning/test-native.log" || { echo "no activation line (mode $m)"; exit 1; }
  # the ordinary scan (generation 99) AFTER the activation line completes with BSS rows
  awk '/NORD2 WIFI IOS MAC: activated=1/{a=1} a&&/NORD2 WIFI IOS SCAN: .*generation=99 status=0 .*bss=[1-9][0-9]* done=1 /{f=1} END{exit !f}' "$L/privmac-mode$m/owning/test-native.log" || { echo "no ordinary scan after the address change (mode $m)"; exit 1; }
  scans "$L/privmac-mode$m/owning/test-native.log" 3; done
( cd "$R" && python3 tools/test-nord2-wifi-lifecycle.py --candidate "$C" --log-dir "$L/lifecycle/log" | tail -1 | cut -c1-120 )
( cd "$R" && python3 tools/test-nord2-wifi-open.py --candidate "$C" --output "$L/open-codec" --only codec | tail -1 )
( cd "$R" && python3 tools/test-nord2-wifi-open.py --candidate "$C" --output "$L/open-legacy" --only legacy-codec | tail -1 )
echo HOST_TESTS_OK
