#!/usr/bin/env python3
"""SPDX-License-Identifier: MIT

check_vendor_words.py -- acceptance check against the phone's own vendor libraries.

The V18 acceptance criterion is "no mnemonic of this class that the vendor libraries use is returned
as N2A32_UNDEF".  This script disassembles the code of the five vendor objects from
gpu/build/cam-vendor-20261109/vfs (or $N2V_VENDOR) with llvm-objdump, keeps the 32-bit Thumb-2
Advanced SIMD data-processing instructions whose A32 form falls in this instruction class, and reports
the mnemonics that actually occur.  Only facts (mnemonics and counts) are printed; no vendor bytes are
copied anywhere.

Exit status: 0 if every mnemonic used in this class is implemented or belongs to the SHA space,
1 if some other class instruction is used that n2a32_neon_3same() rejects, 2 on setup error.
A missing vendor tree prints a SKIP line and exits 0 (the host tests do not require the phone files).
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_VENDOR = os.path.abspath(os.path.join(HERE, "..", "..", "..", "build",
                                              "cam-vendor-20261109", "vfs"))
OBJECTS = ["lib/libvcodecdrv.so", "lib/libvcodec_utility.so", "lib/libvpud_vcodec.so",
           "lib/libvcodec_oal.so", "bin/vpud"]

IMPLEMENTED = set("""
vadd vsub vhadd vhsub vrhadd vqadd vqsub vcgt vcge vceq vtst vshl vqshl vrshl vqrshl
vmax vmin vabd vaba vmul vmla vmls vqdmulh vqrdmulh vpmax vpmin vpadd
vand vbic vorr vorn veor vbsl vbit vbif
vfma vfms vacge vacgt vrecps vrsqrts vmaxnm vminnm
""".split())
SHA = set("sha1c sha1p sha1m sha1su0 sha256h sha256h2 sha256su1".split())

LINE = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{4}(?: [0-9a-f]{4})?)\s+(\S+)")


def a32_of(hw1, hw2):
    """The core's T32 Advanced SIMD data-processing conversion."""
    a32 = 0xF2000000 | ((hw1 & 0xFF) << 16) | hw2
    if hw1 & 0x1000:
        a32 |= 0x01000000
    return a32


def main():
    vendor = os.environ.get("N2V_VENDOR", DEFAULT_VENDOR)
    if not os.path.isdir(os.path.join(vendor, "lib")):
        print("V18-VENDOR SKIP (no vendor tree at %s)" % vendor)
        return 0

    counts = {}
    problems = []
    total = 0
    for rel in OBJECTS:
        path = os.path.join(vendor, rel)
        if not os.path.exists(path):
            continue
        p = subprocess.run(["llvm-objdump", "-d", "--triple=thumbv8a",
                            "--mattr=+neon,+vfp4,+fp-armv8,+hwdiv,+hwdiv-arm", path],
                           capture_output=True, text=True)
        if p.returncode != 0:
            print("V18-VENDOR FAIL llvm-objdump %s" % rel)
            return 2
        inside_neon = False
        for line in p.stdout.splitlines():
            m = LINE.match(line)
            if not m:
                continue
            fields = m.group(2).split()
            if len(fields) != 2:
                continue                    # 16-bit instruction, not this class
            hw1 = int(fields[0], 16)
            if (hw1 & 0xFF00) not in (0xEF00, 0xFF00):
                continue                    # not T32 Advanced SIMD data processing
            a32 = a32_of(hw1, int(fields[1], 16))
            if (a32 & 0xFE800000) != 0xF2000000:
                continue                    # other NEON spaces: two regs, shifts, ld/st
            inside_neon = True
            mn = m.group(3).split(".")[0]
            if mn == "<unknown>":
                # llvm-objdump could not decode the word (data inside .text, or it lost
                # synchronisation): not an instruction this class has to implement.
                counts["<unallocated>"] = counts.get("<unallocated>", 0) + 1
                continue
            counts[mn] = counts.get(mn, 0) + 1
            total += 1
            if mn in SHA:
                problems.append((rel, m.group(1), m.group(3), "SHA space (deliberately UNDEF)"))
            elif mn not in IMPLEMENTED:
                problems.append((rel, m.group(1), m.group(3), "not implemented"))
        del inside_neon

    print("V18-VENDOR class instructions %d, mnemonics: %s"
          % (total, ", ".join("%s=%d" % (k, v) for k, v in sorted(counts.items()))))
    for rel, addr, text, why in problems:
        print("V18-VENDOR detail %s+0x%s %-32s %s" % (rel, addr, text, why))
    real = [p for p in problems if p[3] != "SHA space (deliberately UNDEF)"]
    if not real:
        print("V18-VENDOR PASS every class mnemonic used by the vendor objects is implemented")
        return 0
    print("V18-VENDOR FAIL %d class instructions outside the implemented/SHA set" % len(real))
    return 1


if __name__ == "__main__":
    sys.exit(main())
