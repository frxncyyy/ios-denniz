#!/bin/bash
# gfxall3: pan_disk_cache.c + pan_disk_cache_host_test.c on the host, with pan_screen.c's include paths/defines from the shared build tree
# (as rwa-pan-fb-host-test.sh) and the host Mesa build's libmesa_util (blob, sha1). Cache directory in a scratch dir. Prints 'pan_disk_cache: ok'.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); G=$NORD2_WORK/gpu
BN=$G/build/y2s-stage0/build-nord2; H=${H:-$G/video/build/WP08/hostmesa}; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
(cd $BN && ninja -t commands src/gallium/drivers/panfrost/libpanfrost.a.p/pan_screen.c.o | tail -1 | tr ' ' '\n' |
  grep -E '^-(I|D|std)' | grep -vE 'HAVE_SYS_SYSCTL_H|HAVE_XLOCALE_H|HAVE_BSD_QSORT_R|HAVE_STRTOD_L|HAVE_CET_H|ALLOW_KCMP' |
  sed "s|^-I\([^/]\)|-I$BN/\1|") > "$T/flags"
F="-O1 -g -w -D_GNU_SOURCE -DHAVE_SECURE_GETENV -DHAVE_THRD_CREATE=0 $(cat $T/flags) -DN2PC_DIR=\"$T/cache\" -DN2PC_OFF=\"$T/off\" -DN2PC_RO=\"$T/ro\" -fsanitize=address,undefined"
clang $F -c $D/pan_disk_cache.c -o $T/c.o
clang $F -c $D/pan_disk_cache_host_test.c -o $T/t.o
clang++ -fsanitize=address,undefined -o $T/t $T/t.o $T/c.o $H/src/util/libmesa_util.a $H/src/util/blake3/libblake3.a $H/src/c11/impl/libmesa_util_c11.a -lpthread -lm -lz
(cd $T && ASAN_OPTIONS=detect_leaks=0 ./t)
