#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Optional read-only cross-check of the J04 parser against real JPEGs already in the tree.

Usage: crosscheck_video.py N2JDEC_TOOL DIRECTORY

Every baseline JPEG in DIRECTORY is parsed by the tool and its width/height/component count is
compared with djpeg's verbose output (the oracle). Files with a non-SOF0 frame are skipped: the J04
parser deliberately refuses them. Prints "N2JDEC crosscheck OK/TOTAL" and exits 1 on any mismatch.
"""
import glob
import re
import subprocess
import sys

SOF_RE = re.compile(r"Start Of Frame 0x([0-9a-fA-F]+): width=(\d+), height=(\d+), components=(\d+)")
GEOM_RE = re.compile(r"width=(\d+) height=(\d+)")
COMP_RE = re.compile(r"comps=(\d+)")


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: crosscheck_video.py TOOL DIR\n")
        return 2
    tool, directory = sys.argv[1], sys.argv[2]
    files = sorted(glob.glob(directory + "/*.jpg") + glob.glob(directory + "/*.jpeg"))
    ok = bad = 0
    for path in files:
        p = subprocess.run([tool, "info", path], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True)
        d = subprocess.run(["djpeg", "-verbose", "-verbose", "-outfile", "/dev/null", path],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        m = SOF_RE.search(d.stdout)
        if not m:
            sys.stderr.write("crosscheck: djpeg gave no SOF for %s\n" % path)
            bad += 1
            continue
        sof, w, h, nc = int(m.group(1), 16), int(m.group(2)), int(m.group(3)), int(m.group(4))
        if sof != 0xC0:
            continue  # not baseline: the J04 parser is specified to refuse it
        if p.returncode != 0:
            sys.stderr.write("crosscheck: parser refused %s: %s\n" % (path, p.stderr.strip()))
            bad += 1
            continue
        g = GEOM_RE.search(p.stdout)
        c = COMP_RE.search(p.stdout)
        if (not g or not c or int(g.group(1)) != w or int(g.group(2)) != h or
                int(c.group(1)) != nc):
            sys.stderr.write("crosscheck: mismatch %s (djpeg %dx%d/%d, tool %r)\n" %
                             (path, w, h, nc, p.stdout.splitlines()[:1]))
            bad += 1
            continue
        ok += 1
    print("N2JDEC crosscheck %d/%d" % (ok, ok + bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
