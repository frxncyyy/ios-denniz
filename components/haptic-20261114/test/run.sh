#!/bin/bash
# haptic-20261114/test/run.sh: host tests of 0610 (b/bsd/dev/nord2_haptic.h against a simulated AW8697 + the policy).
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); H=$(dirname "$T"); O=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$O"' EXIT
CF="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
cc $CF -I$H/b/bsd/dev $T/haptic_test.c -o $O/hap
$O/hap
if [ -d $H/patched-sources-0610 ]; then
  for f in bsd/dev/nord2_haptic.h bsd/dev/nord2_haptic.inc bsd/dev/sweet_touch.c iokit/Kernel/arm/SweetFramebuffer.cpp; do cmp $H/b/$f $H/patched-sources-0610/$f; done
fi
echo HAPTIC_HOST_TESTS_OK
