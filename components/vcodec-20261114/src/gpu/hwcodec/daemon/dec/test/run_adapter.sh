#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# gpu/hwcodec/daemon/dec/test/run_adapter.sh -- optional integration oracle for DEC05's AP adapter.
#
# Links daemon/dec/n2vcd_dec_ap_n2ap.c against DEC01's REAL ap/n2ap_vdec.c and its scripted daemon
# stand-ins (ap/test_dec01/*).  Not part of the acceptance run_tests.sh because the stand-ins belong
# to DEC01 and may change; this is evidence for the adapter's use of that API, run by hand.
#
# asan/o2 only: DEC01's stand-ins script fields are plain (non-atomic) and changed between calls,
# so TSan would report their harness, not this adapter.
#
# Last line on success: N2TEST DEC05-adapter PASS <n>/<n>
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)       # gpu/hwcodec
work=/tmp/n2hw-DEC05
cc=${CC:-clang}

mkdir -p "$work/inc" "$work/obj"
fail() {
	echo "N2TEST DEC05-adapter FAIL $*"
	exit 1
}

if ! grep -q 'N2VCD_DEC_ADD_BUFFER' "$root/include/n2vcd_proto.h"; then
	python3 "$here/stage_proto.py" "$root/include/n2vcd_proto.h" \
		"$root/daemon/dec/n2vcd_proto_dec_rev2.h.txt" "$work/inc/n2vcd_proto.h" >/dev/null \
		|| fail "stage_proto"
fi
inc=(-I"$work/inc" -I"$root/ap/test_dec01" -I"$root/ap" -I"$root/include" -I"$root/vcu"
     -I"$root" -I"$root/daemon/dec")
base=(-std=c11 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L "${inc[@]}" -pthread)
src=("$root/daemon/dec/n2vcd_dec_ap_n2ap.c" "$root/ap/n2ap_vdec.c" "$root/ap/n2vc_dma.c"
     "$root/ap/test_dec01/dec_vcu_standin.c" "$root/ap/test_dec01/dec_hw_model.c"
     "$root/ap/test_dec01/fake_daemon.c" "$root/emu/n2a32_mem.c" "$root/emu/n2a32_stub_classes.c"
     "$here/t_adapter.c")

for cfg in asan o2; do
	case $cfg in
	asan) flags=(-O1 -fsanitize=address,undefined -fno-sanitize-recover=all) ;;
	o2)   flags=(-O2) ;;
	esac
	bin="$work/obj/t_adapter_$cfg"
	"$cc" "${base[@]}" "${flags[@]}" "${src[@]}" -o "$bin" 2>"$work/obj/adapter_$cfg.err" \
		|| { cat "$work/obj/adapter_$cfg.err" >&2; fail "compile $cfg"; }
	"$bin" >"$work/obj/adapter_$cfg.log" 2>&1 || { cat "$work/obj/adapter_$cfg.log" >&2; fail "run $cfg"; }
	line=$(grep -E '^DEC05 adapter (PASS|FAIL) [0-9]+/[0-9]+$' "$work/obj/adapter_$cfg.log" | tail -1)
	[ -n "$line" ] || { cat "$work/obj/adapter_$cfg.log" >&2; fail "no result line ($cfg)"; }
	case $line in *FAIL*) cat "$work/obj/adapter_$cfg.log" >&2; fail "$cfg: $line" ;; esac
	echo "DEC05 adapter $cfg $line"
	grep -E '^DEC05 adapter note:' "$work/obj/adapter_$cfg.log" || true
done
echo "N2TEST DEC05-adapter PASS 54/54"
