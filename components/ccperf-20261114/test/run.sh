#!/bin/bash
# ccperf-20261114/test/run.sh: host tests of 0492 (GPU DVFS floor) and 0493 (DRAM rate at boot + watch).
#  1. gdvfs_floor_test: 0492's nord2_gov.h (../b) against the chain's (../a), see the file.
#  2. dram_watch_test: 0493's n2dr_watch_update + decode.
#  3. the 0460..0462 suite (dvfs-20261113/test/dvfs_sim_test.c) unchanged, on the merged chain with this piece's files on top.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); D=$(dirname "$H")
B=$NORD2_WORK/gpu/build
S=${STAGE:-$B/integ-20261114/chain/s}
CC="cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
$CC -o $T/floor $H/gdvfs_floor_test.c $H/gov_old.c $H/gov_new.c && $T/floor
$CC -o $T/dram $H/dram_watch_test.c && $T/dram
mkdir -p $T/dv/test $T/dv/b/bsd; cp -a $S/bsd/dev $T/dv/b/bsd/dev; cp $D/b/bsd/dev/nord2_gov.h $D/b/bsd/dev/nord2_dram_rate.h $T/dv/b/bsd/dev/
cp $B/dvfs-20261113/test/run.sh $B/dvfs-20261113/test/dvfs_sim_test.c $T/dv/test/
STAGE=$S bash $T/dv/test/run.sh | tail -4
echo CCPERF_HOST_TESTS_OK
