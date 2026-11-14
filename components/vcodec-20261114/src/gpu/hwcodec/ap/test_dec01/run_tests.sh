#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/ap/test_dec01/run_tests.sh -- DEC01: the AP side of a VDEC session (ap/n2ap_vdec.c).
#
# Steps, all host only (nothing touches the phone, nothing is published):
#   1. the unit suite in three configurations (ASan+UBSan, -O2, TSan): a scripted daemon
#      (fake_daemon.c) over a stand-in /dev/vcu (dec_vcu_standin.c) and a register/power model
#      (dec_hw_model.c) exercise every AP message and every VCU -> AP request of one decode;
#   2. the layout oracle: ap/mtk_vdec_ipi.h against the kernel's own vdec_ipi_msg.h compiled for the
#      64-bit and the 32-bit branch (probe_mine.c vs probe_kernel.c);
#   3. the 32-bit AAPCS half: the same header compiled for armv7 with _Static_asserts;
#   4. the arm64-apple-ios16.0 compile of the library sources;
#   5. the real-vendor probe (probe_dec01.c) against the phone's own codec stack under the
#      interpreter, when the extracted /vendor is available: it prints the farthest milestone reached.
#
# Last line on success: N2TEST DEC01 PASS <passed>/<total>
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)          # gpu/hwcodec
top=$(cd "$root/../.." && pwd)           # the work tree root
work=/tmp/n2hw-DEC01
cc=${CC:-clang}
K=${N2HW_KERNEL:-$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel}
vendor=${N2HW_VENDOR_DIR:-$top/gpu/build/cam-vendor-20261109/vfs}

