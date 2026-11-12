#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Stage include/n2vcd_proto.h rev 2 for DEC05's tests.

The frozen header belongs to hw-lead.  Until rev 2 lands, every DEC05 build uses a /tmp copy with
the exact text of daemon/dec/n2vcd_proto_dec_rev2.h.txt inserted at the documented anchors.  If the
tree header already has N2VCD_DEC_ADD_BUFFER nothing is staged.
"""
import pathlib
import re
import sys


def block(text, name):
    s = text.index("/* STAGE-BEGIN-%s */" % name)
    e = text.index("/* STAGE-END-%s */" % name)
    body = text[s:e].split("\n", 1)[1]
    return body.rstrip() + "\n"


def main():
    if len(sys.argv) != 4:
        print("usage: stage_proto.py <tree n2vcd_proto.h> <rev2 text> <out.h>", file=sys.stderr)
        return 2
    tree = pathlib.Path(sys.argv[1]).read_text()
    text = pathlib.Path(sys.argv[2]).read_text()
    out = pathlib.Path(sys.argv[3])
    out.parent.mkdir(parents=True, exist_ok=True)
    if "N2VCD_DEC_ADD_BUFFER" in tree:
        out.write_text(tree)
        print("stage_proto: tree header already rev 2")
        return 0
    lines = tree.splitlines(keepends=True)
    res = []
    inserted_enum = False
    inserted_structs = False
    for line in lines:
        if line.startswith("#define N2VCD_VERSION "):
            line = "#define N2VCD_VERSION 2u\n"
        res.append(line)
        if "N2VCD_ERROR = 100" in line and not inserted_enum:
            if not line.rstrip().endswith(","):
                line = line.rstrip() + ",\n"
                res[-1] = line
            res.append("\n" + block(text, "ENUM"))
            inserted_enum = True
        if line.startswith("struct n2vcd_simple "):
            res.append("\n" + block(text, "STRUCTS"))
            inserted_structs = True
    if not inserted_enum or not inserted_structs:
        print("stage_proto: anchors not found", file=sys.stderr)
        return 1
    staged = "".join(res)
    if not re.search(r"N2VCD_DEC_ADD_BUFFER", staged):
        print("stage_proto: insertion failed", file=sys.stderr)
        return 1
    out.write_text(staged)
    print("stage_proto: wrote %s (rev 2)" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
