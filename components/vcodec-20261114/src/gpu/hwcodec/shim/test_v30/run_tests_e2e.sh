#!/bin/bash
# V30 end-to-end check: a real ARM .so importing the shims, through V03's loader and V02's core.
# Separate from run_tests.sh: it links other workers' packages (loader/, emu/) which are DONE but not mine.
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$H/.." && pwd)
W=$(cd "$S/.." && pwd)
OUT=/tmp/n2hw-v30/e2e
mkdir -p "$OUT"

# The guest: ARM mode, ARMv8-A AArch32 (so the compiler emits udiv instead of a libgcc call), soft-float AAPCS,
# no stack protector / unwind tables so the import list stays inside the five tables.
nice -n 10 clang --target=armv7a-linux-androideabi -march=armv8-a -mfloat-abi=soft \
	-fPIC -shared -nostdlib -fuse-ld=lld -O1 -fno-stack-protector -fno-unwind-tables \
	"$H/arm/t_e2e_guest.c" -o "$OUT/libv30e2e.so"

# The host: V03's loader + V02's core + the A32 instruction classes + the V30 shims.
# -std=gnu11 only because V02's core and V03's loader use POSIX signal interfaces.
nice -n 10 clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -I"$W/include" \
	"$H/t_e2e.c" \
	"$W/loader/n2elf.c" "$W/loader/n2elf_reloc.c" \
	"$W/emu/n2a32_core.c" "$W/emu/n2a32_mem.c" "$W/emu/n2a32_excl.c" "$W/emu/n2a32_stub_classes.c" \
	"$W/emu/n2a32_arm_dp.c" "$W/emu/n2a32_arm_media.c" "$W/emu/n2a32_arm_mem.c" "$W/emu/n2a32_arm_misc.c" \
	"$S/n2shim_string.c" "$S/n2shim_format.c" "$S/n2shim_heap.c" \
	-o "$OUT/t_e2e" -lpthread -lm
"$OUT/t_e2e" "$OUT/libv30e2e.so"