mkdir -p "$work"
rm -f "$work"/*.log "$work"/*.err "$work"/layout_* 2>/dev/null || true

fail() {
	echo "N2TEST DEC01 FAIL $*"
	exit 1
}

incs=(-I"$root/include" -I"$root/ap" -I"$root" -I"$root/host" -I"$root/vcu" -I"$here")
libsources=("$root/ap/n2ap_vdec.c" "$root/ap/n2vc_dma.c")
testsources=("$here/t_dec01.c" "$here/dec_hw_model.c" "$here/dec_vcu_standin.c" "$here/fake_daemon.c"
             "$root/emu/n2a32_mem.c" "$root/emu/n2a32_stub_classes.c")
base=(-std=c11 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE "${incs[@]}" -pthread)
checks=""

# ---- 1. the unit suite, three configurations ------------------------------------------------
for cfg in asan o2 tsan; do
	case $cfg in
	asan) flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all) ;;
	o2)   flags=(-O2) ;;
	tsan) flags=(-O1 -fsanitize=thread) ;;
	esac
	bin=$work/t_dec01_$cfg
	if ! "$cc" "${base[@]}" "${flags[@]}" "${libsources[@]}" "${testsources[@]}" -o "$bin" \
	     2>"$work/compile_$cfg.err"; then
		cat "$work/compile_$cfg.err" >&2
		fail "compile $cfg"
	fi
	if ! "$bin" "$cfg" >"$work/run_$cfg.log" 2>"$work/run_$cfg.err"; then
		cat "$work/run_$cfg.log" "$work/run_$cfg.err" >&2
		fail "run $cfg"
	fi
	line=$(grep -E "^DEC01 $cfg (PASS|FAIL) [0-9]+/[0-9]+$" "$work/run_$cfg.log" | tail -1)
	[ -n "$line" ] || { cat "$work/run_$cfg.log" >&2; fail "no result line from $cfg"; }
	case $line in *" FAIL "*) cat "$work/run_$cfg.log" >&2; fail "$cfg: $line" ;; esac
	n=$(printf '%s\n' "$line" | sed -E 's#.*PASS ([0-9]+)/([0-9]+)$#\1#')
	t=$(printf '%s\n' "$line" | sed -E 's#.*PASS ([0-9]+)/([0-9]+)$#\2#')
	[ "$n" = "$t" ] || fail "$cfg only $n/$t"
	if [ -z "$checks" ]; then checks=$n; else [ "$checks" = "$n" ] || fail "$cfg has $n checks, the first build had $checks"; fi
	echo "$line"
done

# ---- 2. layout oracle: our header vs the kernel's own header --------------------------------
[ -f "$K/drivers/media/platform/mtk-vcodec/vdec_ipi_msg.h" ] || \
	fail "kernel tree not found at $K (set N2HW_KERNEL)"
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
diff -u "$work/layout_kernel64.txt" "$work/layout_mine.txt" >"$work/layout64.diff" \
	|| { cat "$work/layout64.diff" >&2; fail "layout differs from the kernel header (64-bit branch)"; }
diff -u "$work/layout_kernel32.txt" "$work/layout_mine.txt" >"$work/layout32.diff" \
	|| { cat "$work/layout32.diff" >&2; fail "layout differs from the kernel header (32-bit branch)"; }
lines=$(wc -l < "$work/layout_mine.txt")
echo "DEC01 layout PASS $lines lines match the kernel header (64-bit and 32-bit branches)"

# ---- 3. the 32-bit AAPCS half ---------------------------------------------------------------
"$cc" --target=armv7a-linux-gnueabi -ffreestanding -nostdinc \
	-isystem "$("$cc" -print-resource-dir)/include" -std=c11 -Wall -Wextra -Werror \
	-I"$root/ap" -I"$here" -c "$here/probe_arm.c" -o "$work/probe_arm.o" \
	|| fail "armv7a compile of ap/mtk_vdec_ipi.h"
echo "DEC01 armv7 layout PASS (mtk_vdec_ipi.h static asserts)"

# ---- 4. the arm64-apple-ios16.0 compile of the library -------------------------------------
video=$(cd "$root/../video" 2>/dev/null && pwd || true)
if [ -n "$video" ] && [ -d "$video/sdk/sysinc" ]; then
	res=$("$cc" -print-resource-dir)
	"$cc" -c --target=arm64-apple-ios16.0 -std=c11 -O2 -fno-common -fvisibility=hidden \
		-ffreestanding -nostdinc -isystem "$res/include" -isystem "$video/sdk/sysinc" \
		-I "$root/include" -I "$root/ap" -I "$root" -I "$video/include" \
		-Wall -Wextra -Werror -o "$work/n2ap_vdec.ios.o" "$root/ap/n2ap_vdec.c" \
		|| fail "arm64-apple-ios16.0 compile of ap/n2ap_vdec.c"
	echo "DEC01 ios compile PASS (ap/n2ap_vdec.c for arm64-apple-ios16.0)"
else
	echo "DEC01 ios compile SKIP (gpu/video/sdk/sysinc not found)"
fi

# ---- 5. the real-vendor probe ----------------------------------------------------------------
probe_line=""
if [ -f "$vendor/lib/libvpud_vcodec.so" ] && [ -f "$root/host/n2vhost.h" ]; then
	if make -C "$root/host" o2 OUT="$work" >"$work/host_build.log" 2>&1; then
		probe=$work/n2vdec_probe
		if "$cc" "${base[@]}" -O1 "$root/ap/n2ap_vdec.c" "$here/probe_dec01.c" -o "$probe" \
		     -Wl,--whole-archive "$work/o2/libn2vhost.a" -Wl,--no-whole-archive -lpthread -lm \
		     2>"$work/probe_build.err"; then
			set +e
			N2HW_VENDOR_DIR="$vendor" N2HW_RUN_DIR="$work/run" \
				timeout 120 "$probe" --seconds 1 --require-mm21 --fake-irq >"$work/probe.log" 2>&1
			prc=$?
			set -e
			grep -E "^N2VDEC (reached|first-unmet) VD-" "$work/probe.log" | sed 's/^/  /' || true
			probe_line=$(grep -E "^N2VDEC (reached|first-unmet) VD-" "$work/probe.log" | tail -1 || true)
			echo "DEC01 vendor probe: ${probe_line:-no milestone line} (exit $prc)"
			grep -E "^N2VDEC (cap fmt|cap size|vsi after header|frame buffers|frames:|first display|stats:|vcu-final|fake:)" \
				"$work/probe.log" | sed 's/^/DEBUG /' || true
			case $probe_line in
			*"reached VD-d"*|*"reached VD-e"*) ;;
			*)
				cat "$work/probe.log" >&2
				fail "the real-vendor probe did not reach VD-d: ${probe_line:-no milestone}"
				;;
			esac
			if grep -q 'get_ringbuf input handle NULL' "$work/probe.log"; then
				fail "the vendor still logs the get_ringbuf input handle NULL error"
			fi
		else
			cat "$work/probe_build.err" >&2
			echo "DEC01 vendor probe SKIP (the probe did not build)"
		fi
	else
		echo "DEC01 vendor probe SKIP (gpu/hwcodec/host did not build; see $work/host_build.log)"
	fi
else
	echo "DEC01 vendor probe SKIP (no extracted /vendor at $vendor)"
fi

# ---- 6. the runner hook: real H.264/H.265 access units, generated with ffmpeg ------------------
if [ "${N2DEC01_VECTORS:-1}" != "0" ]; then
	bash "$here/decode_vectors.sh" || fail "the ffmpeg vector run failed"
fi

[ -n "$checks" ] || fail "no checks ran"
echo "N2TEST DEC01 PASS $((checks * 3))/$((checks * 3))"
