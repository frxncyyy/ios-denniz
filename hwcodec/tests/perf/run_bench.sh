#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Interpreter micro benchmark (opus-perf).  Host only; builds into /tmp/n2hw-perf/bench.
#
#   bash gpu/hwcodec/tests/perf/run_bench.sh [--emu DIR] [--ms N] [--prof]
#
# --prof also samples the host PC (SIGPROF, x86-64 Linux) and prints the hottest host functions.
# --emu DIR benchmarks another copy of the emu/ sources (e.g. a saved baseline) with the same kernels.
# Builds the guest kernels for A32 and T32 at -O2 (NEON + VFPv4, softfp like the vendor objects),
# the runner at -O2 without sanitizers, and prints guest MIPS per kernel and in total.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
hw=$(cd "$here/../.." && pwd)
emu=$hw/emu
ms=300
prof=0
while [ $# -gt 0 ]; do
	case $1 in
	--emu) shift; emu=$(cd "$1" && pwd) ;;
	--ms) shift; ms=$1 ;;
	--prof) prof=1 ;;
	*) echo "usage: $0 [--emu DIR] [--ms N] [--prof]" >&2; exit 2 ;;
	esac
	shift
done
out=${N2HW_TMP:-/tmp/n2hw-perf}/bench
mkdir -p "$out"

cc_arm=(clang --target=armv7a-linux-gnueabi -mfpu=neon-vfpv4 -mfloat-abi=softfp -O2
	-ffreestanding -fno-builtin -nostdlib -fPIC -shared -fuse-ld=lld
	-fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -Wl,--hash-style=gnu)
"${cc_arm[@]}" -marm "$here/bench_kernels.c" -o "$out/bench_arm.so"
"${cc_arm[@]}" -mthumb "$here/bench_kernels.c" -o "$out/bench_thumb.so"

nice -n 10 clang -std=c11 -O2 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
	-Wno-tautological-constant-out-of-range-compare -I "$hw/include" -I "$hw/tests/emu" -I "$hw/emu" \
	-DBENCH_NATIVE "$emu"/n2a32_*.c "$hw"/loader/n2elf.c "$hw"/loader/n2elf_reloc.c \
	"$here/bench.c" "$here/bench_kernels.c" -pthread -lm -o "$out/bench"

for isa in arm thumb; do
	echo "== $isa (emu=$emu)"
	"$out/bench" --lib "$out/bench_$isa.so" --ms "$ms" --native
done

if [ "$prof" = 1 ]; then
	nice -n 10 clang -std=c11 -O2 -g -no-pie -fno-pie -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
		-D_DEFAULT_SOURCE -D_GNU_SOURCE -Wno-tautological-constant-out-of-range-compare -I "$hw/include" \
		-I "$hw/tests/emu" -DBENCH_PROF "$emu"/n2a32_*.c "$hw"/loader/n2elf.c "$hw"/loader/n2elf_reloc.c \
		"$here/bench.c" -pthread -lm -o "$out/bench_prof"
	for isa in arm thumb; do
		"$out/bench_prof" --lib "$out/bench_$isa.so" --ms "$ms" --prof "$out/prof_$isa.txt" >/dev/null
		echo "== host profile, $isa guest (share of samples per host function)"
		llvm-symbolizer --obj="$out/bench_prof" --output-style=GNU <"$out/prof_$isa.txt" 2>/dev/null |
			awk 'NR % 2 == 1' | sort | uniq -c | sort -rn |
			awk -v tot="$(wc -l <"$out/prof_$isa.txt")" 'NR <= 20 { printf "  %5.1f%%  %s\n", 100 * $1 / tot, $2 }'
	done
fi
