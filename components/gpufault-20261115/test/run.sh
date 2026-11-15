#!/bin/bash
# gpufault-20261115/test/run.sh: host tests of piece 0750 (MMU-fault records, trailing != orphan, revival of a FAULTED client) on the
# q10jfinal19 chain sources (sim-20261115/fin19/integ/chain/s).  a/ = chain files, b/ = with 0750.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); G=$H/..; B=$G/..; S=$B/sim-20261115/fin19/integ/chain/s/bsd/dev; T0=$B/gpusched-20261114/test
T=$(mktemp -d -p $G); trap 'rm -rf "$T"' EXIT
F="-std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wno-misleading-indentation -DN2C_HW_RB_EXTERN"
STEPS=${STEPS:-200000}
{
python3 $H/make_core_test_0750.py
cc $F -I$G/b/bsd/dev -I$S $H/core_test_0750.c $G/b/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t750
$T/t750 $STEPS | grep -E 'targeted|random|revive on|prio:|pipe=|0669|CORE_'
# regression: the unchanged 0669 suite (revive off = n2c_init default) gives the same numbers on a/ and b/
for v in a b; do cc $F -I$G/$v/bsd/dev -I$S $T0/core_test_0669.c $G/$v/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t669$v; $T/t669$v $STEPS > $T/o669$v; done
tail -1 $T/o669b; diff $T/o669a $T/o669b && echo "0669 suite: a/ and b/ outputs identical (revive off)"
# the 0500 dynamic-AS suite: one intended difference (its 'orphan' fault hits the AS the FAULTED context still holds = trailing in 0750)
sed 's/CHECK(core.as_stats.orphan_faults == 1 \&\& core.stats.mmu_faults == faults + 1);/CHECK(core.as_stats.orphan_faults + core.as_stats.trail_faults == 1 \&\& core.stats.mmu_faults == faults + 1);/' $T0/gpuas-0666/core_test_0500.c > $T/core_test_0500.c
grep -q 'orphan_faults + core.as_stats.trail_faults' $T/core_test_0500.c
cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter -I$T0/gpuas-0666 -I$G/b/bsd/dev -I$S $T/core_test_0500.c $G/b/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/g500
$T/g500 $STEPS | tail -3
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I$G/b/bsd/dev -I$S -c $G/b/bsd/dev/nord2_gpu_core.c -o $T/c.o && echo "-Werror 0750 core: clean"
echo GPUFAULT_0750_HOST_TESTS_OK
} 2>&1 | tee $H/host-tests.log
grep -q '^GPUFAULT_0750_HOST_TESTS_OK' $H/host-tests.log
