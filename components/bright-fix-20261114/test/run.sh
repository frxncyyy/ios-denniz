#!/bin/bash
# bright-fix-20261114/test/run.sh: host tests of 0495 (sweet_fb_backlight.h) and 0496 (nord2_torch.h + the MT6360 register sequence).
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); H=$(dirname "$T"); O=$(mktemp -d); trap 'rm -rf "$O"' EXIT
CF="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
# the register code in the test must be the kernel's, byte for byte
fn() { awk -v n="$2" '$0 ~ "^"n"\\(" {p=1; print prev} p {print} p && /^}/ {exit} {prev=$0}' "$1"; }
for f in n2t_hw_off n2t_hw_on; do
  diff <(fn $H/b/bsd/dev/sweet_touch.c $f) <(fn $T/torch_test.c $f) > /dev/null || { echo "TEST COPY OF $f DIFFERS FROM sweet_touch.c"; exit 1; }
done
echo "register code in torch_test.c == sweet_touch.c (n2t_hw_on, n2t_hw_off)"
cc $CF -I$H/b/iokit/Kernel/arm $T/bl_test.c -o $O/bl && $O/bl
cc $CF -I$H/b/bsd/dev $T/torch_test.c -o $O/torch && $O/torch
echo BRIGHT_HOST_TESTS_OK
