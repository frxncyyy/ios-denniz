#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Split an Annex B H.264 stream into access units for daemon/test/fake_vhost.c.

Record format (little endian): u32 length, u32 key_frame, length bytes of the access unit (start codes included).
An access unit starts at an SPS/VPS or at a slice NAL when the current unit already has a slice; the key flag is
"the unit contains an SPS".  The splitter only needs the NAL header byte, which is part of the H.264 syntax
(ISO/IEC 14496-10 7.3.1); it does not parse slices.
"""
import struct
import sys


def nals(data):
    """Yield (start_offset, nal_offset) for every Annex B NAL unit."""
    i = 0
    n = len(data)
    starts = []
    while i + 3 <= n:
        if data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 1:
            starts.append((i, i + 3))
            i += 3
        elif i + 4 <= n and data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 0 and data[i + 3] == 1:
            starts.append((i, i + 4))
            i += 4
        else:
            i += 1
    for k, (start, nal) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else n
        yield start, nal, end


def split(data):
    units = []
    cur = bytearray()
    cur_has_slice = False
    cur_key = False
    for start, nal, end in nals(data):
        if nal >= len(data):
            continue
        ntype = data[nal] & 0x1F
        new_unit = ntype in (7, 32) or (ntype in (1, 5) and cur_has_slice)
        if new_unit and cur:
            units.append((bytes(cur), cur_key))
            cur = bytearray()
            cur_has_slice = False
            cur_key = False
        cur += data[start:end]
        if ntype in (1, 5):
            cur_has_slice = True
        if ntype == 7:
            cur_key = True
    if cur:
        units.append((bytes(cur), cur_key))
    return units


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: split_au.py IN.h264 OUT.au\n")
        return 2
    data = open(sys.argv[1], "rb").read()
    units = split(data)
    with open(sys.argv[2], "wb") as f:
        for payload, key in units:
            f.write(struct.pack("<II", len(payload), 1 if key else 0))
            f.write(payload)
    keys = sum(1 for _, k in units if k)
    print("split_au: %d bytes -> %d access units (%d key)" % (len(data), len(units), keys))
    return 0


if __name__ == "__main__":
    sys.exit(main())
