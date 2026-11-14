#!/bin/bash
# haptic4-20261114/test/run.sh: host tests of 0610+0611 (b/bsd/dev/nord2_haptic.h + nord2_haptic_ram.h, simulated AW8697):
# the 3 synthesis profiles, then the vendor mode with a fake vendor file (our own image in Android's file format) through
# make_vendor_header.py (checksum, table parse, index choice, generated header, -DN2H_VENDOR_RAM build).
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); H=$(dirname "$T"); O=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$O"' EXIT
CF="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
for prof in 0 1 2; do
  cc $CF -DN2H_RAM_PROFILE=$prof -I$H/b/bsd/dev $T/haptic_test.c -o $O/hap$prof -lm
  N2H_DUMP=$O/img$prof.bin $O/hap$prof
done
[ -n "${DUMP_DIR:-}" ] && cp $O/img*.bin "$DUMP_DIR"/ || true
# fake vendor file: checksum(2) base(2) image
python3 -I - "$O/img0.bin" "$O/fake_vendor.bin" <<'PY'
import sys
img = open(sys.argv[1], 'rb').read(); body = bytes([0x08, 0x00]) + img; ck = sum(body) & 0xffff
open(sys.argv[2], 'wb').write(bytes([ck >> 8, ck & 0xff]) + body)
PY
mkdir -p $O/v
python3 -I $H/make_vendor_header.py $O/fake_vendor.bin $O/v/nord2_haptic_vendor.h --tick 1 --click 2 --heavy 3
grep -q 'n2h_vendor_idx\[4\] = { 0, 1, 2, 3 }' $O/v/nord2_haptic_vendor.h
cc $CF -DN2H_VENDOR_RAM -I$O/v -I$H/b/bsd/dev $T/haptic_test.c -o $O/hapv -lm
$O/hapv
# a corrupted file is refused
python3 -I -c "import sys; d=bytearray(open(sys.argv[1],'rb').read()); d[100]^=1; open(sys.argv[2],'wb').write(d)" $O/fake_vendor.bin $O/bad.bin
! python3 -I $H/make_vendor_header.py $O/bad.bin $O/v/x.h 2>/dev/null
if [ -d $H/patched-sources-0614 ]; then
  for f in bsd/dev/nord2_haptic.h bsd/dev/nord2_haptic.inc bsd/dev/nord2_haptic_ram.h; do cmp $H/b/$f $H/patched-sources-0614/$f; done
fi
echo HAPTIC4_HOST_TESTS_OK
