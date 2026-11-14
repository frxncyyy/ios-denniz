#!/bin/bash
# gpusched-20261114/test/run.sh: host tests of 0630 (JS NEXT pipelining) + 0631 (fast-ramp GPU DVFS profile) against ../b (= the patched sources).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); D=$H/..; S=$D/../perf-20261114/integ/chain/s/bsd/dev; G=$D/../gpuas-20261114/test
T=$(mktemp -d -p $D); trap 'rm -rf "$T"' EXIT
F="-std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined"
{
cc $F -Wall -Wno-misleading-indentation -DN2C_HW_RB_EXTERN -I$D/b/bsd/dev -I$S $H/core_test_0630.c $D/b/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t0630
$T/t0630 ${STEPS:-200000}
cc $F -Wall -Wno-unused-function -I$D/b/bsd/dev -I$S $H/hw_rb_test.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/hwrb && $T/hwrb
(cd $G && cc $F -Wall -Wextra -Wno-unused-parameter -I$D/b/bsd/dev -I$S core_test_0500.c $D/b/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t0500)
$T/t0500 200000 | tee $T/o500 | tail -4
diff <(tail -4 $T/o500) <(grep -A3 '^random seed 0x9e37' $G/host-tests.log | head -4) && echo "pre-0630 suite (gpuas 0500, jsnext off): PASS with numbers identical to its own log"
cc $F -Wall -Wextra -I$D/b/bsd/dev $H/gfast_test.c -o $T/gfast && $T/gfast
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I$D/b/bsd/dev -I$S -c $D/b/bsd/dev/nord2_gpu_core.c -o $T/core.o && echo "host -Werror compile of nord2_gpu_core.c: clean"
python3 $H/mutants_0630.py $D $S
# 0632 (on ../b2 = 0630 + 0631 + 0632): the 0630 model test and the pre-0630 suite again (no precise job: unchanged), the 0632 model test, mutants
cc $F -Wall -Wno-misleading-indentation -DN2C_HW_RB_EXTERN -I$D/b2/bsd/dev -I$S $H/core_test_0630.c $D/b2/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t0630b && $T/t0630b ${STEPS:-200000} | tail -1
# the gpuas copy in gpuas-0632/ differs in ONE line: its "unknown submit flag" probe uses bit 2 (bit 1 is N2C_SUBMIT_PRECISE since 0632)
(cd $H/gpuas-0632 && cc $F -Wall -Wextra -Wno-unused-parameter -I$D/b2/bsd/dev -I$S core_test_0500.c $D/b2/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t0500b)
$T/t0500b 200000 > $T/o500b; diff <(tail -4 $T/o500b) <(grep -A3 '^random seed 0x9e37' $G/host-tests.log | head -4) && echo "pre-0630 suite on b2: identical numbers"
python3 $H/make_core_test_0632.py
cc $F -Wall -Wno-misleading-indentation -DN2C_HW_RB_EXTERN -I$D/b2/bsd/dev -I$S $H/core_test_0632.c $D/b2/bsd/dev/nord2_gpu_core.c $S/nord2_gpu_mmu.c $S/nord2_gpu_va.c -o $T/t0632 && $T/t0632 ${STEPS:-200000} | grep -E '0632 targeted|pipe=1|CORE_0632'
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I$D/b2/bsd/dev -I$S -c $D/b2/bsd/dev/nord2_gpu_core.c -o $T/core2.o && echo "host -Werror compile of b2 nord2_gpu_core.c: clean"
python3 $H/mutants_0632.py $D $S
# gpupipe1 userland: nord2_pan.c ABI v3 host test
mkdir -p $T/gp/include && cp $D/userland/src/redwood-drm/nord2_gpu_transport.h $T/gp/include/ && (cd $D/userland/src/redwood-drm && gcc -O1 -g -Wall -pthread -fsanitize=address,undefined -I$T/gp/include -o $T/gp/t gpupipe_host_test.c) && $T/gp/t | tail -1
# 0633: live voltage change of the rails (on ../b3)
cc $F -Wall -I$D/b3/bsd/dev -I$S $H/rail_set_test.c $D/b3/bsd/dev/nord2_gpu_rail.c -o $T/rail && $T/rail
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I$D/b3/bsd/dev -I$S -c $D/b3/bsd/dev/nord2_gpu_rail.c -o $T/rail.o && echo "host -Werror compile of b3 nord2_gpu_rail.c: clean"
echo GPUSCHED_HOST_TESTS_OK
} 2>&1 | tee $H/host-tests.log
grep -q '^GPUSCHED_HOST_TESTS_OK' $H/host-tests.log
