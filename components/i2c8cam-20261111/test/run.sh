#!/bin/bash
# Host model test of 0330 against shim/nord2-xnu (the CI objects' source): before = the shim as is (expected to FAIL with
# the phone signature start=-3 i2c=5/16 i2cpa=10006170 uncertain=1), after = the shim + ../shim-0330.diff (expected PASS).
# Then the whole tools/nord2-audio-check.py suite on the patched copy. Needs the private board container (argument 1).
set -euo pipefail
D=$(cd "$(dirname "$0")/.." && pwd); W=$(cd "$D/../../.." && pwd); BOARD=${1:?board container tfa98xx.cnt}
T=${TMPDIR:-/tmp}/n2i2c8-test; rm -rf "$T"; mkdir -p "$T"
cp -r "$W/shim" "$W/tests" "$W/tools" "$T/"; cp "$D/test/test-cam-power.c" "$T/tests/nord2-audio/"
S=$T/shim/nord2-xnu; TT=$T/tests/nord2-audio
cc() { clang -std=c11 -DNORD2 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
  -fsanitize=address,undefined -fno-sanitize-recover=all -I"$S" -I"$TT" "$TT/test-cam-power.c" \
  "$S"/audio_{hw,afe,id,i2c8,amp,board}.c -o "$1"; }
cc "$T/before"; if "$T/before" "$BOARD"; then echo "UNEXPECTED: unpatched shim passed" >&2; exit 1; fi
(cd "$T" && patch -p1 -s -i "$D/shim-0330.diff")
cc "$T/after"; "$T/after" "$BOARD"
python3 "$T/tools/nord2-audio-check.py" --board "$BOARD" | tail -1
