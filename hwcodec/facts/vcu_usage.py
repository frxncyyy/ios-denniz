#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""facts/vcu_usage.py -- what the vendor codec objects do with /dev/vcu, /dev/vpu and mmap.

For libvpud_vcodec.so and libvcodec_utility.so (the two objects that talk to the kernel) this prints,
for every call to ioctl(), mmap(), mmap64(), open()/__open_2():

  * the call site and the enclosing function,
  * the value of r1 (ioctl request / mmap length) when it is a literal built in the same block
    (movw+movt, movs, or a PC-relative literal word), decoded with the _IOC() layout,
  * for mmap, the r0/r1/r2/r3 arguments when literal, and the fd/offset stack slots when visible,
  * for open, the path string (PC-relative literal + add pc).

Output is a plain table; facts/vcu_usage.md holds the interpretation and the comparison with
nord2-current-kernel/include/uapi/linux/mtk_vcu_controls.h and mtk-vcu/mtk_vcu.c mtk_vcu_mmap().

Usage: python3 gpu/hwcodec/facts/vcu_usage.py [--object NAME] [--json OUT]
Deterministic, read-only.
"""

import argparse
import json
import os
import re
import subprocess
import sys

OBJECTS = ["lib/libvpud_vcodec.so", "lib/libvcodec_utility.so", "lib/libvcodecdrv.so"]
HERE = os.path.dirname(os.path.abspath(__file__))
MATTR = "+neon,+vfp4,+fp-armv8,+hwdiv,+hwdiv-arm"
WANT = ("ioctl", "mmap", "mmap64", "open", "__open_2")

LINE_RE = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{4}(?:[0-9a-f]{4}| [0-9a-f]{4})?)\s+(\S+)(?:\s+(.*))?$")
LABEL_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:")
CALL_RE = re.compile(r"^(?:bl|blx)\s+0x[0-9a-f]+\s+<([A-Za-z0-9_@.]+)>")
LIT_RE = re.compile(r"@\s*0x([0-9a-f]+)")
IMM_RE = re.compile(r"#(-?0x[0-9a-f]+|-?\d+)")


def vendor_dir():
    return os.environ.get("N2HW_VENDOR_DIR") or os.path.join(
        os.path.abspath(os.path.join(HERE, "..", "..", "..")),
        "gpu", "build", "cam-vendor-20261109", "vfs")


def run(cmd):
    try:
        return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit("vcu_usage.py: %s failed: %s" % (" ".join(cmd[:2]), e))


class Elf:
    """vaddr -> file offset mapping and literal/string reads."""

    SEC_RE = re.compile(r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)")

    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        self.secs = []
        for line in run(["llvm-readelf", "-S", "-W", path]).splitlines():
            m = self.SEC_RE.match(line)
            if m:
                self.secs.append((int(m.group(3), 16), int(m.group(5), 16), int(m.group(4), 16), m.group(1)))

    def off(self, vaddr):
        for addr, size, off, _ in self.secs:
            if addr <= vaddr < addr + size and size:
                return off + (vaddr - addr)
        return None

    def u32(self, vaddr):
        o = self.off(vaddr)
        if o is None or o + 4 > len(self.data):
            return None
        return int.from_bytes(self.data[o:o + 4], "little")

    def cstr(self, vaddr):
        o = self.off(vaddr)
        if o is None:
            return None
        end = self.data.find(b"\0", o)
        if end < 0 or end - o > 200:
            return None
        s = self.data[o:end]
        if all(32 <= c < 127 for c in s) and s:
            return s.decode()
        return None


def ioc(n):
    return {"dir": (n >> 30) & 3, "size": (n >> 16) & 0x3fff,
            "type": chr((n >> 8) & 0xff), "nr": n & 0xff}


# Expected /dev/vcu numbers, from include/uapi/linux/mtk_vcu_controls.h.  _IOC(dir, type, nr, size) with
# _IOW = dir 1, _IOWR = dir 3, type 'v', and the 32-bit ARM (AAPCS, 8-byte aligned u64) struct sizes:
#   share_obj 0x58 (observed in the binary), mem_obj 0x20, gce_cmdq_obj 0x20, gce_obj 0x10, map_obj 0x10
VCU_EXPECTED = {
    0x40587600: "VCU_SET_OBJECT (_IOW('v',0,share_obj) size 0x58)",
    0xc058760a: "VCU_GET_OBJECT (_IOWR('v',10,share_obj) size 0x58)",
    0xc0207601: "VCU_MVA_ALLOCATION (_IOWR('v',1,mem_obj) size 0x20)",
    0xc0207602: "VCU_MVA_FREE",
    0xc0207603: "VCU_CACHE_FLUSH_ALL",
    0xc0207604: "VCU_CACHE_FLUSH_BUFF",
    0xc0207605: "VCU_CACHE_INVALIDATE_BUFF",
    0xc0207606: "VCU_PA_ALLOCATION (_IOWR('v',6,mem_obj) size 0x20)",
    0xc0207607: "VCU_PA_FREE",
    0x40207608: "VCU_GCE_SET_CMD_FLUSH (_IOW('v',8,gce_cmdq_obj) size 0x20)",
    0x40107609: "VCU_GCE_WAIT_CALLBACK (_IOW('v',9,gce_obj) size 0x10)",
    0x4004760b: "VCU_GET_LOG_OBJECT",
    0x4004760c: "VCU_SET_LOG_OBJECT",
    0x4010760d: "VCU_SET_MMAP_TYPE (_IOW('v',13,map_obj) size 0x10)",
}


def classify_other(n):
    """Non-VCU ioctls seen in the same objects: report magic/nr/size only."""
    c = ioc(n)
    return "type '%s' nr 0x%x size 0x%x dir %d" % (c["type"], c["nr"], c["size"], c["dir"])


def disasm(path):
    out = run(["llvm-objdump", "-d", "--triple=thumbv8a", "--mattr=" + MATTR, path])
    return out.splitlines()


def scan(obj, elf, lines):
    """All ioctl/mmap/open call sites with the literal arguments of the same block."""
    funcs = []
    cur = "?"
    results = []
    # index lines by address for backward scans
    idx = []
    for i, l in enumerate(lines):
        lm = LABEL_RE.match(l)
        if lm:
            cur = lm.group(2)
            continue
        m = LINE_RE.match(l)
        if m:
            idx.append((int(m.group(1), 16), i, m.group(3), (m.group(4) or "")))
    by_addr = {a: (i, mn, ops) for a, i, mn, ops in idx}

    for a, i, mn, ops in idx:
        cm = CALL_RE.match(mn + " " + ops)
        if not cm:
            continue
        callee = cm.group(1).split("@")[0]
        if callee not in WANT:
            continue
        # enclosing function: nearest label above
        func = "?"
        for j in range(i, -1, -1):
            lm = LABEL_RE.match(lines[j])
            if lm:
                func = lm.group(2)
                break
        args = {"r0": None, "r1": None, "r2": None, "r3": None}
        how = {}
        # walk back up to 80 lines, stop at a branch/call that is not our target (basic-block end)
        parts = {}
        pcadd = {}
        litval = {}
        j = i - 1
        steps = 0
        while j >= 0 and steps < 80:
            lm = LABEL_RE.match(lines[j])
            if lm:
                break
            m = LINE_RE.match(lines[j])
            j -= 1
            if not m:
                continue
            steps += 1
            ia = int(m.group(1), 16)
            imn = m.group(3)
            iops = (m.group(4) or "").split("@")[0].strip()
            if imn in ("b", "bx", "bl", "blx", "pop") or imn.startswith("b") and imn[1:] in (
                    "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"):
                break
            dm = re.match(r"^(r[0-3]),\s*(.*)$", iops)
            if not dm:
                continue
            reg, rhs = dm.group(1), dm.group(2)
            if reg in args and args[reg] is None:
                val = None
                p = parts.setdefault(reg, {"lo": None, "hi": None})
                if imn.startswith("movw") and IMM_RE.match(rhs):
                    p["lo"] = int(IMM_RE.match(rhs).group(1), 0) & 0xffff
                    if p["hi"] is not None:
                        val = (p["hi"] << 16) | p["lo"]
                elif imn.startswith("movt") and IMM_RE.match(rhs):
                    p["hi"] = int(IMM_RE.match(rhs).group(1), 0) & 0xffff
                    if p["lo"] is not None:
                        val = (p["hi"] << 16) | p["lo"]
                elif imn.startswith("mov") and IMM_RE.match(rhs):
                    val = int(IMM_RE.match(rhs).group(1), 0)
                elif imn.startswith("ldr") and "[pc" in rhs:
                    lm2 = LIT_RE.search(m.group(4) or "")
                    if lm2:
                        tgt = int(lm2.group(1), 16)
                        val = elf.u32(tgt)
                        litval[reg] = val
                        if reg in pcadd:
                            val = (pcadd[reg] + 4 + val) & 0xffffffff
                            how[reg] = "literal+pc"
                        else:
                            how[reg] = "literal@0x%x" % tgt
                elif rhs.startswith("pc"):
                    # add rX, pc: either ADR (with an immediate) or the second half of a PC-relative literal
                    imm = IMM_RE.search(rhs)
                    if imm:
                        val = (((ia + 4) & ~3) + int(imm.group(1), 0)) & 0xffffffff
                        how[reg] = "adr"
                    else:
                        pcadd[reg] = ia
                        if reg in litval:
                            val = (ia + 4 + litval[reg]) & 0xffffffff
                            how[reg] = "literal+pc"
                if val is not None:
                    args[reg] = val
        results.append({"object": os.path.basename(obj), "func": func, "addr": a, "call": callee,
                        "args": args, "how": how})
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--object")
    ap.add_argument("--json", metavar="OUT")
    args = ap.parse_args()
    vdir = vendor_dir()
    objs = OBJECTS
    if args.object:
        objs = [o for o in OBJECTS if os.path.basename(o) == args.object]

    all_res = []
    for rel in objs:
        path = os.path.join(vdir, rel)
        elf = Elf(path)
        lines = disasm(path)
        res = scan(os.path.basename(path), elf, lines)
        all_res.extend(res)
        print("== %s: %d call sites" % (os.path.basename(path), len(res)))
        for r in res:
            a = r["args"]
            extra = ""
            if r["call"] == "ioctl" and a["r1"] is not None:
                n = a["r1"]
                extra = " r1=0x%08x" % n
                if n in VCU_EXPECTED:
                    extra += "  %s" % VCU_EXPECTED[n]
                else:
                    extra += "  [%s]" % classify_other(n)
            elif a["r1"] is not None:
                extra = " r1=0x%x" % a["r1"]
            if r["call"] in ("open", "__open_2") and a["r0"] is not None:
                s = elf.cstr(a["r0"])
                if s:
                    extra += " path=%r" % s
            print("  %-24s 0x%-7x %-8s%s" % (r["func"][:24], r["addr"], r["call"], extra))
        print()

    if args.json:
        with open(args.json, "w") as f:
            json.dump(all_res, f, indent=1, sort_keys=True)
        print("wrote %s" % args.json)

    vcu = [r for r in all_res if r["call"] == "ioctl" and r["args"]["r1"] in VCU_EXPECTED]
    unknown = [r for r in all_res if r["call"] == "ioctl" and r["args"]["r1"] is not None
               and r["args"]["r1"] not in VCU_EXPECTED]
    print("%d call sites, %d with a decoded /dev/vcu request, %d other ioctl requests, "
          "%d with no literal request at the call site"
          % (len(all_res), len(vcu), len(unknown),
             len([r for r in all_res if r["call"] == "ioctl" and r["args"]["r1"] is None])))


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:      # piped into head/grep -q: not an error
        sys.exit(0)
