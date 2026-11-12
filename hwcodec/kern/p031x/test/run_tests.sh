#!/bin/bash
# SPDX-License-Identifier: MIT
# p031x host test: the PINNED 0310 + 0311 applied to kernel/b in /tmp, K02's t_k02.c (all its checks) + t_p031x.c (the VENC
# checks), ASan+UBSan -O1 and plain -O2.  Builds only under /tmp/n2hw-opus/test.  Last line: N2TEST P031X PASS n/n.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
P=$(cd "$HERE/.." && pwd)                         # kern/p031x
H=$(cd "$P/.." && pwd)                            # kern
ROOT=$(cd "$H/.." && pwd)                         # gpu/hwcodec
T=/tmp/n2hw-opus/test
fail() { echo "N2TEST P031X FAIL $*"; exit 1; }

rm -rf "$T"; mkdir -p "$T/tree" "$T/test"
cp -a "$ROOT/kernel/b/." "$T/tree/"
(cd "$T/tree" && patch -p1 -s --no-backup-if-mismatch -i "$P/0310-nord2-jpegdev-vcodec.patch") || fail "0310 does not apply to kernel/b"
(cd "$T/tree" && patch -p1 -s --no-backup-if-mismatch -i "$P/0311-nord2-vcodec-venc-autotest.patch") || fail "0311 does not apply"
# 0312 (venc_fast sysctl) and 0313 (vcodec pool 384 MiB at 0xA0000000) are hand-made deltas on top of 0310/0311 (not
# generated from the live sources); N2P_UPTO=0311 tests the generated pair only.
for n in 0312-nord2-vcodec-venc-fast 0313-nord2-vcodec-pool-384m 0314-nord2-vcodec-adaptive-wait 0315-nord2-jpeg-hw-decode-path 0316-nord2-jpeg-ioctl-kernel-buffer 0317-nord2-jpeg-encode-eoi-trim-128; do
	[ "${N2P_UPTO:-}" = 0311 ] && break
	(cd "$T/tree" && patch -p1 -s --no-backup-if-mismatch -i "$P/$n.patch") || fail "$n does not apply"
done
mkdir -p "$T/bsd" && ln -s "$T/tree/bsd/dev" "$T/bsd/dev"         # t_k02.c includes ../bsd/dev/nord2_vcodec.inc
# K02's test as it is: since K02 rev 3 its model has the VENC windows at 0x3000 and MDP (bit 19) on at reset, like the phone
# (runs/q10hw1/00-step0.log: PWR_STATUS 0x10580e03); the grep below refuses an older t_k02.c.
cp "$H/test_k02/t_k02.c" "$T/test/t_k02.c"
grep -q '0x3000u, 0x3000u' "$T/test/t_k02.c" || fail "K02 model window sizes not 0x3000"
cp "$HERE/t_p031x.c" "$T/test/"
CF=(-std=c11 -g -Wall -Wextra -Werror -iquote "$T/tree/bsd/dev" -I "$ROOT/include")   # the tree's headers (0313 pool) first
nice -n 10 clang "${CF[@]}" -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -o "$T/t-san" "$T/test/t_p031x.c" || fail "sanitizer build"
nice -n 10 clang "${CF[@]}" -O2 -o "$T/t-o2" "$T/test/t_p031x.c" || fail "-O2 build"
for v in san o2; do
	"$T/t-$v" > "$T/out-$v.txt" 2>&1 || true
	l=$(tail -n 1 "$T/out-$v.txt")
	case "$l" in N2TEST\ P031X\ PASS*) echo "$v: $(grep '^0311 checks' "$T/out-$v.txt"); $l" ;; *) grep -E '^FAIL|violation' "$T/out-$v.txt" | head -20; fail "$v: $l" ;; esac
done
rm -f "$T/t-san" "$T/t-o2"
# 0315/0316: K01's t_jpegdev.c (whole suite, decoder model on, ioctl through the sys_ioctl model) + t_jpegdec.c against the patched nord2_jpegdev.inc
if [ -e "$T/tree/bsd/dev/redwood_jpegdec.h" ]; then
	mkdir -p "$T/kernel" && ln -s "$T/tree" "$T/kernel/b"            # t_jpegdev.c includes ../kernel/b/bsd/dev/nord2_hwcodec.inc
	cp "$H/test_k01/t_jpegdev.c" "$HERE/t_jpegdec.c" "$T/test/"
	JF=(-std=c11 -g -Wall -Wextra -Werror -pthread -iquote "$T/tree/bsd/dev" -I "$ROOT/include")
	nice -n 10 clang "${JF[@]}" -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -o "$T/j-san" "$T/test/t_jpegdec.c" || fail "jpegdec sanitizer build"
	nice -n 10 clang "${JF[@]}" -O2 -o "$T/j-o2" "$T/test/t_jpegdec.c" || fail "jpegdec -O2 build"
	for v in san o2; do
		"$T/j-$v" -q > "$T/jout-$v.txt" 2>&1 || true
		l=$(tail -n 1 "$T/jout-$v.txt")
		case "$l" in N2TEST\ JPEGDEC\ PASS*) echo "jpegdec $v: $(grep '^0315 checks' "$T/jout-$v.txt"); $l" ;; *) grep -E 'FAIL|VIOLATION' "$T/jout-$v.txt" | head -20; fail "jpegdec $v: $l" ;; esac
	done
	rm -f "$T/j-san" "$T/j-o2"
fi
tail -n 1 "$T/out-san.txt"
