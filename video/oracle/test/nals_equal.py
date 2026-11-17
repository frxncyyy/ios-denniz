#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare the NAL-unit payloads of two Annex-B streams (start codes differ)."""

import sys


def nals(data):
    starts = []
    i, n = 0, len(data)
    while i + 3 <= n:
        if data[i] == 0 and data[i + 1] == 0:
            if data[i + 2] == 1:
                starts.append((i, 3))
                i += 3
                continue
            if data[i + 2] == 0 and i + 4 <= n and data[i + 3] == 1:
                starts.append((i, 4))
                i += 4
                continue
        i += 1
    out = []
    for k, (off, sclen) in enumerate(starts):
        start = off + sclen
        end = starts[k + 1][0] if k + 1 < len(starts) else n
        while end > start and data[end - 1] == 0:
            end -= 1
        if end > start:
            out.append(data[start:end])
    return out


def main(argv):
    if len(argv) != 2:
        print("usage: nals_equal.py a.264 b.264", file=sys.stderr)
        return 2
    a = nals(open(argv[0], "rb").read())
    b = nals(open(argv[1], "rb").read())
    if a != b:
        print("NAL lists differ: %d vs %d NALs" % (len(a), len(b)), file=sys.stderr)
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                print("first difference at NAL %d (%d vs %d bytes)"
                      % (i, len(x), len(y)), file=sys.stderr)
                break
        return 1
    print("NAL lists equal: %d NALs" % len(a))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
