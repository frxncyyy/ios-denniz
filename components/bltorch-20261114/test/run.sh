#!/bin/bash
# bltorch-20261114/test/run.sh: host tests of 0501 (sweet_fb_backlight.h: legacy "brightness", safe floor, architecture).
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); H=$(dirname "$T"); O=$(mktemp -d -p "$H" .t.XXXXXX); trap 'rm -rf "$O"' EXIT
CF="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
cmp $H/c/iokit/Kernel/arm/sweet_fb_backlight.h $H/b/iokit/Kernel/arm/sweet_fb_backlight.h
cc $CF -I$H/c/iokit/Kernel/arm $T/bl2_test.c -o $O/bl2 && $O/bl2
echo BLTORCH_HOST_TESTS_OK
