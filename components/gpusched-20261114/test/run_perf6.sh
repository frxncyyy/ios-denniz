#!/bin/bash
# gpusched-20261114/test/run_perf6.sh: host tests of q10jperf6 = step A (0660) + step C without 0664/0667 + the 0668 NEXT-eviction flag fix (../p10/b).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); D=$H/..; S=$D/fin/integ/chain/s/bsd/dev; P=$D/p10/b/bsd/dev
T=$(mktemp -d -p $D); trap 'rm -rf "$T"' EXIT
F="-std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wno-misleading-indentation -DN2C_HW_RB_EXTERN"
{
grep -c 'l2pol\|gl2\|n2c_js_start_cfg\|wc_dirty' $P/nord2_gpu_core.c $P/nord2_gpu_core.h $D/p10/b/iokit/Kernel/arm/Nord2GPU.cpp || echo "no 0664/0667 symbols"
python3 $H/make_core_test_0669.py
for t in 0661 0669; do
  cc $F -I$P -I$S $H/core_test_$t.c $P/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t$t
  $T/t$t ${STEPS:-200000} | grep -E 'targeted|random:|prio:|pg|CORE_'
done
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I$P -I$S -c $P/nord2_gpu_core.c -o $T/c.o && echo "-Werror core: clean"
(cd $H/gpuas-0666 && cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter -I$P -I$S core_test_0500.c $P/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/g)
$T/g 200000 > $T/og; diff <(tail -4 $T/og) <(grep -A3 '^random seed 0x9e37' $H/gpuas-0666/host-tests.log | head -4) && echo "pre-0630 suite on the perf6 core: identical numbers"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function -I$P -I$S $H/hw_rb10_test.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/hw && $T/hw
echo GPUSCHED_PERF6_HOST_TESTS_OK
} 2>&1 | tee $H/host-tests-perf6.log
grep -q '^GPUSCHED_PERF6_HOST_TESTS_OK' $H/host-tests-perf6.log
