#!/bin/bash
# 0652 host tests: the real n2ram code (extracted from the patched osfmk sources) for stages 0/1/2, ASan+UBSan.
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); cd $T
python3 -I ram8c_extract.py ram8c_gen.h
for st in 0 1 2; do
  nice -n 10 cc -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-function -DN2RAM_STAGE=$st -o t$st.bin ram8c_host_test.c
  ./t$st.bin | tee ram8c_host_test-s$st.out | head -1; rm -f t$st.bin
done
echo RAM8C_HOST_TESTS_OK
