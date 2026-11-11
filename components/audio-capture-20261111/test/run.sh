#!/bin/bash
# Host tests for patches 0320/0321 (microphone for recording). Run: bash run.sh  (binaries in $TMPDIR or the scratchpad)
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); D=$(cd "$T/.." && pwd)
W=$(cd "$D/../../.." && pwd)                                   # work root
STAGE=$W/gpu/build/device-mmu-q10in-candidate/k/kernel/stage/bsd/dev   # headers of the phone's series (q10in/q10jw)
OUT=${OUT:-${TMPDIR:-/tmp}/n2mic-test}; mkdir -p "$OUT"
CF0=(-std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all '-DNAC_DMB_LD()=' '-DNAC_DMB_ST()=' '-DNAC_DSB()='
    -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter)
CF=("${CF0[@]}" -I"$T/stub" -I"$STAGE")
for t in test_call_regress test_mic_call test_mic_ring test_mic_worker; do
  [ -f "$T/$t.c" ] || continue
  if [ $t = test_mic_worker ]; then cc "${CF0[@]}" -I"$T/stub_worker" -o "$OUT/$t" "$T/$t.c"
  else cc "${CF[@]}" -o "$OUT/$t" "$T/$t.c"; fi
  echo "== $t"; "$OUT/$t" | tail -2
done
# the call path's AFE/PMIC writes are byte-identical to the q10in source (same trace from base and src)
for v in base src; do
  cc "${CF[@]}" -DSRC="\"$D/$v/bsd/dev/audio_call.c\"" -o "$OUT/trace_$v" "$T/trace_call.c"
  "$OUT/trace_$v" > "$OUT/trace_$v.txt"
done
cmp "$OUT/trace_base.txt" "$OUT/trace_src.txt" && echo "== call-path write trace identical to base ($(grep -c "^afe\|^pm" "$OUT/trace_src.txt") register writes)"
