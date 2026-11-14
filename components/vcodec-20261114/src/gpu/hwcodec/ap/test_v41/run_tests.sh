#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# V41 host test: the AP side of a VENC session + the DMA mirror.
# Builds under /tmp/n2hw-V41 in three configurations (ASan+UBSan, -O2, TSan), then checks the
# message layouts against the kernel's own headers (probe_kernel.c vs probe_mine.c) and compiles
# the header for armv7 (its _Static_asserts are the 32-bit half of the same check).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
work=/tmp/n2hw-V41
cc=${CC:-clang}
# The kernel tree this package's structs come from; override with N2HW_KERNEL.
K=${N2HW_KERNEL:-$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel}

mkdir -p "$work"
rm -f "$work"/*.log "$work"/*.err "$work"/*.txt 2>/dev/null || true

fail() {
	echo "N2TEST V41 FAIL $*"
	exit 1
}

# V40's real /dev/vcu model once it exists: V41_VCU_SRC=/path/to/vcu/n2vcu.c bash run_tests.sh
if [ -n "${V41_VCU_SRC:-}" ]; then
	vcu_src=$V41_VCU_SRC
else
	vcu_src=$here/n2vcu_standin.c
fi

# n2a32_mem.c is V02's guest address space (n2a32_mem_alloc is what the frozen n2vc_hw.h names);
# the stub classes only provide the weak trace hooks the memory code references.
sources=("$root/ap/n2ap_venc.c" "$root/ap/n2vc_dma.c" "$here/t_v41.c" "$here/hw_fake.c" "$vcu_src"
         "$root/emu/n2a32_mem.c" "$root/emu/n2a32_stub_classes.c")
incs=(-I"$root/include" -I"$root/ap" -I"$root" -I"$here")
base=(-std=c11 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L "${incs[@]}" -pthread)

checks=""
for cfg in asan o2 tsan; do
	case $cfg in
	asan) flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all) ;;
	o2)   flags=(-O2) ;;
	tsan) flags=(-O1 -fsanitize=thread) ;;
	esac
	bin=$work/t_v41_$cfg
	if ! "$cc" "${base[@]}" "${flags[@]}" "${sources[@]}" -o "$bin" -lm 2>"$work/compile_$cfg.err"; then
		cat "$work/compile_$cfg.err" >&2
		fail "compile $cfg"
	fi
	if ! "$bin" "$cfg" >"$work/run_$cfg.log" 2>"$work/run_$cfg.err"; then
		cat "$work/run_$cfg.log" "$work/run_$cfg.err" >&2
		fail "run $cfg"
	fi
	line=$(grep -E "^V41 build .* (PASS|FAIL) [0-9]+/[0-9]+$" "$work/run_$cfg.log" | tail -1)
	[ -n "$line" ] || { cat "$work/run_$cfg.log" >&2; fail "no result line from $cfg"; }
	case $line in
	*" FAIL "*) cat "$work/run_$cfg.log" "$work/run_$cfg.err" >&2; fail "$cfg: $line" ;;
	esac
	n=$(printf '%s\n' "$line" | sed -E 's#.*PASS ([0-9]+)/([0-9]+)$#\1#')
	t=$(printf '%s\n' "$line" | sed -E 's#.*PASS ([0-9]+)/([0-9]+)$#\2#')
	[ "$n" = "$t" ] || fail "$cfg only $n/$t"
	if [ -z "$checks" ]; then checks=$n; else [ "$checks" = "$n" ] || fail "$cfg has $n checks, first build had $checks"; fi
	echo "$line"
done

# ---- integration against V40's real /dev/vcu model, when it is in the tree -----------------
real_checks=""
if [ -f "$root/vcu/n2vcu.c" ]; then
	for cfg in asan o2; do
		case $cfg in
		asan) flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all) ;;
		o2)   flags=(-O2) ;;
		esac
		bin=$work/t_v41_real_$cfg
		rsources=("$root/ap/n2ap_venc.c" "$root/ap/n2vc_dma.c" "$here/t_v41_real.c" "$here/hw_fake.c"
		          "$root/vcu/n2vcu.c" "$root/emu/n2a32_mem.c" "$root/emu/n2a32_stub_classes.c")
		if ! "$cc" "${base[@]}" "${flags[@]}" "${rsources[@]}" -o "$bin" -lm 2>"$work/compile_real_$cfg.err"; then
			cat "$work/compile_real_$cfg.err" >&2
			fail "compile real-n2vcu $cfg"
		fi
		if ! "$bin" >"$work/run_real_$cfg.log" 2>"$work/run_real_$cfg.err"; then
			cat "$work/run_real_$cfg.log" "$work/run_real_$cfg.err" >&2
			fail "run real-n2vcu $cfg"
		fi
		line=$(grep -E "^V41 real n2vcu (PASS|FAIL) [0-9]+/[0-9]+$" "$work/run_real_$cfg.log" | tail -1)
		[ -n "$line" ] || { cat "$work/run_real_$cfg.log" >&2; fail "no real-n2vcu result line ($cfg)"; }
		case $line in *" FAIL "*) fail "real-n2vcu $cfg: $line" ;; esac
		n=$(printf '%s\n' "$line" | sed -E 's#.*PASS ([0-9]+)/([0-9]+)$#\1#')
		if [ -z "$real_checks" ]; then real_checks=$n; else [ "$real_checks" = "$n" ] || fail "real-n2vcu check counts differ"; fi
		echo "$line"
	done
else
	echo "V41 real n2vcu SKIP (vcu/n2vcu.c not in the tree; the stand-in covered the API)"
fi

# ---- layout oracle: our header vs the kernel's own header --------------------------------
[ -d "$K/drivers/media/platform/mtk-vcodec" ] || fail "kernel tree not found at $K (set N2HW_KERNEL)"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$root/ap" "$here/probe_mine.c" -o "$work/probe_mine" \
	|| fail "compile probe_mine"
"$cc" -std=c11 -Wall -Wextra -Werror -D__KERNEL__ -I"$here/kstub" -I"$K/include/uapi" \
	-I"$K/drivers/media/platform/mtk-vcodec" "$here/probe_kernel.c" -o "$work/probe_kernel64" \
	|| fail "compile probe_kernel (64-bit branch)"
"$cc" -std=c11 -Wall -Wextra -Werror -D__KERNEL__ -UCONFIG_64BIT -I"$here/kstub" \
	-I"$K/include/uapi" -I"$K/drivers/media/platform/mtk-vcodec" "$here/probe_kernel.c" \
	-o "$work/probe_kernel32" || fail "compile probe_kernel (32-bit branch)"
"$work/probe_mine" >"$work/layout_mine.txt" || fail "run probe_mine"
"$work/probe_kernel64" >"$work/layout_kernel64.txt" || fail "run probe_kernel64"
"$work/probe_kernel32" >"$work/layout_kernel32.txt" || fail "run probe_kernel32"
diff -u "$work/layout_kernel64.txt" "$work/layout_mine.txt" >"$work/layout.diff" \
	|| { cat "$work/layout.diff" >&2; fail "layout differs from the kernel header (64-bit branch)"; }
diff -u "$work/layout_kernel32.txt" "$work/layout_mine.txt" >"$work/layout32.diff" \
	|| { cat "$work/layout32.diff" >&2; fail "layout differs from the kernel header (32-bit branch)"; }
echo "V41 layout PASS $(wc -l < "$work/layout_mine.txt") lines match the kernel header"

# ---- the 32-bit AAPCS half: compile the header for armv7 (static asserts only) -----------
"$cc" --target=armv7a-linux-gnueabi -ffreestanding -nostdinc \
	-isystem "$("$cc" -print-resource-dir)/include" -std=c11 -Wall -Wextra -Werror \
	-I"$root/ap" -c "$here/probe_arm.c" -o "$work/probe_arm.o" \
	|| fail "armv7a compile of ap/mtk_venc_ipi.h"
echo "V41 armv7 layout PASS (mtk_venc_ipi.h static asserts)"

# ---- optional: the arm64-apple-ios16.0 compile of the two library sources -------------------
# Same flags as gpu/hwcodec/build_ios.sh (its own product list does not build these objects today;
# daemon/n2ip_deps.h only includes the header).  Skipped when gpu/video/sdk is not present.
video=$(cd "$root/../video" 2>/dev/null && pwd || true)
if [ -n "$video" ] && [ -d "$video/sdk/sysinc" ]; then
	res=$("$cc" -print-resource-dir)
	for src in ap/n2ap_venc.c ap/n2vc_dma.c; do
		"$cc" -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden \
			-ffreestanding -nostdinc -isystem "$res/include" -isystem "$video/sdk/sysinc" \
			-I "$root/include" -I "$root/ap" -I "$root" -I "$video/include" \
			-Wall -Wextra -Werror -o "$work/$(basename "${src%.c}").ios.o" "$root/$src" \
			|| fail "arm64-apple-ios16.0 compile of $src"
	done
	echo "V41 ios compile PASS (n2ap_venc.c, n2vc_dma.c for arm64-apple-ios16.0)"
else
	echo "V41 ios compile SKIP (gpu/video/sdk/sysinc not found)"
fi

[ -n "$checks" ] || fail "no checks ran"
if [ -n "$real_checks" ]; then
	checks=$((checks + real_checks))
fi
echo "N2TEST V41 PASS $checks/$checks"
