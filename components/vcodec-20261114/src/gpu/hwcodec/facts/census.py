#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""facts/census.py -- instruction census of the phone's vendor vcodec objects.

What it does, per object (bin/vpud, lib/libvpud_vcodec.so, lib/libvcodecdrv.so,
lib/libvcodec_utility.so, lib/libvcodec_oal.so):

  1. reads .text's address/size and every defined dynamic symbol from the ELF;
  2. disassembles the whole .text twice with llvm-objdump, once as ARMv8-A A32
     (--triple=armv8a) and once as Thumb-2 (--triple=thumbv8a), with
     --mattr=+neon,+vfp4,+fp-armv8,+hwdiv,+hwdiv-arm (the object build
     attributes say ARMv8-A, cortex-a55);
  3. walks control flow recursively from every exported function: an even
     symbol address enters in ARM state, an odd one in Thumb state (bit 0 set);
     direct B/B<c>/CBZ/CBNZ/BL keep the mode, BLX <imm> switches ARM<->Thumb,
     returns/indirect jumps (BX, LDR pc, POP {..,pc}, TBB/TBH, UDF) end a block;
  4. reports, per object: instruction count per mnemonic per mode, bytes of
     .text covered (%), and the addresses the walk could not decode.

The mnemonic class of each instruction (V10..V20 in work_packages.json) is
assigned from the mnemonic, with one encoding-level rule: on A32, cond == 0xF
marks the Advanced SIMD (NEON) space and everything else beginning with V is
VFP; CPDATA instructions are then split by mnemonic table.  Mnemonics that
exist in several classes (mostly the scalar NEON forms of three-register
instructions) are flagged in the shared list rather than guessed twice.

Usage:
  python3 gpu/hwcodec/facts/census.py                 # summary, all objects
  python3 gpu/hwcodec/facts/census.py --mnemonics      # every mnemonic per mode
  python3 gpu/hwcodec/facts/census.py --classes        # per V1x class, sorted
  python3 gpu/hwcodec/facts/census.py --undecodable    # full address list
  python3 gpu/hwcodec/facts/census.py --json OUT       # machine-readable
  python3 gpu/hwcodec/facts/census.py --library NAME   # one object

