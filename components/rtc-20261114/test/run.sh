#!/bin/bash
# rtc-20261114/test/run.sh: host tests of 0540 (b/bsd/dev/nord2_rtc.h against a simulated MT6359 RTC, the boot choice in
# b/bsd/kern/nord2_clockfloor.h) + the 0530 floor test of heat-20261114 on the extended header (the 0530 policy must be unchanged).
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); H=$(dirname "$T"); O=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$O"' EXIT
CF="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
cc $CF -I$H/b/bsd/dev -I$H/b/bsd/kern $T/rtc_test.c -o $O/rtc
$O/rtc
cc $CF -I$H/b/bsd/kern $H/../heat-20261114/test/clockfloor_test.c -o $O/ck
$O/ck
if [ -d $H/patched-sources-0540 ]; then
  for f in bsd/dev/nord2_rtc.h bsd/dev/nord2_rtc.inc bsd/kern/nord2_clockfloor.h; do cmp $H/b/$f $H/patched-sources-0540/$f; done
fi
echo RTC_HOST_TESTS_OK
