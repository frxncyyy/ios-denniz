#!/bin/bash
# heat-20261114/test/run.sh: host tests of 0530 (nord2_clockfloor.h).  0531 has no host model (three nanohub ConfigCmd calls moved under a switch).
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); H=$(dirname "$T"); O=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$O"' EXIT
CF="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
cc $CF -I$H/b/bsd/kern $T/clockfloor_test.c -o $O/ck && $O/ck
cmp $H/b/bsd/kern/nord2_clockfloor.h $H/patched-sources-0530/bsd/kern/nord2_clockfloor.h
echo HEAT_HOST_TESTS_OK
