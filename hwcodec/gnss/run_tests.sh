#!/bin/bash
# SPDX-License-Identifier: MIT -- host tests of the GNSS milestone 1 (power core + /dev/gpsdl skeleton); XNU-target compile.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d -p "$D" .t.XXXXXX); trap 'rm -rf "$T"' EXIT
cc -O1 -Wall -Wextra -o $T/tp $D/test_gnss_pwr.c $D/gnss_pwr.c && $T/tp
cc -O1 -Wall -Wextra -o $T/tl $D/test_gnss_dl.c $D/gnss_dl.c $D/gnss_pwr.c && $T/tl | tail -1
cc -O1 -Wall -Wextra -Wno-unused-function -I$D -o $T/td $D/test_gpsdl.c $D/gnss_pwr.c $D/gnss_dl.c && $T/td | tail -1
clang --target=arm64-apple-ios16.0 -ffreestanding -nostdinc -isystem "$(clang -print-resource-dir)/include" -O2 -Wall -Wextra -Werror -c $D/gnss_pwr.c -o $T/gp.o && clang --target=arm64-apple-ios16.0 -ffreestanding -nostdinc -isystem "$(clang -print-resource-dir)/include" -O2 -Wall -Wextra -Werror -fno-builtin -I$D -c $D/gnss_dl.c -o $T/dl.o
echo "GNSS M1+M2 TESTS OK"
