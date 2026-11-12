#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Differential test: the same C kernels on the host (reference) and interpreted from ARM.
#
#   bash gpu/hwcodec/tests/emu/run_diff.sh
#
# Seven ARM configurations: -marm/-mthumb x -O0/-O2/-O3 -ftree-vectorize, plus an ARMv8-A
# -O3 variant.  The support object and the kernel object are loaded separately through V03,
# so cross-object imports (memcpy, __aeabi_uidiv, ...) are resolved by the loader.  The last
# line is "N2TEST T01 PASS n/n" or "... FAIL ...".
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"             # gpu/hwcodec
work="$(cd "$root/../.." && pwd)"             # work tree
tmp="${N2HW_TMP:-/tmp/n2hw-T01}/diff"
kernels_dir="$here/kernels"
inc="$root/include"
seeds="0,1,2,3,5,7,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xffff,0x10000,0x7fffffff,0x80000000,0xffffffff,0x12345678,0xdeadbeef,0x9e3779b9"

fail() { echo "N2TEST T01 FAIL $*"; exit 1; }

missing=()
for f in "$root/emu/n2a32_core.c" "$root/emu/n2a32_mem.c" \
         "$root/loader/n2elf.c" "$root/loader/n2elf_reloc.c"; do
	[ -f "$f" ] || missing+=("${f#$work/}")
done
if [ "${#missing[@]}" -ne 0 ]; then
	fail "waiting for V02/V03 sources: ${missing[*]}"
fi

mkdir -p "$tmp/results"
rm -f "$tmp/results"/*.txt "$tmp"/*.log

emu_sources=()
for f in "$root"/emu/n2a32_*.c; do
	[ -f "$f" ] && emu_sources+=("$f")
done
loader_sources=()
for f in "$root"/loader/n2elf*.c; do
	[ -f "$f" ] && loader_sources+=("$f")
done
kernel_sources=("$kernels_dir"/*.c)

# Kernel names, in one stable order for the host and the ARM runs.
"$here"/kernel_list.sh > "$tmp/kernels.txt"
[ -s "$tmp/kernels.txt" ] || fail "no k_* kernels found in $kernels_dir"

configs=(arm-O0 arm-O2 arm-O3v thumb-O0 thumb-O2 thumb-O3v armv8-O3v)
declare -A flags
flags[arm-O0]="-marm -O0"
flags[arm-O2]="-marm -O2"
flags[arm-O3v]="-marm -O3 -ftree-vectorize"
flags[thumb-O0]="-mthumb -O0"
flags[thumb-O2]="-mthumb -O2"
flags[thumb-O3v]="-mthumb -O3 -ftree-vectorize"
flags[armv8-O3v]="-marm -march=armv8-a -mfpu=neon-fp-armv8 -O3 -ftree-vectorize"

cc_arm=(clang --target=armv7a-linux-gnueabi -mfpu=neon-vfpv4 -mfloat-abi=softfp
	-ffreestanding -fno-builtin -nostdlib -fPIC -shared -fuse-ld=lld
	-fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
	-ffp-contract=off -Wl,--hash-style=gnu)

# ---- host reference, built at two optimisation levels (kernel-C consistency check) --------
python3 - "$tmp/kernels.txt" "$seeds" > "$tmp/ref_main.c" <<'PY'
import sys
names = [l.strip() for l in open(sys.argv[1]) if l.strip()]
seeds = sys.argv[2].split(',')
print('#include <stdint.h>\n#include <stdio.h>\n')
for n in names:
    print('extern uint32_t %s(uint32_t);' % n)
print('\nint main(void) {')
print('    static const uint32_t seeds[] = {%s};' % ', '.join(s + 'u' for s in seeds))
print('    unsigned j;')
for n in names:
    print('    for (j = 0; j < sizeof(seeds)/sizeof(seeds[0]); j++)')
    print('        printf("%s %%08x %%08x\\n", seeds[j], %s(seeds[j]));' % (n, n))
print('    return 0;\n}')
PY

clang -std=c11 -O2 -g -Wall -Wextra -Werror -ffp-contract=off -I "$kernels_dir" \
	"${kernel_sources[@]}" "$tmp/ref_main.c" -o "$tmp/ref_o2" \
	|| fail "host reference -O2 did not build"
clang -std=c11 -O0 -g -Wall -Wextra -Werror -ffp-contract=off -I "$kernels_dir" \
	"${kernel_sources[@]}" "$tmp/ref_main.c" -o "$tmp/ref_o0" \
	|| fail "host reference -O0 did not build"
"$tmp/ref_o2" > "$tmp/ref.txt"
"$tmp/ref_o0" > "$tmp/ref0.txt"

# ---- one harness binary, ASan+UBSan --------------------------------------------------------
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
	-fno-sanitize-recover=all -pthread -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
	-Wno-error=tautological-constant-out-of-range-compare -I "$inc" -I "$here" \
	"${emu_sources[@]}" "${loader_sources[@]}" "$here/diffrun.c" -lm -o "$tmp/diffrun" \
	|| fail "diffrun did not build (ASan+UBSan)"

# ---- each ARM configuration ---------------------------------------------------------------
for cfg in "${configs[@]}"; do
	dir="$tmp/$cfg"
	mkdir -p "$dir"
	echo "== building $cfg (${flags[$cfg]})"
	# shellcheck disable=SC2086
	"${cc_arm[@]}" ${flags[$cfg]} -Wl,-soname,libsupport.so \
		"$here/support_arm.c" -o "$dir/libsupport.so" \
		|| fail "$cfg: support object did not build"
	# shellcheck disable=SC2086
	"${cc_arm[@]}" ${flags[$cfg]} -Wl,-soname,libkernels.so \
		"${kernel_sources[@]}" -o "$dir/libkernels.so" \
		|| fail "$cfg: kernel object did not build"

	if ! "$tmp/diffrun" --lib "$dir/libsupport.so" --lib "$dir/libkernels.so" \
		--kernels "$tmp/kernels.txt" --seeds "$seeds" \
		--out "$tmp/results/$cfg.txt" > "$tmp/$cfg.log" 2>&1; then
		echo "   diffrun reported stopped/missing calls (details in $tmp/$cfg.log)"
	fi
done

python3 "$here/diffcheck.py" --ref "$tmp/ref.txt" --ref-o0 "$tmp/ref0.txt" \
	--results-dir "$tmp/results" --configs "${configs[*]}" \
	--kernels "$tmp/kernels.txt" --seeds "$seeds"
