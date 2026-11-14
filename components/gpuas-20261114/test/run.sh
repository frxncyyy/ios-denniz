#!/bin/bash
# gpuas-20261114/test/run.sh: host tests of 0500 (dynamic GPU address spaces) against b/ (= patched-sources-0500), ASan+UBSan, the production core model.
#   core_test_base_0500.c  = gpulat-20261112/test/core_test_base.c (the 27 production scenarios) adapted: pointer va/bos, AS-tracking hardware model
#   core_test_0400_0500.c  = gpulat-20261112/test/core_test_0400.c without its include/main
#   core_test_0500.c       = static (kill switch) + dynamic runs of both, targeted AS cases, 3 x 200000-step randomized runs with invariants
# Then the same core with -Wall -Wextra -Werror (host cc) as a cheap lint (the kernel build compiles it with the CI -Wall -Wextra -Werror flags).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); cd "$H"
S=$H/../../integ-20261114/chain/s/bsd/dev      # nord2_gpu_mmu.c / nord2_gpu_va.c / hw headers (unchanged by 0500)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
{
cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter -I../b/bsd/dev -I"$S" \
   core_test_0500.c ../b/bsd/dev/nord2_gpu_core.c "$S/nord2_gpu_mmu.c" "$S/nord2_gpu_va.c" -o "$T/core0500"
"$T/core0500" "${STEPS:-200000}"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I../b/bsd/dev -I"$S" -c ../b/bsd/dev/nord2_gpu_core.c -o "$T/core.o" && echo "host -Werror compile of nord2_gpu_core.c: clean"
cmp ../b/bsd/dev/nord2_gpu_core.c ../patched-sources-0500/bsd/dev/nord2_gpu_core.c && cmp ../b/bsd/dev/nord2_gpu_core.h ../patched-sources-0500/bsd/dev/nord2_gpu_core.h && echo "tested sources == patched-sources-0500"
echo GPUAS_HOST_TESTS_OK
} 2>&1 | tee host-tests.log
grep -q '^GPUAS_HOST_TESTS_OK' host-tests.log
