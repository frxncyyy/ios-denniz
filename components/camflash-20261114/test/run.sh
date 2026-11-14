#!/bin/bash
# camflash-20261114/test/run.sh: host tests of 0560 (nord2_torch.h strobe policy + MT6360 STROBE register sequence) and the 0496 tests
# against the 0560 header (unchanged behaviour of torch / pulse requests).
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); H=$(dirname "$T"); O=$(mktemp -d); trap 'rm -rf "$O"' EXIT
CF0="-Wno-missing-field-initializers"; CF="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
fn() { awk -v n="$2" '$0 ~ "^"n"\\(" {p=1; print prev} p {print} p && /^}/ {exit} {prev=$0}' "$1"; }
for f in n2t_hw_off n2t_hw_strobe; do
  diff <(fn $H/b/bsd/dev/sweet_touch.c $f) <(fn $T/strobe_test.c $f) > /dev/null || { echo "TEST COPY OF $f DIFFERS FROM sweet_touch.c"; exit 1; }
done
for f in n2t_hw_off n2t_hw_on; do
  diff <(fn $H/b/bsd/dev/sweet_touch.c $f) <(fn $T/torch_test_0496.c $f) > /dev/null || { echo "0496 TEST COPY OF $f DIFFERS"; exit 1; }
done
echo "register code in the tests == sweet_touch.c (n2t_hw_on, n2t_hw_off, n2t_hw_strobe)"
cc $CF $CF0 -I$H/b/bsd/dev $T/torch_test_0496.c -o $O/torch && $O/torch
cc $CF -I$H/b/bsd/dev $T/strobe_test.c -o $O/strobe && $O/strobe
echo STROBE_HOST_TESTS_OK