Deterministic, read-only; the vendor objects are read from the extracted
/vendor copy (N2HW_VENDOR_DIR overrides the path).  Nothing is written into
the work tree.
"""

import argparse
import array
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

OBJECTS = [
    "bin/vpud",
    "lib/libvpud_vcodec.so",
    "lib/libvcodecdrv.so",
    "lib/libvcodec_utility.so",
    "lib/libvcodec_oal.so",
]

HERE = os.path.dirname(os.path.abspath(__file__))
MATTR = "+neon,+vfp4,+fp-armv8,+hwdiv,+hwdiv-arm"

# ---------------------------------------------------------------- classes
# class ids; owner = the work package that implements the class function
CLASSES = [
    (0, "none", ""),
    (1, "V10", "A32 data processing, MOVW/MOVT, multiplies, MRS/MSR, CLZ"),
    (2, "V11", "A32 media: extend, bitfield, REV/RBIT, saturate, parallel, DIV, SMULxy"),
    (3, "V12", "A32 load/store, LDM/STM, exclusives"),
    (4, "V12", "A32 branches, system, hints, cp15"),
    (5, "V14", "T32 16-bit instructions"),
    (6, "V15", "T32 32-bit data processing"),
    (7, "V16", "T32 32-bit load/store"),
    (8, "V16", "T32 32-bit branches/system"),
    (9, "V17", "VFP"),
    (10, "V18", "NEON three registers, same length"),
    (11, "V19", "NEON shift by immediate, different lengths, scalar"),
    (12, "V20", "NEON two-register misc, VEXT/VTBL/VDUP, modified immediate"),
    (13, "V20", "NEON structure load/store (VLD1-4/VST1-4)"),
    (14, "?", "unclassified (listed in STATUS.md and on the board)"),
]
CLS = {name: i for i, name, _ in CLASSES}

# mnemonic -> class, for A32 non-SIMD.  Condition and 's' suffixes are stripped
# before lookup.  Coprocessor/VFP/NEON decisions are handled separately.
A32_DP = """
adc add and asr bic clz cmn cmp eor lsl lsr mov movw movt mrs msr mvn orr ror rrx rsb rsc sbc sub teq tst
mul mla mls umull umlal smull smlal
"""
A32_MEDIA = """
sxtb sxth sxtab sxtab16 sxtah uxtb uxth uxtab uxtab16 uxtah
rev rev16 revsh rbit bfc bfi ubfx sbfx ssat usat ssat16 usat16 pkhbt pkhtb sel
qadd qsub qdadd qdsub qadd8 qsub8 qadd16 qsub16 qasx qsax
uqadd8 uqsub8 uqadd16 uqsub16 uqasx uqsax shadd8 shsub8 shadd16 shsub16 shasx shsax
uadd8 uadd16 usub8 usub16 uasx usax sdiv udiv
smulbb smulbt smultb smultt smlabb smlabt smlatb smlatt smulwb smulwt smlawb smulwt
smlalbb smlalbt smlaltb smlaltt smlald smlsld smuad smusd smlad smlsd
usad8 usada8
"""
A32_MEM = """
ldr ldrb ldrh ldrsb ldrsh ldrd str strb strh strd
ldm ldmia ldmda ldmdb ldmib stm stmia stmda stmdb stmib push pop
ldrex ldrexb ldrexh ldrexd strex strexb strexh strexd
pld pli pldw
"""
A32_MISC = """
b bl blx bx bxj svc swi bkpt udf nop yield wfe wfi sev dbg dmb dsb isb
mrc mcr mrrc mcrr ldc stc cdp mcr2 mrc2 ldc2 stc2 cdp2
"""
T32_DP = """
adc add addw adr and asr asrs bic clz cmn cmp cps eor lsl lsls lsr lsrs mov movs movw movt mrs msr mvn muls
mul mla mls orn orr ror rors rrx rsb sbc sub subw teq tst
sxtb sxth uxtb uxth sxtab sxtah uxtab uxtah rev rev16 revsh rbit bfc bfi ubfx sbfx
umull umlal smull smlal umaal smmla smmls smuad smusd
ssat usat ssat16 usat16 pkhbt pkhtb sel qadd qsub qdadd qdsub qadd8 qsub8 qadd16 qsub16 qasx qsax
uqadd8 uqsub8 uqadd16 uqsub16 uqasx uqsax shadd8 shsub8 shadd16 shsub16 shasx shsax
uadd8 uadd16 usub8 usub16 uasx usax sdiv udiv
smulbb smulbt smultb smultt smlabb smlabt smlatb smlatt smulwb smulwt smlawb
usad8 usada8
"""
T32_MEM = """
ldr ldrb ldrh ldrsb ldrsh ldrd ldrbt ldrht ldrsbt ldrsht ldrt str strb strh strd strbt strht strt
ldm ldmia ldmdb stm stmia stmdb push pop ldrex ldrexb ldrexh ldrexd strex strexb strexh strexd
pld pli pldw
"""
T32_MISC = """
b bl blx bx bxj cbz cbnz svc bkpt udf nop yield wfe wfi sev dbg dmb dsb isb it itt ite ittt itte itet itee itttt ittte ittet ittee itett itete ittee
mrc mcr mrrc mcrr ldc stc cdp mrc2 mcr2 ldc2 stc2 cdp2 tbb tbh
"""

# NEON: three registers of the same length (V18)
NEON_3SAME = """
vadd vaddhn vaddl vaddw vand vbic vbif vbit vbsl vceq vcge vcgt vcle vclt
vdot veor vhadd vhsub vmax vmin vmla vmlal vmls vmlsl vmul vmla vmlal vmls vmlsl
vmull vorn vorr vpadd vpaddl vqadd vqdmulh vqdmlal vqdmlsl vqdmull vqmovn vqmovun
vqrdmulh vqsub vqrshrn vqrshrun vqshl vqshrn vqshrun vqsub vraddhn vrhadd vrshrn vrsubhn
vshl vsub vsubhn vsubl vsubw vtst vaba vabal vabdl vabd vabs vneg vqabs vqneg
vceqz vcgez vcgtz vclez vcltz vcnez vcadd vcmul vcmla vfma vfms vfnma vfnms vmlaq vmlsq
vmaxnm vminnm vpaddl vpadal vpmax vpmin vrndn vrnda vrndp vrndm vrnd vrndx
vrecpe vrecps vrsqrte vrsqrts vsqrt vdiv vmaxnm vminnm vsel
"""
# NEON: shifts by immediate, different-length, scalar (V19)
NEON_SHIFT = """
vshr vshl vsli vsri vshrn vqshrn vqshrun vrshrn vqrshrn vqrshrun vqshl vqshlu vshll vmovn vqmovn vqmovun
vshll vmovl vaddw vaddl vsubw vsubl vaddhn vraddhn vsubhn vrsubhn vmull vmlal vmlsl vqdmull vqdmlal vqdmlsl
vdup vdupq
"""
# NEON: misc, VEXT/VTBL/VDUP, modified immediate (V20); structure load/store separately
NEON_MISC = """
vabs vcls vclz vcnt vcvt vcvta vcvtm vcvtn vcvtp vcvtr vcvtz vcvtb vcvtt vget_lane vmaxnm vminnm
vmov vmovn vmrs vmsr vmvn vneg vpadal vpaddl vpmax vpmin vqabs vqneg vrecpe vrecps vrev16 vrev32 vrev64
vrsqrte vrsqrts vrintn vrinta vrintp vrintm vrintx vrintz
vshll vswp vtbl vtbx vtrn vuzp vzip vext vdup vld1 vld2 vld3 vld4 vst1 vst2 vst3 vst4
"""
# NEON structure load/store handled as its own class id
NEON_LDST = "vld1 vld2 vld3 vld4 vst1 vst2 vst3 vst4"


def words(s):
    return s.split()


def build_sets():
    s = {}
    for name, cls in (("dp", 1), ("media", 2), ("mem", 3), ("misc", 4),
                      ("t32dp", 6), ("t32mem", 7), ("t32misc", 8),
                      ("n3", 10), ("nshift", 11), ("nmisc", 12), ("nldst", 13)):
        s[name] = set(words(globals()[{"dp": "A32_DP", "media": "A32_MEDIA", "mem": "A32_MEM",
                                      "misc": "A32_MISC", "t32dp": "T32_DP", "t32mem": "T32_MEM",
                                      "t32misc": "T32_MISC", "n3": "NEON_3SAME",
                                      "nshift": "NEON_SHIFT", "nmisc": "NEON_MISC",
                                      "nldst": "NEON_LDST"}[name]]))
    return s


SETS = build_sets()

CONDS = {"eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc",
         "hi", "ls", "ge", "lt", "gt", "le", "al"}
VFP = """
vldr vstr vldm vldmia vldmdb vstm vstmia vstmdb vpush vpop
vmov vmrs vmsr
vadd vsub vmul vnmul vdiv vabs vneg vsqrt vcmp vcmpe
vcvt vcvtb vcvtt vcvtm vcvtn vcvtp vcvta vcvtr vcvtz
vmla vmls vnmla vnmls vfma vfms vfnma vfnms
vmaxnm vminnm vsel vrinta vrintn vrintp vrintm vrintx vrintz vjcvt
fconsts fconstd fmstat
"""


def strip_suffix(mn):
    """mnemonic -> (base, mode_suffix) ; strips .w/.n and the A32 cond/s suffixes when they are real."""
    suf = ""
    if mn.endswith(".w") or mn.endswith(".n"):
        suf, mn = mn[-2:], mn[:-2]
    return mn, suf


KNOWN_MNEMONICS = None


def strip_cond_s(b):
    """Strip an A32/T32 condition suffix and a flag-setting 's' when the remainder is a known mnemonic."""
    global KNOWN_MNEMONICS
    if KNOWN_MNEMONICS is None:
        KNOWN_MNEMONICS = (SETS["dp"] | SETS["media"] | SETS["mem"] | SETS["misc"] | SETS["t32dp"]
                           | SETS["t32mem"] | SETS["t32misc"] | SETS["n3"] | SETS["nshift"]
                           | SETS["nmisc"] | SETS["nldst"] | set(VFP.split()))
    known = KNOWN_MNEMONICS
    if b in known:
        return b
    if len(b) > 2 and b[-2:] in CONDS:
        r = b[:-2]
        if r in known:
            return r
        if r.endswith("s") and r[:-1] in known:
            return r[:-1]
    if b.endswith("s") and b[:-1] in known:
        return b[:-1]
    return b


def a32_base(mn):
    """Strip A32 condition and S suffix to find the base mnemonic (best effort, table driven)."""
    if mn in SETS["dp"] or mn in SETS["media"] or mn in SETS["mem"] or mn in SETS["misc"] or mn in SETS["n3"] \
            or mn in SETS["nshift"] or mn in SETS["nmisc"] or mn in SETS["nldst"] or mn in VFP:
        return mn, ""
    # trailing "s" of flag-setting forms
    if mn.endswith("s"):
        b = mn[:-1]
        if b in SETS["dp"] or b in SETS["media"] or b in SETS["mem"] or b in SETS["misc"]:
            return b, "s"
    if len(mn) > 2 and mn[-2:] in CONDS:
        b, cond = mn[:-2], mn[-2:]
        if b in SETS["dp"] or b in SETS["media"] or b in SETS["mem"] or b in SETS["misc"]:
            return b, cond
        if b.endswith("s"):
            b2 = b[:-1]
            if b2 in SETS["dp"] or b2 in SETS["media"] or b2 in SETS["mem"] or b2 in SETS["misc"]:
                return b2, cond + "s"
    return mn, ""


# ---------------------------------------------------------------- ELF access
def run(cmd):
    try:
        return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit("census.py: %s failed: %s" % (" ".join(cmd[:2]), e))


SECTION_RE = re.compile(r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)")


def text_range(path):
    for line in run(["llvm-readelf", "-S", "-W", path]).splitlines():
        m = SECTION_RE.match(line)
        if m and m.group(1) == ".text":
            return int(m.group(3), 16), int(m.group(5), 16), int(m.group(4), 16)
    sys.exit("census.py: %s: no .text" % path)


def sections(path):
    d = {}
    for line in run(["llvm-readelf", "-S", "-W", path]).splitlines():
        m = SECTION_RE.match(line)
        if m:
            d[m.group(1)] = (int(m.group(3), 16), int(m.group(4), 16), int(m.group(5), 16))
    return d


def exidx_functions(path):
    """Function start addresses from .ARM.exidx (EHABI prel31 entries).  This is the only function-start
    list that covers static functions in these stripped libraries: one exported symbol (cacheTestNeon in
    libvcodec_oal.so) even has size 0 and points 0x14 before its real prologue."""
    secs = sections(path)
    if ".ARM.exidx" not in secs:
        return []
    addr, off, size = secs[".ARM.exidx"]
    data = open(path, "rb").read()
    out = []
    for i in range(0, size - 7, 8):
        a = addr + i
        w0 = int.from_bytes(data[off + i:off + i + 4], "little")
        rel = w0 & 0x7fffffff
        if rel & 0x40000000:
            rel -= 0x80000000
        fn = (a + rel) & 0xffffffff
        if fn:
            out.append(fn)
    return out


SYM_RE = re.compile(r"^\s*(\d+):\s+([0-9a-f]+)\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*(.*)$")


def exports(path):
    """Every defined FUNC/IFUNC symbol of .dynsym with the raw st_value (bit 0 = Thumb entry), plus
    the ELF entry point.  llvm-nm is not used here: it masks bit 0 of st_value, which hides the mode."""
    out = run(["llvm-readelf", "--dyn-syms", "-W", path])
    syms = []
    for line in out.splitlines():
        m = SYM_RE.match(line)
        if not m:
            continue
        value = int(m.group(2), 16)
        typ = m.group(4)
        ndx = m.group(7)
        name = m.group(8).strip()
        if typ not in ("FUNC", "IFUNC") or ndx == "UND" or not name:
            continue
        syms.append((name, value))
    hdr = run(["llvm-readelf", "-h", path])
    for line in hdr.splitlines():
        if "Entry point address" in line:
            e = int(line.split(":")[1].strip(), 16)
            if e:
                syms.append(("<entry>", e))
    # two symbols can share an address (aliases); dedupe on the raw value
    seen, out = set(), []
    for n, v in syms:
        if v not in seen:
            seen.add(v)
            out.append((n, v))
    return out


# ---------------------------------------------------------------- disassembly
LINE_RE = re.compile(r"^\s*([0-9a-f]{4,8}):\s+([0-9a-f]{4}(?:[0-9a-f]{4}| [0-9a-f]{4})?)\s+(\S+)(?:\s+(.*))?$")
BRTGT_RE = re.compile(r"^(0x[0-9a-f]+)")

# instruction kinds for control flow
K_N = 0      # normal, falls through
K_B = 1      # unconditional direct branch
K_BC = 2     # conditional direct branch
K_BL = 3     # direct call (same mode)
K_BLX = 4    # direct call, switches to Thumb
K_IND = 5    # indirect/return: ends the block
K_CB = 6     # CBZ/CBNZ: conditional direct branch
K_UND = 7    # undecodable
K_TBB = 8    # TBB: byte jump table follows the instruction
K_TBH = 9    # TBH: halfword jump table follows the instruction

BRANCH_BASES = {"b", "bl", "blx", "bx", "bxj", "cbz", "cbnz", "tbb", "tbh"}
PC_WRITE_BASES = {"mov", "mvn", "add", "adc", "sub", "sbc", "rsb", "rsc", "and", "orr", "eor", "bic",
                  "asr", "lsl", "lsr", "ror", "rrx", "ldr", "ldrb", "ldrh", "ldrsb", "ldrsh", "ldrd"}
MEMMULTI = {"pop", "ldm", "ldmia", "ldmda", "ldmdb", "ldmib"}


class Image:
    """One object: .text arrays indexed by byte offset, decoded in windows that always start at a
    function entry (so the Thumb decode is aligned), plus a control-flow walk over them."""

    WINDOW = 0x8000      # bytes per objdump call for the prepared decode
    FILL = 0x800         # bytes per lazy objdump call when the walk needs an undecoded address
    FILL_BUDGET = 4      # objdump calls per mode and object spent on lazy fills (each costs ~0.3 s)

    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.lo, self.size, self.text_off = text_range(path)
        self.hi = self.lo + self.size
        self.file = open(path, "rb").read()
        self.roots = exports(path)
        self.exidx = sorted({a & ~1 for a in exidx_functions(path)
                             if self.lo <= (a & ~1) < self.hi})
        # function entries: exported symbols and EHABI unwind entries (the latter include statics)
        self.starts = sorted({v & ~1 for _, v in self.roots if self.lo <= (v & ~1) < self.hi}
                             | set(self.exidx))
        self.mnid = {}
        self.ilen = {}
        self.cls = {}
        self.kind = {}
        self.tgt = {}
        self.lit = {}            # mode -> {off: size} literal-pool words addressed via [pc, ...]
        self.badcond = {}        # ARM only: 1 = A32 condition field is not AL/NV
        self.mn_of_id = [""]
        self.mn_index = {}
        self.calls = 0
        self.fills = {0: 0, 1: 0}
        for mode in (0, 1):      # 0 = ARM, 1 = Thumb
            n = self.size
            self.mnid[mode] = array.array("H", bytes(2 * n))
            self.ilen[mode] = bytearray(n)
            self.cls[mode] = bytearray(n)
            self.kind[mode] = bytearray(n)
            self.badcond[mode] = bytearray(n)
            self.tgt[mode] = {}
            self.lit[mode] = {}
        self.mode_names = ["ARM", "Thumb"]

    def mn_id(self, name):
        i = self.mn_index.get(name)
        if i is None:
            i = len(self.mn_of_id)
            self.mn_index[name] = i
            self.mn_of_id.append(name)
        return i

    def decode_symbols(self, mode, names):
        """One objdump call for a batch of symbols: each is decoded from its own entry, which keeps the
        Thumb decode aligned (a single linear sweep drifts after embedded data and never recovers)."""
        triple = "armv8a" if mode == 0 else "thumbv8a"
        cmd = ["llvm-objdump", "-d", "--triple=" + triple, "--mattr=" + MATTR,
               "--disassemble-symbols=" + ",".join(names), self.path]
        self.calls += 1
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                text=True, bufsize=1 << 20)
        self._parse(proc.stdout, mode, overwrite=True)
        proc.wait()
        if proc.returncode != 0:
            sys.exit("census.py: llvm-objdump failed on %s (mode %s, symbols)"
                     % (self.path, self.mode_names[mode]))

    def _parse(self, lines, mode, overwrite):
        mnid, ilen, cls, kind, tgt = self.mnid[mode], self.ilen[mode], self.cls[mode], self.kind[mode], self.tgt[mode]
        lit = self.lit[mode]
        badcond = self.badcond[mode]
        getid = self.mn_id
        lo, hi = self.lo, self.hi
        for line in lines:
            m = LINE_RE.match(line)
            if not m:
                continue
            a = int(m.group(1), 16)
            if a < lo or a >= hi:
                continue
            raw = m.group(2).replace(" ", "")
            iln = len(raw) // 2
            mn = m.group(3)
            full = (m.group(4) or "")
            ops = full.split("@")[0].strip()
            off = a - lo
            if mnid[off] and not overwrite:
                continue
            ilen[off] = iln
            if mn == "<unknown>" or mn in (".byte", ".short", ".word"):
                kind[off] = K_UND
                mnid[off] = getid("<undecodable>")
                continue
            base, _ = strip_suffix(mn)
            mnid[off] = getid(mn)
            k = K_N
            if mode == 0:
                mm = base
                if mm == "b" or (mm[:1] == "b" and mm[1:] in CONDS):
                    tk = BRTGT_RE.match(ops)
                    if tk:
                        k = K_B if mm == "b" else K_BC
                        tgt[off] = int(tk.group(1), 16)
                    else:
                        k = K_IND
                elif mm == "bl" or (mm[:2] == "bl" and mm[2:] in CONDS):
                    tk = BRTGT_RE.match(ops)
                    if tk:
                        k = K_BL
                        tgt[off] = int(tk.group(1), 16)
                elif mm == "blx" or (mm[:3] == "blx" and mm[3:] in CONDS):
                    tk = BRTGT_RE.match(ops)
                    if tk:
                        k = K_BLX
                        tgt[off] = int(tk.group(1), 16)
                    else:
                        k = K_N             # BLX <reg>: indirect call, execution continues
                elif mm in ("bx", "bxj", "udf"):
                    k = K_IND
                elif mm == "pop" or mm.startswith("ldm"):
                    if "pc" in ops:
                        k = K_IND
                elif mm in PC_WRITE_BASES and ops.split(",")[0].strip() == "pc":
                    k = K_IND
                try:
                    if (int(raw, 16) >> 28) not in (0xE, 0xF):
                        badcond[off] = 1
                except ValueError:
                    pass
            else:
                m2 = base
                if m2 in ("tbb", "tbh"):
                    k = K_TBB if m2 == "tbb" else K_TBH
                elif m2 in ("b", "bl", "blx", "bx", "cbz", "cbnz", "udf"):
                    if m2 in ("bx", "udf"):
                        k = K_IND
                    elif m2 in ("b", "bl", "blx"):
                        tk = BRTGT_RE.match(ops)
                        if tk:
                            k = {"b": K_B, "bl": K_BL, "blx": K_BLX}[m2]
                            tgt[off] = int(tk.group(1), 16)
                        elif m2 == "blx":
                            k = K_N         # BLX <reg>: indirect call, execution continues
                        else:
                            k = K_IND
                    else:
                        tk = BRTGT_RE.findall(ops)      # CBZ/CBNZ: target is the last operand
                        if tk:
                            k = K_CB
                            tgt[off] = int(tk[-1], 16)
                elif m2.startswith("b") and m2[1:] in CONDS and len(m2) == 3:
                    tk = BRTGT_RE.match(ops)
                    if tk:
                        k = K_BC
                        tgt[off] = int(tk.group(1), 16)
                elif m2 == "pop" or m2.startswith("ldm"):
                    if "pc" in ops:
                        k = K_IND
                elif m2 in PC_WRITE_BASES and ops.split(",")[0].strip() == "pc":
                    k = K_IND
            kind[off] = k
            cls[off] = self.classify(mode, mn, base, raw)
            # literal-pool word addressed through the PC: llvm-objdump prints its address in the comment
            if "[pc" in ops and base in ("ldr", "ldrh", "ldrb", "ldrsb", "ldrsh", "ldrd", "pld"):
                cm = re.search(r"@\s*0x([0-9a-f]+)", full)
                if cm:
                    t = int(cm.group(1), 16)
                    if lo <= t < hi:
                        lit[t - lo] = 2 if base in ("ldrh", "ldrb", "ldrsb", "ldrsh") else (
                            8 if base == "ldrd" else 4)

    def decode(self, mode, start, end, overwrite):
        """Disassemble [start, end).  llvm-objdump always begins decoding at `start`, so a window that
        starts at a function entry is aligned; overwrite=True lets a repair window replace a drifted decode."""
        triple = "armv8a" if mode == 0 else "thumbv8a"
        cmd = ["llvm-objdump", "-d", "--triple=" + triple, "--mattr=" + MATTR,
               "--start-address=0x%x" % start, "--stop-address=0x%x" % end, self.path]
        self.calls += 1
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                text=True, bufsize=1 << 20)
        self._parse(proc.stdout, mode, overwrite)
        proc.wait()
        if proc.returncode != 0:
            sys.exit("census.py: llvm-objdump failed on %s (mode %s)" % (self.path, self.mode_names[mode]))

    def classify(self, mode, mn, base, raw):
        """Mnemonic -> class.  Data-type suffixes (.i32/.f32/.8/.64 ...) and A32 condition/S suffixes are
        stripped first; on A32 the condition field separates the Advanced SIMD space from VFP."""
        b, _ = strip_suffix(base)
        if "." in b:
            b = b.split(".", 1)[0]
        b = strip_cond_s(b)
        if mode == 0:
            try:
                cond = int(raw, 16) >> 28
            except ValueError:
                return 14
            if b.startswith("v") or b.startswith("f"):
                if cond == 0xF:                  # Advanced SIMD lives in the unconditional space
                    if b in SETS["nldst"]:
                        return 13
                    if b in SETS["nshift"]:
                        return 11
                    if b in SETS["nmisc"]:
                        return 12
                    return 10
                return 9                         # VFP (cp10/cp11, condition field used)
            ab, _ = a32_base(b)
            if ab in SETS["misc"]:
                return 4
            if ab in SETS["mem"]:
                return 3
            if ab in SETS["media"]:
                return 2
            if ab in SETS["dp"]:
                return 1
            return 14
        nraw = len(raw) // 2
        if nraw == 2:
            return 5                             # every 16-bit T32 instruction is V14's
        if b.startswith("v") or b.startswith("f"):
            # T32 VFP encodings start 0xEE; Advanced SIMD data processing 0xEF/0xFF
            if raw[:2].lower() == "ee":
                return 9
            if b in SETS["nldst"]:
                return 13
            if b in SETS["nshift"]:
                return 11
            if b in SETS["nmisc"]:
                return 12
            if b in SETS["n3"]:
                return 10
            return 9
        if b in ("cbz", "cbnz") or (b[:1] == "b" and b[1:] in CONDS):
            return 8
        if b in SETS["t32misc"]:
            return 8
        if b in SETS["t32mem"]:
            return 7
        if b in SETS["t32dp"]:
            return 6
        return 14

    def _probe(self, off, mode, maxn=48):
        """Score how plausible a block decoded in `mode` starting at `off` is (no side effects)."""
        score = 0
        for _ in range(maxn):
            if off < 0 or off >= self.size or self.mnid[mode][off] == 0:
                return score - 2
            k = self.kind[mode][off]
            if k == K_UND:
                return score - 2
            score += 1
            if mode == 0 and self.badcond[mode][off]:
                score -= 2
            if k in (K_IND, K_B):
                return score + 4
            off += self.ilen[mode][off]
        return score

    def entry_mode(self, value):
        """Mode of a function entry.  st_value bit 0 is the Thumb bit and is right for every symbol in this
        set except one size-0 alias in libvcodec_oal.so; .ARM.exidx entries have no mode bit at all, so the
        first block is probed in both decodes and ARM only wins if it scores clearly better."""
        if value & 1:
            return 1, "bit"
        off = value - self.lo
        arm, thm = self._probe(off, 0), self._probe(off, 1)
        if arm > thm + 1:
            return 0, "probe-arm"
        return 1, "probe-thumb"

    def prepare(self):
        """Decode every exported function by name (each starts at its own entry, so the Thumb decode is
        aligned), then fill windows for entries only .ARM.exidx knows (static functions) and for the ELF
        entry point.  Where a window drifts over embedded data, the next entry gets its own window."""
        names = [n for n, _ in self.roots if n != "<entry>"]
        for i in range(0, len(names), 800):
            self.decode_symbols(1, names[i:i + 800])
        for s in self.starts:
            off = s - self.lo
            if not self.mnid[1][off]:
                self.decode(1, s, min(s + 0x2000, self.hi), overwrite=False)

    def fill(self, mode, addr):
        """Last resort for a block start that no prepared window covered (code after an embedded jump
        table drifts a linear decode, and static functions have no symbol).  Bounded: every objdump
        call re-parses the whole ELF, so the walk may spend at most FILL_BUDGET calls per mode."""
        off = addr - self.lo
        if 0 <= off < self.size and not self.mnid[mode][off] and self.fills[mode] < self.FILL_BUDGET:
            self.fills[mode] += 1
            self.decode(mode, addr, min(addr + self.FILL, self.hi), overwrite=False)
        return 0 <= off < self.size and self.mnid[mode][off] != 0

    def jumptable(self, addr, kind):
        """TBB/TBH table: entries start right after the instruction and are offsets from PC (= addr + 4)."""
        base = addr + 4
        esz = 1 if kind == K_TBB else 2
        start = self.text_off + (base - self.lo)
        out = []
        for i in range(256 if esz == 1 else 512):
            p = start + i * esz
            if p < 0 or p + esz > len(self.file):
                break
            e = int.from_bytes(self.file[p:p + esz], "little")
            t = base + ((e << 1) if esz == 2 else e)
            if not (self.lo <= t < self.hi):
                break
            out.append(t)
        return out, len(out) * esz

    def walk(self):
        covered = bytearray(self.size)
        counts = Counter()          # (mode, class, mnemonic) -> n
        undec = []
        tables = {}                 # off -> size of TBB/TBH jump tables (data, not code)
        seen = set()
        work = []
        moded = []
        for name, v in self.roots:
            mode, why = self.entry_mode(v)
            moded.append((name, v, mode, why))
            work.append((v & ~1, mode))
        for a in self.exidx:
            mode, why = self.entry_mode(a)
            moded.append(("<exidx>", a, mode, why))
            work.append((a & ~1, mode))
        work.sort()
        while work:
            a, mode = work.pop()
            off = a - self.lo
            while 0 <= off < self.size:
                if (off, mode) in seen:
                    break
                seen.add((off, mode))
                if self.mnid[mode][off] == 0:
                    if not self.fill(mode, self.lo + off):
                        undec.append((self.mode_names[mode], self.lo + off))
                        break
                iln = self.ilen[mode][off]
                k = self.kind[mode][off]
                mn = self.mn_of_id[self.mnid[mode][off]]
                covered[off:off + iln] = b"\x01" * iln
                counts[(self.mode_names[mode], self.cls[mode][off], mn)] += 1
                if k == K_UND:
                    undec.append((self.mode_names[mode], self.lo + off))
                    break
                if k in (K_TBB, K_TBH):
                    tgts, tsz = self.jumptable(self.lo + off, k)
                    for t in tgts:
                        work.append((t & ~1, mode))
                    if tsz:
                        tables[off + iln] = tsz
                    break
                if k == K_IND:
                    break
                if k in (K_B, K_BC, K_CB, K_BL, K_BLX):
                    t = self.tgt[mode].get(off)
                    if t is not None and self.lo <= t < self.hi:
                        tm = 1 if k == K_BLX else mode
                        work.append((t & ~1, tm))
                if k == K_B:
                    break
                off += iln
        return covered, counts, undec, moded, tables


def vendor_dir(args):
    d = args.vendor_dir or os.environ.get("N2HW_VENDOR_DIR") or os.path.join(
        os.path.abspath(os.path.join(HERE, "..", "..", "..")), "gpu", "build", "cam-vendor-20261109", "vfs")
    if not os.path.isdir(d):
        sys.exit("census.py: vendor directory not found: %s" % d)
    return d


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mnemonics", action="store_true")
    ap.add_argument("--classes", action="store_true")
    ap.add_argument("--undecodable", action="store_true")
    ap.add_argument("--json", metavar="OUT")
    ap.add_argument("--library", metavar="NAME")
    ap.add_argument("--vendor-dir", default=None)
    ap.add_argument("--min-coverage", type=float, default=90.0)
    args = ap.parse_args()

    vdir = vendor_dir(args)
    objs = OBJECTS
    if args.library:
        objs = [o for o in OBJECTS if os.path.basename(o) == args.library]
        if not objs:
            sys.exit("census.py: no such object: %s" % args.library)

    report = {}
    for rel in objs:
        img = Image(os.path.join(vdir, rel))
        img.prepare()
        covered, counts, undec, moded, tables = img.walk()
        n = sum(1 for b in covered if b)
        data = bytearray(img.size)
        for mode in (0, 1):
            for off, sz in img.lit[mode].items():
                data[off:off + sz] = b"\x01" * sz
        for off, sz in tables.items():
            data[off:off + sz] = b"\x01" * sz
        ndata = sum(1 for i, b in enumerate(data) if b and not covered[i])
        code_bytes = img.size - ndata
        # second metric: coverage of the bytes inside known function ranges [entry, next entry), which is
        # where code lives; bytes between functions are padding, pools and tables.
        fn = bytearray(img.size)
        starts = img.starts
        for a, b in zip(starts, starts[1:] + [img.hi]):
            if b > a:
                fn[a - img.lo:b - img.lo] = b"\x01" * (b - a)
        fn_code = sum(1 for i in range(img.size) if fn[i] and not data[i])
        fn_cov = sum(1 for i in range(img.size) if covered[i] and fn[i])
        fn_pct = 100.0 * fn_cov / fn_code if fn_code else 0.0
        pct = 100.0 * n / code_bytes if code_bytes else 0.0
        raw_pct = 100.0 * n / img.size if img.size else 0.0
        per_mode = Counter()
        per_mn = Counter()
        per_cls_mn = defaultdict(Counter)
        for (mode, cl, mn), c in counts.items():
            per_mode[mode] += c
            per_mn[(mn, mode)] += c
            per_cls_mn[cl][mn] += c
        report[img.name] = {
            "path": os.path.join(vdir, rel), "text": [img.lo, img.size],
            "exports": len(img.roots), "exidx_entries": len(img.exidx), "objdump_calls": img.calls,
            "insns": dict(per_mode), "coverage_pct": round(pct, 2),
            "coverage_text_pct": round(raw_pct, 2),
            "covered_bytes": n, "literal_bytes": ndata, "code_bytes": code_bytes,
            "fn_code_bytes": fn_code, "fn_covered_bytes": fn_cov, "fn_coverage_pct": round(fn_pct, 2),
            "undecodable": undec,
            "mode_changes": [(name, v, mode, why) for (name, v, mode, why) in moded
                             if why != "bit" and (v & 1) == 0],
            "mnemonics": {("%s/%s" % (m, md)): c for (md, m), c in [(k, v) for k, v in per_mn.items()]},
            "classes": {str(c): dict(per_cls_mn[c]) for c in per_cls_mn},
        }
        del img

    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=1, sort_keys=True)
        print("wrote %s" % args.json)

    total_und = 0
    for name, r in report.items():
        print("%-22s .text 0x%x+%d exports=%d exidx=%d calls=%d insns=%s coverage=%.2f%% "
              "(%d/%d code bytes; function ranges %.2f%% = %d/%d; text %.2f%%; pools/tables %d) undecodable=%d"
              % (name, r["text"][0], r["text"][1], r["exports"], r["exidx_entries"], r["objdump_calls"],
                 " ".join("%s=%d" % (k, v) for k, v in sorted(r["insns"].items())),
                 r["coverage_pct"], r["covered_bytes"], r["code_bytes"], r["fn_coverage_pct"],
                 r["fn_covered_bytes"], r["fn_code_bytes"], r["coverage_text_pct"],
                 r["literal_bytes"], len(r["undecodable"])))
        total_und += len(r["undecodable"])

    if args.mnemonics:
        for name, r in report.items():
            print("\n-- %s: mnemonics by count (mode/mnemonic count)" % name)
            for k, v in sorted(r["mnemonics"].items(), key=lambda kv: (-kv[1], kv[0])):
                print("  %-18s %6d" % (k, v))

    if args.classes:
        for name, r in report.items():
            print("\n-- %s: classes" % name)
            for cid, cname, desc in CLASSES:
                if cid == 0:
                    continue
                d = r["classes"].get(str(cid))
                if not d:
                    continue
                tot = sum(d.values())
                print("  %s (%s) total %d: %s" % (cname, desc, tot,
                                                  " ".join("%s=%d" % (m, c) for m, c in sorted(d.items(), key=lambda kv: (-kv[1], kv[0])))))

    if args.undecodable:
        for name, r in report.items():
            print("\n-- %s: %d undecodable instruction addresses" % (name, len(r["undecodable"])))
            for mode, a in r["undecodable"][:400]:
                print("  %s 0x%x" % (mode, a))
            if len(r["undecodable"]) > 400:
                print("  ... %d more" % (len(r["undecodable"]) - 400))

    low = [n for n, r in report.items() if r["coverage_pct"] < args.min_coverage]
    if low:
        print("N2CENSUS WARN coverage below %.1f%%: %s" % (args.min_coverage, " ".join(low)))
        return 1
    print("N2CENSUS PASS %d/%d libraries, every .text >= %.1f%% covered" % (len(report), len(report), args.min_coverage))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:      # piped into head/grep -q: not an error
        sys.exit(0)
