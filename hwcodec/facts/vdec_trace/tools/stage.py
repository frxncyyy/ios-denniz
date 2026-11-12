#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""stage.py -- DEC02: prepare a /tmp copy of the tree and of the two decode drivers.

Nothing here edits the work tree.  Four overlays are applied to copies only, each with the exact
text it expects and a clear failure if the tree already changed.  Every overlay is either a
property DEC02 needs and does not own, or a defect DEC02 measured and reported to its owner; each
one is skipped automatically once the tree carries the fix (so the package never double-patches).

  1. V34  shim/n2shim_props.c: vendor.mtk.vdec.frame.layout.mode = "0" (MM21, no UFO).  Without
     it a >=1080p session selects MT21 (UFO-compressed), a different page set.
     Owner post: opus-dec1, board 1794415185.
  2. V40  vcu/n2vcu.c: one entry RWVC_WIN_VDEC_ALL (12) at 0x16000000 + 0x30000 after the ten
     frozen VDEC entries.  This is DEC04/DEC07's window (include/redwood_vcodec.h rev 3); it is
     staged because the frozen header/V40 have not landed it yet.  Skipped once the tree has it.
  3. DEC01 ap/n2ap_vdec.c: two measured defects (posted to hw-dec01):
       a. n2ap_vdec_decode() does not harvest list_disp/list_free after the START.  In sync mode the
          daemon sends no PUT_FRAME_BUFFER (measured: 0 over 12 AUs), so the output buffers are
          never returned and the vendor stops decoding after 3 AUs ("ion_import failed", "no frame
          buffer").  The kernel's vdec_vcu_if worker harvests after vdec_if_decode() returns.
       b. the job path zeroes plane 1 (fb_dma[1]/fb_fd[1]) while vsi.dec.fb_num_planes is 2, so the
          vendor is told its chroma plane is at PA 0 (a DMA write to PA 0 on the phone).  The
          buffer a caller queued through n2ap_vdec_push_fb() has the second plane; use it.
     Each half is skipped independently once the tree has the fix.
  4. T02 tests/hw/n2vc_hw_fake.c: VDEC_BASE (0x16000000) and VDEC_RACING (0x16004000) are in the
     VDEC_SOC (VDE1) power domain, not VDEC_CORE.  Evidence: mt6885/mtk_vcodec_dec_pm.c:184-213
     restores the 68 racing words in mtk_vcodec_dec_clock_on() for BOTH hw_id values, and for
     MTK_VDEC_LAT only MT_CG_SOC + MT_CG_VDEC1 are enabled (VDEC0/CORE off); the VDE1 gates live at
     0x1600f000 (vdec_soc_gcon_clk) and the device tree's VDEC_BASE/VDEC_RACING regs are below
     0x16020000.  Measured consequence with the old map: 12 refused writes (one per AU) at
     0x16004100, exactly the AP's racing restore during LOCK_LAT.  Reported to hw-t02, hw-k02.
     Skipped once the fake maps them to RWVC_BLK_VDEC_SOC.

Usage: stage.py TREE_OUT DRIVER_OUT
"""
import os
import shutil
import subprocess
import sys

HWD = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))   # gpu/hwcodec
HERE = os.path.dirname(os.path.abspath(__file__))


def fail(msg):
    print("DEC02 stage: " + msg)
    sys.exit(1)


def replace_once(path, old, new, what):
    s = open(path).read()
    if s.count(old) != 1:
        fail("%s: expected text occurs %d times (%s)" % (path, s.count(old), what))
    open(path, "w").write(s.replace(old, new))


def stage_tree(tree):
    if os.path.isdir(tree):
        shutil.rmtree(tree)
    os.makedirs(tree, exist_ok=True)
    dirs = ["emu", "loader", "shim", "tests", "vcu", "ap", "fakevenc", "host", "include"]
    for d in dirs:
        subprocess.run(["rsync", "-a", "--include=*/", "--include=*.c", "--include=*.h",
                        "--include=Makefile", "--exclude=*", "--prune-empty-dirs",
                        os.path.join(HWD, d) + "/", os.path.join(tree, d) + "/"], check=True)

    # 1. V34 property: MM21 instead of UFO MT21 for >= 1080p.
    props = os.path.join(tree, "shim/n2shim_props.c")
    s = open(props).read()
    if '{ "vendor.mtk.vdec.frame.layout.mode", "0",' not in s:
        if '{ "vendor.mtk.vdec.frame.layout.mode", NULL,' not in s:
            fail("shim/n2shim_props.c: layout.mode entry not found")
        open(props, "w").write(s.replace('{ "vendor.mtk.vdec.frame.layout.mode", NULL,',
                                         '{ "vendor.mtk.vdec.frame.layout.mode", "0",'))
        print("DEC02 stage: V34 layout.mode=0 (MM21)")
    else:
        print("DEC02 stage: V34 layout.mode already 0")

    # 2. V40: the VDEC_ALL window (DEC04/DEC07 text), only if the tree does not have it.
    vcu = os.path.join(tree, "vcu/n2vcu.c")
    s = open(vcu).read()
    if "0x16000000u, 0x30000u" in s:
        print("DEC02 stage: V40 already has a 0x30000 VDEC window")
    else:
        anchor = "\t{ 0x1602f000u, 0x1000u, RWVC_WIN_VDEC_SYS },\n"
        if anchor not in s:
            fail("vcu/n2vcu.c: VDEC_SYS window line not found")
        s = s.replace(anchor, anchor + "\t{ 0x16000000u, 0x30000u, 12u },   /* DEC02 staged: "
                      "RWVC_WIN_VDEC_ALL, DEC04/DEC07 */\n", 1)
        open(vcu, "w").write(s)
        print("DEC02 stage: V40 window 12 = 0x16000000+0x30000 (DEC04/DEC07 text)")

    # 3. DEC01: the client must be the revision that harvests after START and programs plane 1.
    dec = os.path.join(tree, "ap/n2ap_vdec.c")
    dec_h = os.path.join(tree, "ap/n2ap_vdec.h")
    s = open(dec).read()
    h = open(dec_h).read()
    if "fb_c_handle" in h and "dec_harvest(d, 0);" in s:
        print("DEC02 stage: DEC01 client has the harvest + plane-1 fixes")
    else:
        fail("ap/n2ap_vdec.c is older than DEC01's harvest/plane-1 fix (no fb_c_handle or no "
             "dec_harvest after START); update gpu/hwcodec/ap or ask hw-dec01")

    # 4. T02 fake: the racing/base pages belong to the SOC domain.
    fake = os.path.join(tree, "tests/hw/n2vc_hw_fake.c")
    s = open(fake).read()
    if '{ "VDEC_RACING",  0x1000, RWVC_BLK_VDEC_SOC }' in s:
        print("DEC02 stage: T02 fake already has VDEC_RACING on VDEC_SOC")
    else:
        replaced = 0
        for name in ("VDEC_RACING", "VDEC_BASE"):
            old = '{ "%s",' % name
            i = s.find(old)
            if i < 0:
                fail("tests/hw/n2vc_hw_fake.c: %s row not found" % name)
            j = s.find("RWVC_BLK_VDEC_CORE", i)
            if j < 0:
                fail("tests/hw/n2vc_hw_fake.c: %s block not found" % name)
            s = s[:j] + "RWVC_BLK_VDEC_SOC" + s[j + len("RWVC_BLK_VDEC_CORE"):]
            replaced += 1
        open(fake, "w").write(s)
        print("DEC02 stage: T02 fake VDEC_BASE/VDEC_RACING -> VDEC_SOC (%d rows; posted)" % replaced)
    return


def main():
    if len(sys.argv) != 3:
        fail("usage: stage.py TREE_OUT DRIVER_OUT")
    tree, driver_out = sys.argv[1], sys.argv[2]
    stage_tree(tree)

    # The reference driver (opus-dec1's executable specification), with the recorder and phase
    # markers.  Kept as the independent cross-check of the page set.
    ref_out = os.path.join(os.path.dirname(driver_out), "vdec_ref.c")
    src = open(os.path.join(HWD, "dec1/vdec_ref.c")).read()
    src = src.replace('#include "n2shim_thread.h"',
                      '#include "n2shim_thread.h"\n#include "n2tr.h"', 1)
    old = ('\tif (n2vc_hw_open_fake(&hw, getenv("VR_REC") ? getenv("VR_REC") : "/dev/null")) '
           '{ puts("VR fake open failed"); return 1; }\n'
           '\trc = n2vhost_start(&h, &hw, argv[1]);')
    new = ('\tif (n2vc_hw_open_fake(&hw, getenv("VR_REC") ? getenv("VR_REC") : "/dev/null")) '
           '{ puts("VR fake open failed"); return 1; }\n'
           '\ttr = n2tr_open(&hw, getenv("N2DEC02_TRACE"));\n'
           '\tif (tr == NULL) { puts("VR n2tr_open failed"); return 1; }\n'
           '\trc = n2vhost_start(&h, n2tr_hw(tr), argv[1]);')
    if old not in src:
        fail("dec1/vdec_ref.c: fake/n2vhost_start block not found")
    src = src.replace(old, new, 1)
    src = src.replace('struct n2vhost *h = NULL; int rc, reached = 0;',
                      'struct n2vhost *h = NULL; struct n2tr *tr = NULL; int rc, reached = 0;', 1)
    src = src.replace('\tV->dec.planes = 2;                                     /* SET_PARAM_FB_NUM_PLANES is vsi-only */',
                      '\tV->dec.planes = 2;                                     /* SET_PARAM_FB_NUM_PLANES is vsi-only */\n'
                      '\tn2tr_mark(tr, "init");', 1)
    src = src.replace('\trc = start_au(sbuf, n, 0, 0);                          /* header decode: fb none (mtk_vcodec_dec.c:2212) */',
                      '\tn2tr_mark(tr, "header");\n'
                      '\trc = start_au(sbuf, n, 0, 0);                          /* header decode: fb none (mtk_vcodec_dec.c:2212) */', 1)
    old = ('printf("VR START au=%u bytes=%zu rc=%d changed=0x%x err=0x%x cpu=%.3fs mmio r=%llu w=%llu '
           'bad=%llu req W=%d G=%d P=%d LC=%d UC=%d LL=%d UL=%d\\n",')
    new = ('printf("VR START au=%u bytes=%zu rc=%d changed=0x%x err=0x%x cpu=%.3fs mmio r=%llu w=%llu '
           'bad=%llu gce=%llu/%llu req W=%d G=%d P=%d LC=%d UC=%d LL=%d UL=%d\\n",')
    if old not in src:
        fail("dec1/vdec_ref.c: per-AU printf not found")
    src = src.replace(old, new, 1)
    old = ('(unsigned long long)(s1.mmio_bad - s0.mmio_bad), nreq[0],')
    new = ('(unsigned long long)(s1.mmio_bad - s0.mmio_bad), '
           '(unsigned long long)(s1.gce_flushes - s0.gce_flushes), '
           '(unsigned long long)(s1.gce_cmds - s0.gce_cmds), nreq[0],')
    if old not in src:
        fail("dec1/vdec_ref.c: per-AU printf args not found")
    src = src.replace(old, new, 1)
    src = src.replace('\t\trc = start_au(sbuf + off, n, 1, 1000u + frames_in);',
                      '\t\tn2tr_mark(tr, "frame");\n'
                      '\t\trc = start_au(sbuf + off, n, 1, 1000u + frames_in);', 1)
    src = src.replace('\tn2vhost_stop(h);\n\t_exit(',
                      '\tn2tr_mark(tr, "deinit");\n\tn2tr_free(tr);\n\tn2vhost_stop(h);\n\t_exit(', 1)
    for needle in ('n2tr_mark(tr, "init")', 'n2tr_mark(tr, "header")',
                   'n2tr_mark(tr, "frame")', 'n2tr_free(tr);'):
        if needle not in src:
            fail("dec1/vdec_ref.c: marker insertion failed (%s)" % needle)
    open(ref_out, "w").write(src)
    print("DEC02 stage: reference driver %s with n2tr + MARK phases" % ref_out)


if __name__ == "__main__":
    main()
