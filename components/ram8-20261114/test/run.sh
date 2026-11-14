#!/bin/bash
# ram8-20261114 host tests: heap table / 8 GiB map / allocator / pool lending (ASan+UBSan), then the DT-driven memory map check.
#   HEAP_DIR=piece-0650 (default) or piece-0651: which b/sweet_gfx_heap.h is tested
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd); R=$(dirname "$T"); D=${HEAP_DIR:-piece-0650}
H=$R/$D/b/iokit/Kernel/arm/sweet_gfx_heap.h
nice -n 10 cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror "-DHEAP_H=\"$H\"" -o $T/t.bin $T/ram8_host_test.c
$T/t.bin > $T/ram8_host_test-$D.out; rm -f $T/t.bin; tail -1 $T/ram8_host_test-$D.out
python3 -I $R/tools/memmap.py --heap $H --check > $T/memmap-$D.out; tail -1 $T/memmap-$D.out
echo RAM8_HOST_TESTS_OK $D
