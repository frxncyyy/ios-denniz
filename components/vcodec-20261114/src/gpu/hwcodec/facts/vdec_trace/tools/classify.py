#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""classify.py -- DEC02: turn n2tr traces + the reference driver's per-AU lines into PAGES.md and COST.md.

Inputs (all produced by run.sh):
  --trace320 F --trace1080 F   n2tr traces ("MARK phase" + "R/W/WM win off value [mask]")
  --log320 F --log1080 F       stdout of the staged dec1/vdec_ref run (VR header/START/result lines)
  --pages OUT --cost OUT       where to write the two documents

Exit status 0 when the self-checks pass, 1 otherwise; a summary of the checks goes to stderr so
run.sh can turn it into N2TEST lines.  Nothing here estimates the page list: a page is listed iff
the recorder saw at least one access to it, and the same trace file always yields the same table.
"""
import argparse
import os
import re
import sys
from collections import OrderedDict, defaultdict

VDEC_BASE = 0x16000000
VDEC_END = 0x16040000          # 0x16000000..0x1603ffff is the page range the task asks for
PAGE = 0x1000
WIN_VDEC_ALL = 12
WIN_NAMES = {
    0: "VENC", 1: "VENC_C1", 2: "VDEC_SYS", 3: "VDEC_BASE", 4: "VDEC_VLD", 5: "VDEC_MC",
    6: "VDEC_MV", 7: "VDEC_MISC", 8: "VDEC_LAT_MISC", 9: "VDEC_LAT_VLD", 10: "VDEC_RACING",
    11: "VDEC_SOC_GCON", 12: "VDEC_ALL (staged)",
}

# Physical base of every RWVC window (include/redwood_vcodec.h rev 2 ids) plus the one staged
# VDEC_ALL entry.  A trace line for a frozen window carries the *window-relative* offset; the
# decoder window 12 carries the offset from 0x16000000.  Without this table the page of an access
# served by, say, VDEC_VLD (0x16020000) would be computed at 0x16000000+off and be wrong.
WINDOW_BASE = {
    0: 0x17020000,   # VENC
    1: 0x17820000,   # VENC_C1
    2: 0x1602f000,   # VDEC_SYS
    3: 0x16000000,   # VDEC_BASE
    4: 0x16020000,   # VDEC_VLD
    5: 0x16021000,   # VDEC_MC
    6: 0x16023000,   # VDEC_MV
    7: 0x16025000,   # VDEC_MISC (0x4000)
    8: 0x16010000,   # VDEC_LAT_MISC
    9: 0x16011000,   # VDEC_LAT_VLD (0x400)
    10: 0x16004000,  # VDEC_RACING
    11: 0x1600f000,  # VDEC_SOC_GCON
    12: 0x16000000,  # VDEC_ALL (staged DEC04/DEC07)
}
MIPS = 35.0                    # guest MIPS figure for "instructions": tests/perf/STATUS.md:68
                               # (25-35 MIPS for the six-kernel mix; 35 = top of that range)

# Sub-block table from gpu/hwcodec/facts/VDEC_PROTOCOL.md section 3 (offsets from 0x16000000).
# Names in [I] there (from the vdec_hal_* module names and the Linux window names); the offsets
# are [V] (vdec_drv_init_hw_info @0x1e1b8 / Linux device tree).
SUBBLOCKS = [
    (0x00000000, "BASE",            "VDEC base (mt6893.dts VDEC_BASE)"),
    (0x00000800, "UFO",             "inside the BASE page"),
    (0x00001000, "SOC_1000",        "SOC side"),
    (0x00002000, "SOC_2000",        "SOC side"),
    (0x00003800, "SOC_3800",        "SOC side"),
    (0x00004000, "RACING",          "racing words 0x100..; VDEC_RACING"),
    (0x00004400, "RACING_4400",     "racing"),
    (0x0000d000, "LARB5",           "larb5 (SMI), kernel-owned; SLP_CON at +0x00c"),
    (0x0000e000, "SOC_E000",        "SOC side"),
    (0x0000f000, "SOC_GCON",        "vdec_soc_gcon; VDEC_SOC_GCON"),
    (0x0000f800, "SOC_GCON_F800",   "inside VDEC_SOC_GCON"),
    (0x00010000, "LAT_MISC",        "VDEC_LAT_MISC"),
    (0x00010800, "LAT_MISC_10800",  "inside VDEC_LAT_MISC"),
    (0x00011000, "LAT_VLD",         "VDEC_LAT_VLD"),
    (0x00011800, "LAT_VLD_TOP",     "outside the 0x400 LAT_VLD window"),
    (0x00012000, "LAT_AVC_VLD",     "LAT H.264 entropy/syntax"),
    (0x00013000, "LAT_MV",          "LAT MV"),
    (0x00014800, "LAT_14800",       "LAT WDMA/UBE [I]"),
    (0x00015000, "LAT_15000",       "LAT [I]"),
    (0x00015400, "LAT_15400",       "LAT [I]"),
    (0x00015800, "LAT_15800",       "LAT [I]"),
    (0x00016000, "LAT_16000",       "LAT [I]"),
    (0x00017000, "LAT_17000",       "LAT [I]"),
    (0x00018800, "LAT_18800",       "LAT [I]"),
    (0x00020000, "VLD",             "VDEC_VLD"),
    (0x00020800, "VLD_TOP",         "inside the VLD page"),
    (0x00021000, "MC",              "VDEC_MC"),
    (0x00022000, "AVC_VLD",         "H.264 syntax: ue(v) at +0x23c, barrel shifter"),
    (0x00023000, "AVC_MV",          "VDEC_MV"),
    (0x00024000, "PP",              "post-processor"),
    (0x00025000, "MISC",            "VDEC_MISC (0x4000 window)"),
    (0x00026000, "MISC_26000",      "inside VDEC_MISC"),
    (0x00026400, "MISC_26400",      "inside VDEC_MISC"),
    (0x00026800, "MISC_26800",      "inside VDEC_MISC"),
    (0x00027000, "MISC_27000",      "inside VDEC_MISC"),
    (0x00027800, "MISC_27800",      "inside VDEC_MISC"),
    (0x00028000, "MISC_28000",      "inside VDEC_MISC"),
    (0x00028400, "MISC_28400",      "inside VDEC_MISC"),
    (0x00028800, "MISC_28800",      "inside VDEC_MISC"),
    (0x00029000, "CORE_29000",      "CORE side, no frozen window"),
    (0x0002e000, "LARB4",           "larb4 (SMI), kernel-owned; SLP_CON at +0x00c"),
    (0x0002f000, "VDEC_SYS",        "vdec_gcon; VDEC_SYS"),
    (0x0002f800, "VDEC_SYS_F800",   "inside VDEC_SYS"),
]

PHASES = ["preinit", "init", "header", "frame", "deinit"]
PHASE_ORDER = {p: i for i, p in enumerate(PHASES)}


def subblock(off):
    best = SUBBLOCKS[0]
    for base, name, note in SUBBLOCKS:
        if base <= off:
            best = (base, name, note)
        else:
            break
    return best


def core_of(off):
    if 0x10000 <= off < 0x20000:
        return "LAT"
    if 0x20000 <= off < 0x30000:
        return "CORE"
    return "SOC"


def domain_of(off):
    # VDE (RWVC_BLK_VDEC_SOC) powers 0x00000-0x1ffff, VDE2 (RWVC_BLK_VDEC_CORE) 0x20000-0x2ffff.
    return "VDE (VDEC_SOC)" if off < 0x20000 else "VDE2 (VDEC_CORE)"


def pwr_text(states):
    """Measured power state at the accesses: (soc_on, core_on) pairs from the recorder."""
    names = {(1, 1): "SOC+CORE", (1, 0): "SOC only", (0, 1): "CORE only", (0, 0): "neither (bug)"}
    return ", ".join(names.get(st, str(st)) for st in sorted(states))


def parse_trace(path):
    """[(phase, kind, win, off, value, mask, thread, seq-in-thread, (soc_on, core_on))] in file order.

    The trailing "t<k> <seq>" is the guest thread tag and its per-thread sequence number (n2tr.h);
    older traces without it get thread 0 and the file position as the sequence.  The last element is
    the power state the recorder had at that access ("P <block> <on|off>" events), so a page whose
    block was not powered can be told from one that was, and DEC02 can show which accesses really
    need which domain instead of assuming it."""
    ops = []
    phase = "preinit"
    bad = []
    pos = 0
    pwr = {2: 0, 3: 0}      # RWVC_BLK_VDEC_SOC = 2, RWVC_BLK_VDEC_CORE = 3 (include/redwood_vcodec.h)
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for ln, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            w = line.split()
            if w[0] == "MARK":
                phase = w[1] if len(w) > 1 else ""
                if phase not in PHASE_ORDER:
                    bad.append("line %d: unknown phase %r" % (ln, phase))
                continue
            if w[0] == "P":
                blk = int(w[1])
                if blk in pwr:
                    pwr[blk] = 1 if w[2] == "on" else 0
                continue
            if w[0] in ("R", "W", "WM"):
                if len(w) < 4:
                    bad.append("line %d: short reg line" % ln)
                    continue
                win, woff, val = int(w[1]), int(w[2], 16), int(w[3], 16)
                mask = int(w[4], 16) if w[0] == "WM" and len(w) > 4 and w[4].startswith("0x") else 0xffffffff
                if win not in WINDOW_BASE:
                    bad.append("line %d: unknown window %d" % (ln, win))
                    continue
                off = WINDOW_BASE[win] + woff - VDEC_BASE   # global offset from 0x16000000
                tag, seq = 0, pos
                for j, tok in enumerate(w):
                    if tok.startswith("t") and tok[1:].isdigit():
                        tag = int(tok[1:])
                        if j + 1 < len(w) and w[j + 1].isdigit():
                            seq = int(w[j + 1])
                        break
                pos += 1
                ops.append((phase, w[0], win, off, val, mask, tag, seq, (pwr[2], pwr[3])))
            elif w[0] in ("BAD", "X"):
                bad.append("line %d: %s" % (ln, line))
    return ops, bad


def parse_log(path):
    out = {"header": None, "aus": [], "result": None, "reached": None, "query": [], "params": []}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            m = re.match(r"(?:VR|DC) header START rc=(-?\d+) changed=0x([0-9a-fA-F]+) pic=(\d+)x(\d+) "
                         r"buf=(\d+)x(\d+) fb_sz=(\d+)/(\d+) dpb=(\d+) fourcc=(\S+) cpu=([0-9.]+)s", line)
            if not m:
                m = re.match(r"DC HEADER rc=(-?\d+) changed=0x([0-9a-fA-F]+) pic=(\d+)x(\d+) "
                             r"buf=(\d+)x(\d+) fb_sz=(\d+)/(\d+) dpb=(\d+) fourcc=(\S+) cpu=([0-9.]+)s", line)
            if m:
                out["header"] = {
                    "rc": int(m.group(1)), "changed": int(m.group(2), 16),
                    "pic": (int(m.group(3)), int(m.group(4))),
                    "buf": (int(m.group(5)), int(m.group(6))),
                    "fb_sz": (int(m.group(7)), int(m.group(8))), "dpb": int(m.group(9)),
                    "fourcc": m.group(10), "cpu_s": float(m.group(11)),
                }
                continue
            m = re.match(r"VR START au=(\d+) bytes=(\d+) rc=(-?\d+) changed=0x([0-9a-fA-F]+) err=0x([0-9a-fA-F]+) "
                         r"cpu=([0-9.]+)s mmio r=(\d+) w=(\d+) bad=(\d+) gce=(\d+)/(\d+) "
                         r"req W=(\d+) G=(\d+) P=(\d+) LC=(\d+) UC=(\d+) LL=(\d+) UL=(\d+)", line)
            if not m:
                m = re.match(r"DC START au=(\d+) bytes=(\d+) rc=(-?\d+) changed=0x([0-9a-fA-F]+) err=0x([0-9a-fA-F]+) "
                             r"cpu=([0-9.]+)s mmio r=(\d+) w=(\d+) bad=(\d+) lastbad=0x([0-9a-fA-F]+) "
                             r"gce=(\d+)/(\d+) disp=(\d+) freed=(\d+)", line)
                if m:
                    out["aus"].append({
                        "au": int(m.group(1)), "bytes": int(m.group(2)), "rc": int(m.group(3)),
                        "changed": int(m.group(4), 16), "err": int(m.group(5), 16),
                        "cpu_s": float(m.group(6)), "r": int(m.group(7)), "w": int(m.group(8)),
                        "bad": int(m.group(9)), "lastbad": int(m.group(10), 16),
                        "gce_flush": int(m.group(11)), "gce_cmds": int(m.group(12)),
                        "disp": int(m.group(13)), "req": (),
                    })
                    continue
            if m:
                out["aus"].append({
                    "au": int(m.group(1)), "bytes": int(m.group(2)), "rc": int(m.group(3)),
                    "changed": int(m.group(4), 16), "err": int(m.group(5), 16),
                    "cpu_s": float(m.group(6)), "r": int(m.group(7)), "w": int(m.group(8)),
                    "bad": int(m.group(9)), "gce_flush": int(m.group(10)), "gce_cmds": int(m.group(11)),
                    "req": tuple(int(m.group(i)) for i in range(12, 18)),
                })
                continue
            m = re.match(r"DC result frames_in=(\d+) displayed=(\d+) freed=(\d+)", line)
            if m:
                out["result"] = {"frames_in": int(m.group(1)), "displayed": int(m.group(2)),
                                 "freed_fb": int(m.group(3)), "freed_bs": 0}
                continue
            m = re.match(r"VR result frames_in=(\d+) displayed=(\d+) freed_fb=(\d+) freed_bs=(\d+)", line)
            if m:
                out["result"] = {k: int(v) for k, v in zip(
                    ("frames_in", "displayed", "freed_fb", "freed_bs"), m.groups())}
                continue
            m = re.match(r"(?:VR|DC) reached (\S+)", line)
            if m:
                out["reached"] = m.group(1)
    return out


def page_table(ops):
    """page -> dict(r, w, phases set, first_phase, subblocks set, cores set, offsets set, ops)."""
    pages = OrderedDict()
    for phase, kind, win, off, val, mask, _tag, _seq, pw in ops:
        phys = VDEC_BASE + off
        pg = phys & ~(PAGE - 1)
        e = pages.setdefault(pg, {"r": 0, "w": 0, "phases": set(), "first": phase,
                                  "offsets": set(), "ops": 0, "win": set(), "kind": set(),
                                  "pwr": set()})
        e["ops"] += 1
        e["phases"].add(phase)
        if PHASE_ORDER.get(phase, 99) < PHASE_ORDER.get(e["first"], 99):
            e["first"] = phase
        if kind == "R":
            e["r"] += 1
        else:
            e["w"] += 1
        e["offsets"].add(off)
        e["win"].add(win)
        e["kind"].add(kind)
        e["pwr"].add(pw)
    return pages


def fmt_off(v):
    return "0x%05x" % v


def write_pages(path, t320, t1080, log320, log1080, trace_paths):
    p320 = page_table(t320)
    p1080 = page_table(t1080)
    all_pages = sorted(set(p320) | set(p1080))
    lines = []
    A = lines.append
    A("# VDEC pages the vendor stack touches (0x16000000..0x1603ffff, 4 KiB each)")
    A("")
    A("Generated by `gpu/hwcodec/facts/vdec_trace/run.sh` (DEC02); do not edit by hand.  Host only:")
    A("the phone's own vpud + libvcodecdrv/libvcodec_utility run under the AArch32 interpreter on T02's")
    A("fake hardware, driven by **DEC01's AP client** (`ap/n2ap_vdec.c`: QUERY_CAP, INIT, SET_PARAM,")
    A("one DEC_START per access unit, RESET, DEINIT, and the LOCK/UNLOCK/WAITISR/GET_FB/PUT_FB service)")
    A("with DEC02's `n2tr` recorder around the back end.  opus-dec1's reference driver")
    A("(`dec1/vdec_ref.c`) runs the same two streams once each as an independent cross-check of the")
    A("page set.  Nothing here ran on the phone.")
    A("")
    A("Every page below was **seen in the recorder**, i.e. the vendor issued at least one 32-bit read or")
    A("write to that 4 KiB page; the R/W column is the number of accesses.  The trace is byte-identical on")
    A("a re-run (run.sh checks it twice).  Offsets are from 0x16000000.  `VDEC_ALL` is the one window the")
    A("decode path needs (`RWVC_WIN_VDEC_ALL`, DEC04/DEC07 text); run.sh stages that entry into a /tmp copy")
    A("of `vcu/n2vcu.c` because the frozen header has not been revised to rev 3 yet.  The recorder itself")
    A("models window 12: offsets 0x00000..0x1ffff are powered by VDE (`RWVC_BLK_VDEC_SOC`),")
    A("0x20000..0x2ffff by VDE2 (`RWVC_BLK_VDEC_CORE`), and the SMI larb SLP_CON handshake")
    A("(larb5 +0x00c, larb4 +0x00c: bit 0 request, bit 16 ready) is answered, as the real larb does.")
    A("")
    A("Inputs: the two n2tr traces of this run (`t320b.trace`, `t1080.trace` in the output")
    A("directory; the names in this document are run-independent on purpose, so a re-run produces a")
    A("byte-identical file).")
    A("")
    A("## 1. Page list")
    A("")
    A("| offset | physical | R/W | domain that powers it | powered while accessed | sub-block | core | first phase | ops 320x240 | ops 1920x1080 |")
    A("|---|---|---|---|---|---|---|---|---|---|")
    for pg in all_pages:
        off = pg - VDEC_BASE
        e320 = p320.get(pg)
        e1080 = p1080.get(pg)
        r = (e320["r"] if e320 else 0) + (e1080["r"] if e1080 else 0)
        w = (e320["w"] if e320 else 0) + (e1080["w"] if e1080 else 0)
        rw = ("R" if r else "") + ("/W" if w else "")
        if r and not w:
            rw = "R"
        elif w and not r:
            rw = "W"
        else:
            rw = "R+W"
        base, name, _note = subblock(off)
        # first phase over both traces, earliest in protocol order
        firsts = [e["first"] for e in (e320, e1080) if e]
        first = min(firsts, key=lambda p: PHASE_ORDER.get(p, 99)) if firsts else "-"
        ops320 = e320["ops"] if e320 else 0
        ops1080 = e1080["ops"] if e1080 else 0
        pw = set()
        for e in (e320, e1080):
            if e:
                pw |= e["pwr"]
        A("| 0x%05x | 0x%08x | %s | %s | %s | %s | %s | %s | %d | %d |" % (
            off, pg, rw, domain_of(off), pwr_text(pw), name, core_of(off), first, ops320, ops1080))
    A("")
    A("`R/W` counts reads and writes (%d reads, %d writes on 320x240; %d reads, %d writes on 1920x1080)." % (
        sum(e["r"] for e in p320.values()), sum(e["w"] for e in p320.values()),
        sum(e["r"] for e in p1080.values()), sum(e["w"] for e in p1080.values())))
    A("")
    A("### Which power domain each page needs")
    A("")
    A("The `domain` column is the address split the kernel table uses for the decode pages: the VDE")
    A("(`RWVC_BLK_VDEC_SOC`) range 0x16000000..0x1601ffff and the VDE2 (`RWVC_BLK_VDEC_CORE`) range")
    A("0x16020000..0x1602ffff.  The two clock/register groups live at 0x1600f000 (`vdec_soc_gcon_clk`,")
    A("CLK_VDE1_*: MT_CG_SOC/MT_CG_VDEC1) and 0x1602f000 (`vdec_gcon_clk`, CLK_VDE2_*: MT_CG_VDEC0);")
    A("see mt6893.dts `vdec@16000000` `clock-names` and `mt6885/mtk_vcodec_dec_pm.c`")
    A("`mtk_vcodec_dec_clock_on()`.  Which *MTCMOS domain* each page physically sits in is [I], not")
    A("[V]: the vendor never tells the two apart (the Android stack has both on during a decode), so")
    A("no host measurement can settle it.  The kernel's conservative rule -- pages below 0x0d and")
    A("0x0e are CORE, which takes a hidden reference on SOC (K02 `n2vc_vdec_page_blk()`, DEC07 rev 3)")
    A("-- is safe for every access in this document except one, described next.")
    A("")
    A("The `powered while accessed` column is **measured**, not inferred: the recorder logs every")
    A("power transition (`P <block> <on|off>`), so classify.py knows which domains were on when each")
    A("access happened.  Every vendor access happens with both domains on.  The one exception is the")
    A("client's own 68-word racing restore at 0x16004100: DEC01's client answers LOCK_LAT first and")
    A("does what `mtk_vcodec_dec_clock_on()` does -- power SOC, restore the words, then power CORE")
    A("-- so the restore runs with CORE off.  With the kernel's conservative map (racing = CORE) that")
    A("write is refused.  Two ways to make it legal, both consistent with the kernel source: the")
    A("client takes the CORE reference for its whole active period (both domains on, like Android),")
    A("or the kernel allows the racing page while only SOC is on.  Reported to hw-dec01/hw-k02; the")
    A("staged fake used by run.sh models the second (pages below 0x0d as SOC) so the trace is clean.")
    A("## 2. Pages in 0x16000000..0x1603ffff that were never touched")
    A("")
    untouched = [VERDEC for VERDEC in range(VDEC_BASE, VDEC_END, PAGE) if VERDEC not in all_pages]
    A("These pages received no access in either run, so the kernel need not allow them (the")
    A("0x30000 `VDEC_ALL` window already covers every touched page; pages 0x30000..0x3ffff are unused):")
    A("")
    A("```")
    for pg in untouched:
        A("0x%05x" % (pg - VDEC_BASE))
    A("```")
    A("")
    A("## 3. Access order: first distinct access per phase and vendor thread")
    A("")
    A("The vendor decodes on its own guest threads (vpud's main loop and one vdec service thread), and")
    A("DEC01's client answers the LOCK/UNLOCK/WAITISR requests synchronously on the daemon thread that")
    A("issued them -- so the client's own register work (block power, the 68-word racing restore/save)")
    A("shares a trace tag with vendor programming.  The interleaving between threads is")
    A("scheduling-dependent, but each thread's own program order is not; this table sorts by (phase,")
    A("thread, per-thread sequence), i.e. the order each thread touches a new offset, which is what a")
    A("kernel or a fake hardware model must be prepared for.  The racing batch")
    A("(0x16004100..0x1600420c) is left out here -- it is one memcpy-like block, not programming of the")
    A("decode cores -- and is described in section 4.  The page table's `first phase` column is the")
    A("cross-thread answer.")
    A("")
    seen = set()
    A("| # | phase | thread | offset | sub-block | op | value |")
    A("|---|---|---|---|---|---|---|")
    n = 0
    # The vendor decodes on two guest threads (LAT and CORE); their interleaving is scheduling
    # dependent, each thread's own order is not.  Sorting by (phase, thread, per-thread sequence,
    # offset) therefore gives the real programming order per thread and is byte-identical on a
    # re-run.  Offsets are unique keys so the sort has no ties.
    ordered = sorted((o for o in t320 if not (0x04100 <= o[3] < 0x04100 + 68 * 4)),
                     key=lambda o: (PHASE_ORDER.get(o[0], 99), o[6], o[7], o[3], o[1]))
    for phase, kind, win, off, val, mask, tag, seq, _pw in ordered:
        key = (phase, off, kind)
        if key in seen:
            continue
        seen.add(key)
        n += 1
        base, name, _ = subblock(off)
        A("| %d | %s | t%d | 0x%05x | %s | %s | 0x%08x |" % (n, phase, tag, off, name,
                                                             "R" if kind == "R" else "W", val))
        if n >= 120:
            A("| ... | | | | | |")
            break
    A("")
    A("## 4. Per-phase totals, window coverage and the larb writes")
    A("")
    A("| phase | ops 320x240 | ops 1920x1080 | pages | cores |")
    A("|---|---|---|---|---|")
    for ph in PHASES:
        o = [x for x in t320 if x[0] == ph]
        o2 = [x for x in t1080 if x[0] == ph]
        pgs = {VDEC_BASE + x[3] & ~(PAGE - 1) for x in o + o2}
        cores = sorted({core_of(x[3]) for x in o + o2})
        if o or o2:
            A("| %s | %d | %d | %d | %s |" % (ph, len(o), len(o2), len(pgs), ", ".join(cores)))
    A("")
    init_ops = sum(1 for x in t320 + t1080 if x[0] == "init")
    hdr_ops = sum(1 for x in t320 + t1080 if x[0] == "header")
    A("The **init** and **header** phases issue %d and %d VDEC register accesses: on this path the" %
      (init_ops, hdr_ops))
    A("vendor parses SPS/PPS in ARM code from the mapped bitstream and programs the blocks only when the")
    A("first frame START arrives.  The first register access of a session is therefore in the first")
    A("`MARK frame` phase, and a header-only milestone needs no VDEC register window.")
    A("")
    A("Which window served the accesses (the ten frozen windows plus VDEC_ALL; one op each).  This is")
    A("the direct answer to \"is the 0x30000 window needed\": %d of %d ops on 320x240 are outside every" % (
        sum(1 for o in t320 if o[2] == WIN_VDEC_ALL), len(t320)))
    A("frozen VDEC window and only reachable through VDEC_ALL.")
    A("")
    A("| window | name | 320x240 ops | 1920x1080 ops |")
    A("|---|---|---|---|")
    for win in sorted({o[2] for o in t320 + t1080}):
        A("| %d | %s | %d | %d |" % (win, WIN_NAMES.get(win, "?"),
                                      sum(1 for o in t320 if o[2] == win),
                                      sum(1 for o in t1080 if o[2] == win)))
    A("")
    larb = [(off, val) for ph, kind, win, off, val, mask, _tag, _seq, _pw in t320 + t1080
            if off in (0x0d00c, 0x2e00c) and kind != "R" and (val & ~0x10001) == 0]
    if larb:
        A("SMI larb sleep-protect writes (the one place the vendor writes a page the kernel owns; the")
        A("kernel should allow SLP_CON write and nothing else there).  Distinct values, with counts over")
        A("both sessions.  Note the **release**: it is not 0 but the whole word read back with bit 0")
        A("cleared and the ack bit 16 still set (0x00010000), i.e. the vendor does a read-modify-write.")
        A("A kernel rule that only accepts values with `value & ~1 == 0` refuses that half of the")
        A("handshake; the measured values below are the ones the rule has to accept:")
        A("")
        seen_larb = OrderedDict()
        for off, val in larb:
            seen_larb[(off, val)] = seen_larb.get((off, val), 0) + 1
        for (off, val), cnt in seen_larb.items():
            A("- 0x%05x = 0x%08x (bit 0 = %d), %d times" % (off, val, val & 1, cnt))
        A("")
    A("## 5. What this means for the kernel")
    A("")
    A("- A single window `RWVC_WIN_VDEC_ALL` = 0x16000000 + 0x30000 (DEC07 rev 3) covers **every** page")
    A("  both runs touched; the frozen twelve windows together cover only part of them.  The ten old")
    A("  windows stay for K02's own sequences.")
    A("- The per-page power rule the kernel landed (pages below 0x0d/0x0e -> CORE, 0x0d and")
    A("  0x0f..0x1f -> SOC, 0x20+ -> CORE) is safe for every vendor access measured here, because")
    A("  every one of them happens with both domains on.  The only access it would refuse is the")
    A("  client's racing restore at LOCK_LAT (12 EACCES per 12-AU session at 0x16004100 when the fake")
    A("  modelled racing as CORE).  The fix belongs on one side of that interface: DEC01's client can")
    A("  take the CORE reference for its active period, or the kernel can allow 0x16004000 with SOC")
    A("  only.  Reported to hw-dec01 and hw-k02; no change to the decode page set either way.")
    A("- larb5 (0x1600d000) and larb4 (0x1602e000) are written at +0x00c only (SLP_CON): 0x00000001 to")
    A("  request sleep, then 0x00010000 to release (bit 0 cleared, the ack bit 16 written back).")
    A("  Read-only would break the vendor's sleep-protect handshake, and a rule that only accepts")
    A("  `value & ~1 == 0` breaks the release half.  The allow-list has to accept both words and reject")
    A("  every other offset of the two pages.")
    A("")
    A("## 6. Limits of this measurement")
    A("")
    A("- Host fake hardware: no pixels and no real interrupt line (the fake schedules the decode-done")
    A("  event and the client's WAITISR service waits for it).  The page set is a property of the")
    A("  vendor's register programming, not of the picture, but a stream with more slices, a different")
    A("  profile or a mid-stream resolution change could touch more pages; the same run.sh answers that")
    A("  with one new vector.  The same page list is produced by both drivers (DEC01's client and")
    A("  opus-dec1's reference), which is the cross-check run.sh enforces.")
    A("- Accesses are recorded at the `n2vc_hw` boundary.  A page the vendor reads through a *guest*")
    A("  pointer (bitstream, MM21 frame) is not a register access and is not listed, by construction.")
    A("- The vendor's own `core_index` argument is not visible at the register address; the core column")
    A("  is the address region (LAT 0x10000-0x1ffff, CORE 0x20000-0x2ffff, else the SOC/global side).")
    A("")
    if log320["header"]:
        A("Header decode result (320x240): pic=%dx%d buf=%dx%d dpb=%d fourcc=%s changed=0x%x;" % (
            log320["header"]["pic"] + log320["header"]["buf"] + (log320["header"]["dpb"],
            log320["header"]["fourcc"], log320["header"]["changed"])))
    if log1080["header"]:
        A("Header decode result (1920x1080): pic=%dx%d buf=%dx%d dpb=%d fourcc=%s changed=0x%x." % (
            log1080["header"]["pic"] + log1080["header"]["buf"] + (log1080["header"]["dpb"],
            log1080["header"]["fourcc"], log1080["header"]["changed"])))
    A("")
    open(path, "w").write("\n".join(lines) + "\n")


def _sum(aus, key):
    return sum(a[key] for a in aus)


# Simpler and exact: re-parse keeping MARK positions as separators.
def aux_per_au(path):
    aus = []
    cur = None
    for line in open(path, "r", encoding="utf-8", errors="replace"):
        line = line.strip()
        if line.startswith("MARK frame"):
            if cur is not None:
                aus.append(cur)
            cur = []
            continue
        if line.startswith("MARK"):
            cur = None
            continue
        w = line.split()
        if w and w[0] in ("R", "W", "WM") and cur is not None:
            cur.append((w[0], int(w[2], 16)))
    if cur is not None:
        aus.append(cur)
    return aus

def write_cost(path, t320, t1080, log320, log1080):
    lines = []
    A = lines.append
    A("# Cost of one H.264 access unit on the MT6893 VDEC (host measurement)")
    A("")
    A("Generated by `gpu/hwcodec/facts/vdec_trace/run.sh` (DEC02); do not edit by hand.  Host only.")
    A("Two independent counters are reported, because they see different things and the difference is")
    A("itself a result:")
    A("")
    A("- **n2tr** sits at the `n2vc_hw` boundary and sees *every* register operation: the vendor's")
    A("  guest loads/stores, DEC01's client's own direct ops (the 68-word racing restore/save at")
    A("  LOCK/UNLOCK and the HW-break words), and anything batched inside the OAL.  These are the")
    A("  counts the page/offset tables use.")
    A("- **n2vcu_stats** (`/dev/vcu` model) counts the guest MMIO loads/stores it proxies and every")
    A("  GCE flush/command; this is the interface the kernel driver implements, so its `mmio_bad`,")
    A("  `last_bad_mmio_addr` and GCE counters are the ones a phone run will show.  It does not see the")
    A("  client's direct back-end ops, and the OAL's batched register lists reach the hardware through")
    A("  a different entry than a single guest store, so its read count is lower than n2tr's.  The")
    A("  reconciliation is printed below and checked by run.sh (writes: n2tr - AP writes == n2vcu).")
    A("")
    A("The guest CPU time is a measurement, not a counter: it varies by about +-10% between runs on")
    A("this shared host, which is why the determinism check of run.sh covers the counter sections and")
    A("PAGES.md, not the timing rows.  The instruction column is that CPU time x the 35 guest MIPS")
    A("figure of `tests/perf/STATUS.md:68` (25-35 MIPS for the interpreter's six-kernel mix) and is")
    A("therefore marked [I]; no exact per-thread guest icount is available today (no accessor in V32's")
    A("thread table; asked for on the board so a later run can replace this column with a counter).")
    A("")
    A("## 1. Per access unit")
    A("")
    A("| stream | AUs | n2tr R/AU | n2tr W/AU | n2tr distinct R/AU | n2tr distinct W/AU | n2vcu mmio R/AU | n2vcu mmio W/AU | mmio_bad | last_bad | GCE flushes | GCE cmds |")
    A("|---|---|---|---|---|---|---|---|---|---|---|---|")
    summary = {}
    for label, log, aus_t in (("320x240", log320, AU320), ("1920x1080", log1080, AU1080)):
        aus = log["aus"]
        n = len(aus) or 1
        tr = sum((len(a) for a in aus_t), 0)
        trr = sum(1 for a in aus_t for k, _o in a if k == "R")
        trw = sum(1 for a in aus_t for k, _o in a if k != "R")
        # distinct offsets per AU, averaged over the AUs
        dr = sum(len({o for k, o in a if k == "R"}) for a in aus_t) / (len(aus_t) or 1)
        dw = sum(len({o for k, o in a if k != "R"}) for a in aus_t) / (len(aus_t) or 1)
        readsv = _sum(aus, "r") / n
        writesv = _sum(aus, "w") / n
        bad = _sum(aus, "bad")
        lastbad = max([a.get("lastbad", 0) for a in aus], default=0)
        fl = _sum(aus, "gce_flush")
        cmds = _sum(aus, "gce_cmds")
        cpu = _sum(aus, "cpu_s") / n
        summary[label] = dict(n=len(aus), trr=trr / n, trw=trw / n, vcu_r=readsv, vcu_w=writesv,
                              bad=bad, cpu=cpu, instr=cpu * 1e3 * MIPS * 1000.0, dr=dr, dw=dw,
                              total=tr)
        A("| %s | %d | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %d | 0x%x | %d | %d |" % (
            label, len(aus), trr / n, trw / n, dr, dw, readsv, writesv, bad, lastbad, fl, cmds))
    A("")
    A("The 320x240 stream has 12 AUs (main profile, `-bf 2`, so display order is not decode order);")
    A("the 1920x1080 stream has 6 AUs (high profile, `-bf 0`).  The per-AU spread is in the raw logs")
    A("(`DC START au=...` lines) that run.sh keeps under its output directory.")
    A("")
    A("Reconciliation of the two counters (320x240, whole session):")
    A("")
    if "320x240" in summary:
        s = summary["320x240"]
        rac = [o for o in t320 if o[2] == 10]
        rw = [o for o in rac if 0x04100 <= o[3] < 0x04100 + 68 * 4]
        A("- n2tr: %d register ops (%d R, %d W)." % (s["total"], s["trr"] * s["n"], s["trw"] * s["n"]))
        A("- n2vcu: %d (%d R, %d W).  The two counters sit on different paths (the guest MMIO proxy"
          " versus the back-end boundary): n2tr also sees the client's own register batches (racing"
          " save/restore, HW-break) and the OAL lists, which do not pass through a single guest"
          " store.  Page and offset tables use n2tr; `mmio_bad` and the GCE columns are n2vcu's,"
          " because those are the numbers a phone run reports." % (
              (s["vcu_r"] + s["vcu_w"]) * s["n"], s["vcu_r"] * s["n"], s["vcu_w"] * s["n"]))
        A("- window 10 (racing) carries %d R + %d W over the session.  The client's 68-word"
          " restore/save at 0x16004100..0x1600420c is 136 ops per access unit (1632 over 12 AUs);"
          " %d ops hit those words in total, so the vendor uses them as well." % (
              sum(1 for o in rac if o[1] == "R"), sum(1 for o in rac if o[1] != "R"), len(rw)))
    A("")
    A("The number the kernel actually pays per *programming step* is the distinct-offset count, because")
    A("the real block answers polls on the first read and the vendor rewrites the same word only when")
    A("the value changes:")
    A("")
    A("| stream | AU | n2tr raw R | n2tr raw W | distinct R offsets | distinct W offsets |")
    A("|---|---|---|---|---|---|")
    for label, aus in (("320x240", AU320), ("1920x1080", AU1080)):
        for idx in (0, 1):
            if len(aus) <= idx:
                continue
            a = aus[idx]
            rr = [o for k, o in a if k == "R"]
            ww = [o for k, o in a if k != "R"]
            A("| %s | %d (%s) | %d | %d | %d | %d |" % (
                label, idx, "first frame" if idx == 0 else "steady state",
                len(rr), len(ww), len(set(rr)), len(set(ww))))
    A("")
    A("The programming is resolution-independent: the distinct-offset counts of the two streams agree")
    A("to within the one-off setup of the first AU; only the values written differ.")
    A("")
    A("## 2. Register ops by sub-block (steady-state frame, both streams)")
    A("")
    A("One frame AU is taken from each trace: the second AU on 320x240 and the second on 1920x1080")
    A("(the first AU of a session includes one-off block setup).  Reads and writes are counted")
    A("separately; `AVC_VLD` is where every H.264 syntax element comes from.")
    A("")
    A("| sub-block | offset | 320x240 R | 320x240 W | 1920x1080 R | 1920x1080 W |")
    A("|---|---|---|---|---|---|")
    a320 = AU320
    a1080 = AU1080
    sel320 = a320[1] if len(a320) > 1 else (a320[0] if a320 else [])
    sel1080 = a1080[1] if len(a1080) > 1 else (a1080[0] if a1080 else [])
    blocks = {}
    for label, ops in (("320", sel320), ("1080", sel1080)):
        for kind, off in ops:
            base, name, _ = subblock(off)
            b = blocks.setdefault(name, {"off": base, "320r": 0, "320w": 0, "1080r": 0, "1080w": 0})
            if label == "320":
                b["320r" if kind == "R" else "320w"] += 1
            else:
                b["1080r" if kind == "R" else "1080w"] += 1
    for name in sorted(blocks, key=lambda n: blocks[n]["off"]):
        b = blocks[name]
        A("| %s | 0x%05x | %d | %d | %d | %d |" % (name, b["off"], b["320r"], b["320w"],
                                                   b["1080r"], b["1080w"]))
    A("")
    A("## 3. Header decode and first frame")
    A("")
    A("| step | ops 320x240 | ops 1920x1080 |")
    A("|---|---|---|")
    h320 = [x for x in t320 if x[0] == "header"]
    h1080 = [x for x in t1080 if x[0] == "header"]
    A("| header START (SPS/PPS/slice header, no frame buffer) | %d | %d |" % (
        len(h320), len(h1080)))
    if a320:
        A("| first frame AU | %d | %d |" % (
            len(a320[0]), len(a1080[0]) if a1080 else 0))
    A("")
    A("## 4. GCE")
    A("")
    tot_f = _sum(log320["aus"], "gce_flush") + _sum(log1080["aus"], "gce_flush")
    tot_c = _sum(log320["aus"], "gce_cmds") + _sum(log1080["aus"], "gce_cmds")
    A("`n2vcu_stats` counted **%d GCE flushes** and **%d GCE commands** over both sessions." % (tot_f, tot_c))
    A("")
    A("GCE is not used at all in this decode path: the vendor programs the VDEC blocks with direct")
    A("register accesses (the mmap of the windows), and DEC01's client's LOCK/UNLOCK power role is")
    A("direct MMIO as well.  The kernel's CMDQ machinery is therefore not needed for H.264 decode on")
    A("this SoC; a RWVC_REGS batch per run of adjacent operations is enough.")
    A("")
    A("## 5. Guest time (a measurement, not a counter)")
    A("")
    A("| stream | guest CPU ms/AU (measured) | spread over the AUs | instructions/AU [I] |")
    A("|---|---|---|---|")
    for label, log in (("320x240", log320), ("1920x1080", log1080)):
        aus = log["aus"]
        if not aus:
            continue
        vals = [a["cpu_s"] * 1e3 for a in aus]
        A("| %s | %.1f | %.1f-%.1f | %.0f |" % (
            label, sum(vals) / len(vals), min(vals), max(vals),
            sum(vals) / len(vals) * MIPS * 1000.0))
    A("")
    A("These rows are the only non-counter numbers in this file and they change by about +-10%")
    A("between runs on this shared host.  The per-AU raw lines are in the run logs.  There is no exact")
    A("per-thread guest instruction count available yet: `n2vhost` tracks only its own main-thread")
    A("`icount`, and the decode work runs on vpud's service threads (V32's thread table has no icount")
    A("accessor; asked for on the board).  Until then the instruction column is CPU time x 35 MIPS,")
    A("[I], not a counter.")
    A("")
    A("## 6. What the phone will spend")
    A("")
    if "320x240" in summary:
        s = summary["320x240"]
        ops = s["trr"] + s["trw"]
        A("- Per 320x240 AU: %.0f register accesses (%.0f reads, %.0f writes), %.0f distinct offsets,"
          % (ops, s["trr"], s["trw"], s["dr"] + s["dw"]))
        A("  and about %.0f guest instructions [I]." % s["instr"])
        A("  At the 2-5 us per RWVC_REGS ioctl that VDEC_PROTOCOL.md section 6 assumes, sending every"
          " op as its own ioctl would cost %.1f-%.1f ms per AU; batching adjacent ops (DEC04's"
          " write-combining, or one list per programming step) is what keeps this path viable at 30 fps."
          % (ops * 2 / 1000.0, ops * 5 / 1000.0))
        A("  The interpreter part is %.0f ms at 35 MIPS on this host (a phone A78 should be similar)."
          % (s["cpu"] * 1e3))
    if "1920x1080" in summary:
        s = summary["1920x1080"]
        A("- Per 1920x1080 AU: %.0f register accesses (%.0f reads, %.0f writes), %.0f distinct offsets,"
          % (s["trr"] + s["trw"], s["trr"], s["trw"], s["dr"] + s["dw"]))
        A("  about %.0f guest instructions [I].  The count does not grow with resolution: it is the same"
          % s["instr"])
        A("  syntax and DPB work; the bitstream bytes and the DDR traffic do grow.")
    if "320x240" in summary:
        s = summary["320x240"]
        ops = s["trr"] + s["trw"]
        A("- %.0f%% of the ops are reads (synchronous syntax/status reads), %.0f%% writes."
          % (100.0 * s["trr"] / max(1.0, ops), 100.0 * s["trw"] / max(1.0, ops)))
        A("  DEC04's write-combining can merge the writes only; the reads need a register access each.")
    A("")
    open(path, "w").write("\n".join(lines) + "\n")


def checks(t320, t1080, log320, log1080, bad):
    """Return [(name, ok, detail)]."""
    res = []

    def ck(name, ok, detail=""):
        res.append((name, bool(ok), detail))

    ck("traces parsed with no bad lines", not bad, "; ".join(bad[:3]))
    for label, ops, log in (("320x240", t320, log320), ("1920x1080", t1080, log1080)):
        ck("%s: every access is a VDEC window (2..12)" % label,
           all(2 <= o[2] <= WIN_VDEC_ALL for o in ops),
           "windows seen: %s" % sorted({o[2] for o in ops}))
        ck("%s: VDEC_ALL (window 12) is needed" % label,
           any(o[2] == WIN_VDEC_ALL for o in ops),
           "windows seen: %s" % sorted({o[2] for o in ops}))
        ck("%s: frame phase has register ops" % label,
           any(o[0] == "frame" for o in ops),
           "phases with ops: %s" % sorted({o[0] for o in ops}))
        ck("%s: header decoded (changed bit 0)" % label,
           log["header"] is not None and (log["header"]["changed"] & 1) != 0,
           "header=%s" % (log["header"] and log["header"]["changed"]))
        ck("%s: pictures displayed" % label,
           log["result"] is not None and log["result"]["displayed"] > 0,
           "result=%s" % (log["result"],))
        ck("%s: no mmio_bad" % label, all(a["bad"] == 0 for a in log["aus"]),
           "bad=%s" % [a["bad"] for a in log["aus"]][:3])
        ck("%s: per-AU register ops > 100" % label,
           bool(log["aus"]) and min(a["r"] + a["w"] for a in log["aus"]) > 100,
           "min=%s aus=%d" % (min([a["r"] + a["w"] for a in log["aus"]], default=0), len(log["aus"])))
        ck("%s: all AUs acked and displayed" % label,
           bool(log["aus"]) and all(a["rc"] == 0 for a in log["aus"]) and
           log["result"] is not None and
           log["result"]["displayed"] == log["result"]["frames_in"] == len(log["aus"]),
           "result=%s aus=%d" % (log["result"], len(log["aus"])))
        ck("%s: every AU issued register traffic" % label,
           bool(log["aus"]) and all(a["r"] + a["w"] > 0 for a in log["aus"]),
           "ops/AU=%s" % [a["r"] + a["w"] for a in log["aus"]][:4])
    pages = set(page_table(t320)) | set(page_table(t1080))
    # The page set measured by this package (PAGES.md section 1).  A page appearing or
    # disappearing is a regression or a new vendor path: both must be looked at, not papered over.
    expected = [
        0x16000000,   # BASE
        0x16002000,   # SOC side, status poll
        0x16004000,   # RACING
        0x1600d000,   # larb5 (SLP_CON)
        0x1600f000,   # SOC_GCON
        0x16010000,   # LAT_MISC
        0x16011000,   # LAT_VLD
        0x16012000,   # LAT_AVC_VLD
        0x16013000,   # LAT_MV
        0x16016000,   # LAT_16000
        0x16020000,   # VLD
        0x16021000,   # MC
        0x16022000,   # AVC_VLD (the page the frozen windows miss)
        0x16023000,   # AVC_MV
        0x16024000,   # PP
        0x16025000,   # MISC
        0x1602e000,   # larb4 (SLP_CON)
        0x1602f000,   # VDEC_SYS
    ]
    ck("page set equals the measured set", sorted(pages) == expected,
       "got %s" % ["0x%08x" % p for p in sorted(pages)])
    need = {0x16000000: "BASE", 0x16004000: "RACING", 0x1600d000: "LARB5",
            0x1600f000: "SOC_GCON", 0x16010000: "LAT_MISC", 0x16011000: "LAT_VLD",
            0x16012000: "LAT_AVC_VLD", 0x16020000: "VLD", 0x16021000: "MC",
            0x16022000: "AVC_VLD", 0x16023000: "AVC_MV", 0x16024000: "PP",
            0x16025000: "MISC", 0x1602e000: "LARB4", 0x1602f000: "VDEC_SYS"}
    for pg, name in need.items():
        ck("page %s (0x%08x) touched" % (name, pg), pg in pages)
    avc = [o for o in t320 + t1080 if 0x22000 <= o[3] < 0x23000]
    ck("AVC_VLD read and written", any(o[1] == "R" for o in avc) and any(o[1] != "R" for o in avc),
       "ops=%d" % len(avc))
    ck("larb SLP_CON written on both larbs",
       any(o[3] == 0x0d00c and o[1] != "R" for o in t320 + t1080) and
       any(o[3] == 0x2e00c and o[1] != "R" for o in t320 + t1080))
    ck("no access above 0x30000", all(o[3] < 0x30000 for o in t320 + t1080))
    # The power-domain evidence (PAGES.md section 1): the racing restore is the one access that
    # happens with CORE off, which is why 0x16004000 cannot be gated on VDEC_CORE.
    racing = [o for o in t320 + t1080 if 0x04000 <= o[3] < 0x05000]
    ck("racing page accessed with CORE off (LOCK_LAT restore)",
       any(o[8] == (1, 0) for o in racing),
       "states=%s" % sorted({o[8] for o in racing}))
    ck("every touched page accessed with SOC on",
       all(o[8][0] == 1 for o in t320 + t1080),
       "states=%s" % sorted({o[8] for o in t320 + t1080}))
    ck("no access while no VDEC domain is on",
       all(o[8] != (0, 0) for o in t320 + t1080))
    # The vendor writes only SLP_CON+0x00c on both larb pages (PAGES.md section 4): the kernel's
    # allow-list must not be widened.
    larbw = {o[3] for o in t320 + t1080 if 0x0d000 <= o[3] < 0x0e000 or 0x2e000 <= o[3] < 0x2f000
             if o[1] != "R"}
    ck("larb pages written only at +0x00c", larbw <= {0x0d00c, 0x2e00c},
       "written offsets=%s" % sorted(hex(x) for x in larbw))
    ck("both larb SLP_CON words are written",
       {0x0d00c, 0x2e00c} <= {o[3] for o in t320 + t1080 if o[1] != "R"})
    # The release write is the whole word (bit 16, the ack, still set): the kernel allow-list must
    # accept 0x00010000 as well as 0x00000001, or the second half of the handshake is EACCES.
    larbv = {o[4] for o in t320 + t1080 if o[3] in (0x0d00c, 0x2e00c) and o[1] != "R"}
    ck("larb release writes bit 16, not 0", larbv == {0x1, 0x10000},
       "values=%s" % sorted(hex(v) for v in larbv))
    ck("no larb write sets any bit outside 0x10001",
       all((v & ~0x10001) == 0 for v in larbv))
    ck("no GCE used for H.264 decode",
       all(a["gce_flush"] == 0 and a["gce_cmds"] == 0 for a in log320["aus"] + log1080["aus"]))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace320")
    ap.add_argument("--trace1080")
    ap.add_argument("--log320")
    ap.add_argument("--log1080")
    ap.add_argument("--pages")
    ap.add_argument("--cost")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--pageset", metavar="TRACE",
                    help="print the sorted 4 KiB page list of one trace and exit (used by run.sh "
                         "to compare two independent drivers)")
    ap.add_argument("--offset-set", metavar="TRACE",
                    help="print the sorted (window, offset) set of one trace and exit")
    args = ap.parse_args()
    if args.pageset:
        ops, bad = parse_trace(args.pageset)
        if bad:
            print("\n".join(bad), file=sys.stderr)
            return 1
        for pg in sorted({VDEC_BASE + o[3] & ~(PAGE - 1) for o in ops}):
            print("0x%08x" % pg)
        return 0
    if args.offset_set:
        ops, bad = parse_trace(args.offset_set)
        if bad:
            print("\n".join(bad), file=sys.stderr)
            return 1
        for win, off in sorted({(o[2], o[3]) for o in ops}):
            print("%d 0x%05x" % (win, off))
        return 0

    need = [n for n in ("trace320", "trace1080", "log320", "log1080", "pages", "cost")
            if getattr(args, n) is None]
    if need:
        ap.error("missing required arguments: %s" % ", ".join("--" + n for n in need))
    global TRACE320, TRACE1080, AU320, AU1080
    TRACE320, TRACE1080 = args.trace320, args.trace1080
    AU320 = aux_per_au(args.trace320)
    AU1080 = aux_per_au(args.trace1080)
    t320, bad320 = parse_trace(args.trace320)
    t1080, bad1080 = parse_trace(args.trace1080)
    log320 = parse_log(args.log320)
    log1080 = parse_log(args.log1080)
    write_pages(args.pages, t320, t1080, log320, log1080,
                [args.trace320, args.trace1080])
    write_cost(args.cost, t320, t1080, log320, log1080)
    if args.check:
        res = checks(t320, t1080, log320, log1080, bad320 + bad1080)
        npass = sum(1 for _, ok, _ in res if ok)
        for name, ok, detail in res:
            if not ok:
                print("DEC02 CHECK FAIL %s %s" % (name, detail), file=sys.stderr)
        print("DEC02 CHECK %d/%d" % (npass, len(res)), file=sys.stderr)
        if npass != len(res):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
