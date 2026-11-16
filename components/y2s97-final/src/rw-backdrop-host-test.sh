#!/bin/bash
# y2s97: rw_backdrop_pure.h (the backdrop-rename matcher) on the build machine, ASan/UBSan, -Werror. Prints 'rw_backdrop: ok ...'.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -o "$T/t" "$D/rw_backdrop_host_test.c"
"$T/t"
