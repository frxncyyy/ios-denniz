#!/bin/bash
# host test of 0460/0461/0462: dvfs_sim_test.c against the patched sources (b/) and the untouched q10jgs stage headers
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); D=$(dirname "$H")
S=${STAGE:-$D/../device-mmu-q10jgs-candidate/k/kernel/stage}
cc -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -I$D/b/bsd/dev -I$S/bsd/dev -o $H/dvfs_sim_test $H/dvfs_sim_test.c $D/b/bsd/dev/nord2_gpu_clk.c $S/bsd/dev/nord2_gpu_rail.c
$H/dvfs_sim_test
