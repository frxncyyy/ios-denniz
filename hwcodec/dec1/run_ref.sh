#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# run_ref.sh -- build the host library from a /tmp COPY of the tree (with the pending fixes below applied to the copy only)
# and run dec1/vdec_ref.c on generated H.264 streams.  Host only; never touches the phone; writes only under $OUT.
#
#   bash gpu/hwcodec/dec1/run_ref.sh            # plain tree + V30 va_list fix (required), fake WAITISR
#   VR_WIDE=1 bash gpu/hwcodec/dec1/run_ref.sh  # also a 0x40000 shadow for VDEC pages outside the 12 windows
#                                               # (stand-in for DEC04/DEC07 RWVC_WIN_VDEC_ALL) + larb SLP_CON ack
# Pending fixes applied to the copy (remove each line once its owner lands it in the tree):
#   V30  shim/n2shim_format.c n2fmt_args_va: AAPCS va_list passed by value (board 1794414606) -- landed by IV
#   V34  shim/n2shim_props.c vendor.mtk.vdec.frame.layout.mode = "0" (else 1080p is UFO MT21) (board 1794415185)
set -euo pipefail
HWD="$(cd "$(dirname "$0")/.." && pwd)"
VFS="$(cd "$HWD/../build/cam-vendor-20261109/vfs" && pwd)"
OUT="${OUT:-/tmp/n2hw-opus-dec1/ref}"
mkdir -p "$OUT/tree" "$OUT/vec"
rsync -a --delete --include='*/' --include='*.c' --include='*.h' --include='Makefile' --exclude='*' --prune-empty-dirs \
	"$HWD"/{emu,loader,shim,tests,vcu,ap,fakevenc,host,include} "$OUT/tree/"
python3 - "$OUT/tree" <<'EOF'
import sys, os
t = sys.argv[1]
p = os.path.join(t, 'shim/n2shim_format.c'); s = open(p).read()
old = '''	a->area = n2a32_ld32(c, guest_va);
	if (!a->area) {
		a->fault = 1;
		return -1;
	}'''
if old in s:
    s = s.replace(old, '\ta->area = guest_va;   /* AAPCS: va_list is passed by value */'); open(p, 'w').write(s); print('patched V30 va_list')
else:
    print('V30 va_list: already fixed in the tree (or changed): not patched')
p = os.path.join(t, 'shim/n2shim_props.c'); s = open(p).read()
old = '{ "vendor.mtk.vdec.frame.layout.mode", NULL,'
if old in s and not os.environ.get('VR_UFO_DEFAULT'):
    s = s.replace(old, '{ "vendor.mtk.vdec.frame.layout.mode", "0",'); open(p, 'w').write(s); print('patched V34 layout.mode=0 (MM21, no UFO)')
if os.environ.get('VR_WIDE'):
    p = os.path.join(t, 'vcu/n2vcu.c'); s = open(p).read()
    shadow = r'''
static uint32_t dec1_shadow[0x40000 / 4];
static int dec1_bad(uint32_t phys, int wr, uint32_t *val)
{
	uint32_t o;
	if (phys < 0x16000000u || phys >= 0x16040000u) return 0;
	o = phys - 0x16000000u;
	if (wr) {
		dec1_shadow[o >> 2] = *val;
		if (o == 0xd00cu || o == 0x2e00cu)   /* SMI larb SLP_CON: bit0 request -> bit16 ready */
			dec1_shadow[o >> 2] = (*val & 1u) ? (*val | 0x10000u) : (*val & ~0x10000u);
	} else {
		*val = dec1_shadow[o >> 2];
	}
	return 1;
}
'''
    s = s.replace('static const struct { uint32_t base, size; uint8_t win; } vcu_windows[] = {',
                  shadow + 'static const struct { uint32_t base, size; uint8_t win; } vcu_windows[] = {', 1)
    s = s.replace('''	if (!vcu_reg_lookup(base, &win, &off)) {
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
		return 0;
	}''', '''	if (!vcu_reg_lookup(base, &win, &off)) {
		uint32_t sv = 0;
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
		if (dec1_bad(base, 0, &sv))
			return (sv >> (8u * sh)) & vcu_lane_mask(bytes);
		return 0;
	}''', 1)
    s = s.replace('''	if (!vcu_reg_lookup(base, &win, &off)) {
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
		return;
	}''', '''	if (!vcu_reg_lookup(base, &win, &off)) {
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
		if (bytes == 4 && sh == 0)
			(void)dec1_bad(base, 1, &value);
		return;
	}''', 1)
    open(p, 'w').write(s); print('patched wide VDEC shadow (n2vcu copy)')
EOF
nice -n 10 make -s -C "$OUT/tree/host" "$OUT/o2/libn2vhost.a" OUT="$OUT" JOBS=2 -j2 2>&1 | grep -E 'N2VHOST build|error' || true
T="$OUT/tree"
nice -n 10 clang -std=c11 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -I"$T" -I"$T/include" -I"$T/tests/hw" \
	-I"$T/tests/emu" -I"$T/vcu" -I"$T/ap" -I"$T/host" -I"$T/shim" "$HWD/dec1/vdec_ref.c" -o "$OUT/vdec_ref" \
	-Wl,--whole-archive "$OUT/o2/libn2vhost.a" -Wl,--no-whole-archive -lpthread -lm
[ -s "$OUT/vec/t320b.h264" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 -frames:v 12 \
	-pix_fmt yuv420p -c:v libx264 -profile:v main -bf 2 -g 12 "$OUT/vec/t320b.h264"
[ -s "$OUT/vec/t1080.h264" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=1920x1080:rate=30 -frames:v 6 \
	-pix_fmt yuv420p -c:v libx264 -profile:v high -bf 0 "$OUT/vec/t1080.h264"
[ -s "$OUT/vec/h320.h265" ] || ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 -frames:v 8 \
	-pix_fmt yuv420p -c:v libx265 -x265-params log-level=error:bframes=2 "$OUT/vec/h320.h265"
pass=0; total=0
for v in t320b t1080 h320; do
	total=$((total + 1))
	hevc=; min=; nmax=1000
	# HEVC: header START works; frames need a VLD fetch-ok / HEVC block model in the fake (DECODE_PATH.md), so only VD-c
	[ "$v" = h320 ] && { hevc=1; min=3; nmax=0; }
	ext=h264; [ -n "$hevc" ] && ext=h265
	if VR_MIN="$min" VR_HEVC="$hevc" VR_FAKEIRQ="${VR_FAKEIRQ:-1}" timeout 110 "$OUT/vdec_ref" "$VFS" "$OUT/vec/$v.$ext" "$nmax" > "$OUT/$v.log" 2>&1; then pass=$((pass + 1)); fi
	grep -E '^VR (header|result|reached|guest)' "$OUT/$v.log" | sed "s/^/$v: /"
done
echo "N2TEST DEC1-REF $([ $pass = $total ] && echo PASS || echo FAIL) $pass/$total"
