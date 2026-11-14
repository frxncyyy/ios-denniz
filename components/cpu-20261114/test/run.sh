#!/bin/bash
# cpu-20261114 host tests: 0641 CCI rule / store order, 0642 prime-first policy (+ the original 0099 policy test), 0643 thermal ladder. ASan+UBSan.
set -euo pipefail
C=$(cd "$(dirname "$0")/.." && pwd)
S=$C/../device-mmu-q10jperf1-candidate/k/kernel/stage/bsd/dev
F="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined"
cd $C/test
{ cc $F -I$C/piece-0641/patched-sources-0641/bsd/dev -I$S cpuplat_test.c -o cpuplat_test && ./cpuplat_test
  cc $F -I$C/piece-0643/patched-sources-0643/bsd/dev therm_test.c -o therm_test && ./therm_test
  (cd $C/sched0099/tests && cc $F prime_test.c -o prime_test && ./prime_test && cc $F nord2_sched_policy_test.c -o pol_test && ./pol_test)
  echo CPU20261114_HOST_TESTS_OK; } 2>&1 | tee host-tests.log
