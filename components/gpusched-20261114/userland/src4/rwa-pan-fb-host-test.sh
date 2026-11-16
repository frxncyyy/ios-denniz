#!/bin/bash
# y2s96: rwa_pan_fb.c on the build machine against the Panfrost model of rwa_pan_fb_host_test.c, with pan_screen.c's own include
# paths and defines (from the shared build tree's ninja command), host compiler, no Apple SDK. Prints 'rwa_pan_fb: ok'.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
W=$NORD2_WORK
BN=$W/gpu/build/y2s-stage0/build-nord2; T=$(mktemp -d -p "$D/.."); trap 'rm -rf "$T"' EXIT
(cd $BN && ninja -t commands src/gallium/drivers/panfrost/libpanfrost.a.p/pan_screen.c.o | tail -1 | tr ' ' '\n' |
  grep -E '^-(I|D|std)' | grep -vE 'HAVE_SYS_SYSCTL_H|HAVE_XLOCALE_H|HAVE_BSD_QSORT_R|HAVE_STRTOD_L|HAVE_CET_H|ALLOW_KCMP' |
  sed "s|^-I\([^/]\)|-I$BN/\1|") > "$T/flags"
clang -O1 -g -w -D_GNU_SOURCE -DHAVE_SECURE_GETENV -DHAVE_THRD_CREATE=0 $(cat "$T/flags") -c "$D/rwa_pan_fb.c" -o "$T/fb.o"
clang -O1 -g -w -D_GNU_SOURCE -DHAVE_SECURE_GETENV -DHAVE_THRD_CREATE=0 $(cat "$T/flags") -c "$D/rwa_pan_fb_host_test.c" -o "$T/t.o"
clang -o "$T/t" "$T/t.o" "$T/fb.o"
"$T/t"
