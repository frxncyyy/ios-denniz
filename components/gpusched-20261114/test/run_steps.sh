#!/bin/bash
# gpusched-20261114/test/run_steps.sh: host tests of steps A (0660: ../p6/b), B (0664: ../p7/b), C (0665/0666: ../p8/b) on the q10jfinal1 chain sources.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); D=$H/..; S=$D/fin/integ/chain/s/bsd/dev
T=$(mktemp -d -p $D); trap 'rm -rf "$T"' EXIT
F="-std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wno-misleading-indentation -DN2C_HW_RB_EXTERN"
{
for st in "0661 p6" "0664 p7" "0666 p8"; do set -- $st
  python3 $H/make_core_test_$1.py
  cc $F -I$D/$2/b/bsd/dev -I$S $H/core_test_$1.c $D/$2/b/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t$1
  $T/t$1 ${STEPS:-200000} | grep -E 'targeted|random:|prio:|CORE_'
  cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I$D/$2/b/bsd/dev -I$S -c $D/$2/b/bsd/dev/nord2_gpu_core.c -o $T/c$1.o && echo "-Werror $1: clean"
done
for g in 0661:p6 0664:p7 0666:p8; do n=${g%%:*}; p=${g##*:}
  (cd $H/gpuas-$n && cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter -I$D/$p/b/bsd/dev -I$S core_test_0500.c $D/$p/b/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/g$n)
  $T/g$n 200000 > $T/og$n; diff <(tail -4 $T/og$n) <(grep -A3 '^random seed 0x9e37' $H/gpuas-$n/host-tests.log | head -4) && echo "pre-0630 suite on the $n core: identical numbers"
done
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function -I$D/p7/b/bsd/dev -I$S $H/hw_rb7_test.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/hw7 && $T/hw7
python3 $H/mutants_0661.py $D $S | tail -1
python3 $H/mutants_0664.py $D $S | tail -1
echo GPUSCHED_STEPS_HOST_TESTS_OK
} 2>&1 | tee $H/host-tests-steps.log
grep -q '^GPUSCHED_STEPS_HOST_TESTS_OK' $H/host-tests-steps.log
