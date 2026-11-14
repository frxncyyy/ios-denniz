#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
apply_hevc_patches.py -- the changes outside fakevenc/n2venc_fake_hevc.* that a host HEVC encode session
needs, as anchored, idempotent edits on a COPY of gpu/hwcodec.  Owner: opus-hevc.  Each block names the
owning package; the owners get the same text on the board.  Usage:

    python3 apply_hevc_patches.py <root of a COPY of gpu/hwcodec>      # refuses to run outside /tmp

Blocks:
  V41        ap/n2ap_venc.[ch]   n2ap_enc_param_1080p30_hevc() (the V4L2 HEVC defaults)
  opus-venc  fakevenc/n2venc_fake.c   the HEVC hook (3 small edits: include, frame trigger, stats)
  IV         host/Makefile        build fakevenc/n2venc_fake_hevc.c
  IV         host/n2vhost.c       N2VHOST_CODEC=hevc: open with 'H265', HEVC params, write bitstream-WxH.h265
  IV         host/run_vhost.sh    check *.h265 with tests/check/hevc_check.py
Prints APPLIED / ALREADY / SKIP per block; exit 1 when a block is neither applied nor present.
"""
import os
import sys

root = sys.argv[1] if len(sys.argv) > 1 else None
if not root or not os.path.isdir(os.path.join(root, 'host')):
    sys.exit('usage: apply_hevc_patches.py <copy of gpu/hwcodec>')
if '/tmp/' not in os.path.realpath(root) + '/':
    sys.exit('apply_hevc_patches.py: refusing to edit outside /tmp (work on a copy)')
failed = []


def edit(owner, rel, marker, pairs):
    p = os.path.join(root, rel)
    s = open(p).read()
    if marker in s:
        print(f'ALREADY {owner:9s} {rel}: {marker[:60]!r}')
        return
    t = s
    for old, new in pairs:
        if t.count(old) != 1:
            print(f'SKIP    {owner:9s} {rel}: anchor not unique/found: {old[:70]!r}')
            failed.append(rel)
            return
        t = t.replace(old, new, 1)
    open(p, 'w').write(t)
    print(f'APPLIED {owner:9s} {rel}: {marker[:60]!r}')


# ---------------------------------------------------------------------------------------------- V41
edit('V41', 'ap/n2ap_venc.h', 'n2ap_enc_param_1080p30_hevc', [
    ('void n2ap_enc_param_1080p30_h264(struct n2ap_enc_param *p);',
     'void n2ap_enc_param_1080p30_h264(struct n2ap_enc_param *p);\n'
     '/* The same for an H265 session (n2ap_venc_open(..., N2AP_FOURCC_H265, ...)): V4L2 HEVC defaults\n'
     ' * profile MAIN, level 4, tier MAIN (mtk_vcodec_enc.c:2760-2776); n2ap_venc_start() converts them\n'
     ' * with venc_h265_get_profile/level (2 = Main, level 4 main tier = 10). */\n'
     'void n2ap_enc_param_1080p30_hevc(struct n2ap_enc_param *p);'),
])
edit('V41', 'ap/n2ap_venc.c', 'n2ap_enc_param_1080p30_hevc', [
    ('/* ---- encode (venc_vcu_if.c:515-609 + venc_common_if.c:120-217)',
     'void n2ap_enc_param_1080p30_hevc(struct n2ap_enc_param *p)\n'
     '{\n'
     '\tif (p == NULL) {\n'
     '\t\treturn;\n'
     '\t}\n'
     '\tn2ap_enc_param_1080p30_h264(p);        /* same geometry, rate, QP and header-mode defaults */\n'
     '\tp->profile = 0;                         /* V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN, mtk_vcodec_enc.c:2760-2762 */\n'
     '\tp->level = 5;                           /* V4L2_MPEG_VIDEO_HEVC_LEVEL_4 (enum value 5), :2769-2772 */\n'
     '\tp->tier = 0;                            /* V4L2_MPEG_VIDEO_HEVC_TIER_MAIN, :2773-2776 */\n'
     '}\n\n'
     '/* ---- encode (venc_vcu_if.c:515-609 + venc_common_if.c:120-217)'),
])

# ---------------------------------------------------------------------------------------- opus-venc
edit('opus-venc', 'fakevenc/n2venc_fake.c', 'n2venc_hevc_frame(', [
    ('#include "n2venc_fake.h"\n',
     '#include "n2venc_fake.h"\n#include "n2venc_fake_hevc.h"   /* opus-hevc: HEVC frames (n2venc_fake_hevc.c) */\n'),
    ('\tif (dma_xfer(f, bs_pa, out, n, 1) != 0)\n\t\tf->st.dma_errors++;\ndone:',
     '\tif (dma_xfer(f, bs_pa, out, n, 1) != 0)\n\t\tf->st.dma_errors++;\n\tgoto done;\nhevc_done:\n'
     '\tif (n == 0) {\n\t\tf->st.overflow++;\n\t\tR[VR_BS_BYTES / 4u] = bs_cap;\n\t\traise_irq(f, core, IRQ_DRAM);\n'
     '\t\treturn;\n\t}\ndone:'),
    ('\tf->st.frames++;\n',
     '\tf->st.frames++;\n'
     '\t{   /* opus-hevc: a session whose sequence header was HEVC gets an HEVC slice instead (hook) */\n'
     '\t\tstruct n2venc_hevc_io io = { f, R, hevc_dma, hevc_avail };\n'
     '\t\tint hn = n2venc_hevc_frame(&io, &bs_cap);\n\n'
     '\t\tif (hn >= 0) {\n\t\t\tn = (uint32_t)hn;\n\t\t\tf->st.last_bs_pa = bs_pa;\n'
     '\t\t\tf->st.last_luma_pa = y_pa;\n\t\t\tf->st.last_chroma_pa = c_pa;\n\t\t\tgoto hevc_done;\n\t\t}\n\t}\n'),
    ('static void raise_irq(struct n2venc_fake *f, unsigned core, uint32_t bits)\n',
     '/* opus-hevc hook: DMA for n2venc_fake_hevc.c */\n'
     'static int hevc_dma(void *ctx, uint32_t pa, void *buf, uint32_t bytes, int write)\n'
     '{\n\treturn dma_xfer((struct n2venc_fake *)ctx, pa, buf, bytes, write);\n}\n\n'
     'static int hevc_avail(void *ctx, uint32_t pa, uint32_t *avail)\n'
     '{\n\tuint32_t h, off;\n\n\t(void)ctx;\n\treturn dma_locate(pa, &h, &off, avail);\n}\n\n'
     'static void raise_irq(struct n2venc_fake *f, unsigned core, uint32_t bits)\n'),
])

edit('opus-venc', 'fakevenc/run_tests.sh', 'n2venc_fake_hevc.c', [
    ('"$HERE/t_fakevenc.c" "$HERE/n2venc_fake.c" \\\n',
     '"$HERE/t_fakevenc.c" "$HERE/n2venc_fake.c" "$HERE/n2venc_fake_hevc.c" -lm \\\n'),
])

# ----------------------------------------------------------------------------------------------- IV
edit('IV', 'host/Makefile', 'fakevenc/n2venc_fake_hevc.c', [
    ('$(wildcard $(HWD)/tests/hw/n2vc_hw_fake.c) $(wildcard $(HWD)/fakevenc/n2venc_fake.c))',
     '$(wildcard $(HWD)/tests/hw/n2vc_hw_fake.c) $(wildcard $(HWD)/fakevenc/n2venc_fake.c) '
     '$(wildcard $(HWD)/fakevenc/n2venc_fake_hevc.c))'),
])
edit('IV', 'host/n2vhost.c', 'N2VHOST_CODEC', [
    ('static unsigned ap_timeout_ms(void)\n',
     '/* N2VHOST_CODEC=hevc drives an H265 session instead of H.264 (opus-hevc). */\n'
     'static int codec_is_hevc(void)\n{\n\tconst char *c = getenv("N2VHOST_CODEC");\n\n'
     '\treturn c && (!strcmp(c, "hevc") || !strcmp(c, "h265") || !strcmp(c, "hvc1"));\n}\n\n'
     'static unsigned ap_timeout_ms(void)\n'),
    ('rc = n2ap_venc_open(&e, n2vhost_vcu(h), hw, N2AP_FOURCC_H264, ap_timeout_ms());',
     'rc = n2ap_venc_open(&e, n2vhost_vcu(h), hw, codec_is_hevc() ? N2AP_FOURCC_H265 : N2AP_FOURCC_H264,\n'
     '\t\t\t\t    ap_timeout_ms());'),
    ('\t\tn2ap_enc_param_1080p30_h264(&p);\n',
     '\t\tif (codec_is_hevc())\n\t\t\tn2ap_enc_param_1080p30_hevc(&p);\n\t\telse\n'
     '\t\t\tn2ap_enc_param_1080p30_h264(&p);\n'),
    ('\t\t       vs.last_bytes, vs.last_mb_w, vs.last_mb_h);\n\t}\n#endif\n',
     '\t\t       vs.last_bytes, vs.last_mb_w, vs.last_mb_h);\n\t}\n'
     '\tif (venc && !quiet && codec_is_hevc()) {\n\t\tstruct n2venc_hevc_stats hs;\n\n'
     '\t\tn2venc_hevc_get_stats(&hs);\n'
     '\t\tprintf("N2VHOST fake-venc-hevc frames=%u headers=%u defaults=%u %ux%u qp=%u last_bytes=%u "\n'
     '\t\t       "overflow=%u dma_errors=%u unsupported=%u\\n", hs.frames, hs.headers_learned, hs.defaults,\n'
     '\t\t       hs.width, hs.height,\n'
     '\t\t       hs.qp, hs.last_bytes, hs.overflow, hs.dma_errors, hs.unsupported);\n\t}\n#endif\n'),
    ('#include "n2venc_fake.h"        /* opus-venc: VENC behaviour on top of T02\'s fake */\n',
     '#include "n2venc_fake.h"        /* opus-venc: VENC behaviour on top of T02\'s fake */\n'
     '#include "n2venc_fake_hevc.h"   /* opus-hevc: HEVC frames of the fake VENC */\n'),
    ('\t\tp.intra_period = 30;\n\t\trc = n2ap_venc_start(e, &p);\n',
     '\t\tp.intra_period = 30;\n'
     '\t\tif (getenv("N2VHOST_JOINED")) {   /* the daemon\'s mode: headers joined with every IDR */\n'
     '\t\t\tp.seq_hdr_mode = 1;\n\t\t\tp.prependheader = 1;\n\t\t}\n'
     '\t\trc = n2ap_venc_start(e, &p);\n'),
    ('\t\t\trc = ap_encode(h, e, 0, bs.handle, N2AP_ENC_SEQ_HDR, &r, &fds);\n',
     '\t\t\tif (getenv("N2VHOST_JOINED"))\n\t\t\t\tmemset(&r, 0, sizeof r);   /* no separate header job */\n'
     '\t\t\telse\n\t\t\t\trc = ap_encode(h, e, 0, bs.handle, N2AP_ENC_SEQ_HDR, &r, &fds);\n'),
    ('snprintf(path, sizeof path, "%s/bitstream-%ux%u.h264", out_dir, w, hh);',
     'snprintf(path, sizeof path, "%s/bitstream-%ux%u.%s", out_dir, w, hh,\n'
     '\t\t\t\t\t codec_is_hevc() ? "h265" : "h264");'),
])
edit('IV', 'host/run_vhost.sh', 'hevc_check.py', [
    ('rm -f "$run"/bitstream-*.h264\n', 'rm -f "$run"/bitstream-*.h264 "$run"/bitstream-*.h265\n'),
    ('last=$(tail -1 "$run/report-$cfg.txt")\n',
     '# HEVC session (N2VHOST_CODEC=hevc): same check with the HEVC checker (opus-hevc)\n'
     'if ls "$run"/bitstream-*.h265 >/dev/null 2>&1; then\n'
     '\tbss=$(ls -t "$run"/bitstream-*.h265 | head -1)\n'
     '\tframe=$run/frame-$size.nv12\n'
     '\tif python3 "$root/gpu/hwcodec/tests/check/hevc_check.py" "$bss" "${size%x*}" "${size#*x}" 1 \\\n'
     '\t\t--source "$frame" --quiet >"$run/hevc-check.log" 2>&1; then\n'
     '\t\techo "run_vhost.sh: $bss validated (HEVC SPS/geometry/decode/PSNR, $(wc -c <"$bss") bytes)" >&2\n'
     '\telse\n'
     '\t\techo "run_vhost.sh: $bss FAILED the HEVC check (rc $?, see $run/hevc-check.log)" >&2\n'
     '\tfi\nfi\n\n'
     'last=$(tail -1 "$run/report-$cfg.txt")\n'),
])

if failed:
    sys.exit(1)
