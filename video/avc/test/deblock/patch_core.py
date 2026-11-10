#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Make a build-directory copy of avc2/n2avc_core.c that honours N2AVC_F_DEBLOCK.

The real integration is the lead's: the three edits below belong in avc2/n2avc_core.c itself.  Until they land there this
script prepares a *copy* under build/ (never a source file) so the deblocking filter can be judged bit-exactly against ffmpeg
now.  It refuses to guess: every anchor must occur exactly once.

usage: patch_core.py <src n2avc_core.c> <dst n2avc_core.c>
exit 0: dst written (patched, or copied unchanged when the core already calls the filter)
exit 1: the source does not look like the expected core; nothing is written
"""
import sys

PROTO = """
/* R2 deblocking filter (avc2/n2avc_deblock.c) */
void n2avc_deblock_picture(n2avc_frame *rec, const n2avc_mbinfo *mb, int mb_w, int mb_h);
"""

CALL = """	if (e->cfg.flags & N2AVC_F_DEBLOCK)
		n2avc_deblock_picture(rec, e->mb, e->mb_w, e->mb_h);
"""


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: patch_core.py <src> <dst>\n")
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()

    if "n2avc_deblock_picture" in text:
        open(dst, "w", encoding="utf-8").write(text)
        print("patch_core: %s already calls n2avc_deblock_picture; copied unchanged" % src)
        return 0

    replacements = [
        ("refuse N2AVC_F_DEBLOCK",
         "\tif (c.flags & N2AVC_F_DEBLOCK)\n\t\treturn N2AVC_E_ARGUMENT;\n",
         ""),
        ("disable_deblocking_filter_idc",
         "\tn2bw_ue(&s->bw, 1);                                     /* disable_deblocking_filter_idc */\n",
         "\tif (e->cfg.flags & N2AVC_F_DEBLOCK) {\n"
         "\t\tn2bw_ue(&s->bw, 0);                             /* disable_deblocking_filter_idc: filter everything */\n"
         "\t\tn2bw_se(&s->bw, 0);                             /* slice_alpha_c0_offset_div2 */\n"
         "\t\tn2bw_se(&s->bw, 0);                             /* slice_beta_offset_div2 */\n"
         "\t} else {\n"
         "\t\tn2bw_ue(&s->bw, 1);\n"
         "\t}\n"),
        ("prototype after the include",
         "#include \"n2avc_int.h\"\n",
         "#include \"n2avc_int.h\"\n" + PROTO),
        ("deblock call before extend_plane",
         "\textend_plane(&rec->y, W, H, N2AVC_PAD);\n",
         CALL + "\textend_plane(&rec->y, W, H, N2AVC_PAD);\n"),
    ]

    for what, old, new in replacements:
        n = text.count(old)
        if n != 1:
            sys.stderr.write("patch_core: anchor %r occurs %d times (expected 1); refusing\n" % (what, n))
            return 1
        text = text.replace(old, new, 1)

    open(dst, "w", encoding="utf-8").write(text)
    print("patch_core: wrote %s (4 edits: open, slice header, prototype, call)" % dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
