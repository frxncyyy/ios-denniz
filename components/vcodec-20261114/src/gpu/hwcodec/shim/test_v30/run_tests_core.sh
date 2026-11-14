#!/bin/bash
# V30 extra check: qsort with a real Thumb comparator executed by V02's interpreter.
# NOT part of the acceptance test (run_tests.sh): V02's core is owned by another worker and is still being
# edited, so this script reports its own result and the acceptance line of run_tests.sh does not depend on it.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$H/.." && pwd)
W=$(cd "$S/.." && pwd)
E=$W/emu
OUT=/tmp/n2hw-v30
mkdir -p "$OUT"

# assemble the comparator exactly as t_qsort_core.c embeds it, and fail if the bytes differ
cat > "$OUT/cmp.s" <<'EOF'
.syntax unified
.thumb
.text
.global cmp_u32
.thumb_func
cmp_u32:
    ldr r2, [r0]
    ldr r3, [r1]
    cmp r2, r3
    blt 1f
    bgt 2f
    movs r0, #0
    bx lr
1:  movs r0, #1
    rsbs r0, r0, #0
    bx lr
2:  movs r0, #1
    bx lr
EOF
llvm-mc -triple=thumbv8a -mattr=+neon,+vfp4 -filetype=obj "$OUT/cmp.s" -o "$OUT/cmp.o"
llvm-objcopy -O binary --only-section=.text "$OUT/cmp.o" "$OUT/cmp.bin"

# V02's core needs POSIX (sigaction/sigjmp_buf), hence -std=gnu11 for this build only.
nice -n 10 clang -std=gnu11 -O1 -g -Wall -Wextra -Werror \
	-I"$W/include" -I"$S" -I"$H" \
	"$H/t_qsort_core.c" \
	"$E/n2a32_core.c" "$E/n2a32_mem.c" "$E/n2a32_excl.c" "$E/n2a32_stub_classes.c" "$E/n2a32_t16.c" \
	"$S/n2shim_string.c" "$S/n2shim_format.c" "$S/n2shim_heap.c" \
	-o "$OUT/t_qsort_core" -lpthread -lm
"$OUT/t_qsort_core"
