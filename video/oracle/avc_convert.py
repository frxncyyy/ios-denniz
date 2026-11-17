#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent AVCC <-> Annex-B converter for the WP10 oracle.

  avc_convert.py avcc2annexb IN OUT [--length 4]
  avc_convert.py annexb2avcc IN OUT [--length 4]

AVCC: every NAL unit is preceded by `length` (default 4) big-endian bytes.
Annex-B: every NAL unit is preceded by 00 00 01 or 00 00 00 01.

This is intentionally a separate implementation from check_h264.py's scanner
so that the oracle's AVCC path is not judged by the code it is testing.
"""

from __future__ import annotations

import argparse
import sys


class Fail(Exception):
    pass


def read_avcc(data: bytes, length: int):
    nals = []
    off = 0
    while off < len(data):
        if off + length > len(data):
            raise Fail("AVCC: %d trailing byte(s) do not form a NAL length" % (len(data) - off))
        n = int.from_bytes(data[off:off + length], "big")
        off += length
        if n == 0:
            raise Fail("AVCC: zero-length NAL at byte %d" % off)
        if off + n > len(data):
            raise Fail("AVCC: NAL at byte %d says %d bytes, only %d left"
                       % (off, n, len(data) - off))
        nals.append(data[off:off + n])
        off += n
    return nals


def split_annexb(data: bytes):
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
    if not starts:
        raise Fail("Annex-B: no start code found")
    nals = []
    for k, (off, sclen) in enumerate(starts):
        start = off + sclen
        end = starts[k + 1][0] if k + 1 < len(starts) else n
        while end > start and data[end - 1] == 0:
            end -= 1
        if end <= start:
            raise Fail("Annex-B: empty NAL after start code at byte %d" % off)
        nals.append(data[start:end])
    return nals


def main(argv):
    ap = argparse.ArgumentParser(description="AVCC <-> Annex-B converter (WP10)")
    ap.add_argument("mode", choices=["avcc2annexb", "annexb2avcc"])
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--length", type=int, default=4, help="AVCC length field size (2 or 4)")
    ap.add_argument("--params-from", dest="params_from",
                    help="Annex-B file to take SPS/PPS from when the AVCC stream "
                         "does not carry them; also verifies the slice NALs match")
    args = ap.parse_args(argv)
    try:
        if args.length not in (2, 4):
            raise Fail("--length must be 2 or 4")
        data = open(args.src, "rb").read()
        if args.mode == "avcc2annexb":
            nals = read_avcc(data, args.length)
            if args.params_from:
                ref = split_annexb(open(args.params_from, "rb").read())
                has_ps = bool(nals) and (nals[0][0] & 0x1F) in (7, 8)
                if not has_ps:
                    ps = []
                    for nal in ref:
                        if (nal[0] & 0x1F) in (7, 8):
                            ps.append(nal)
                        else:
                            break
                    if not ps:
                        raise Fail("--params-from: no leading SPS/PPS in %s"
                                   % args.params_from)
                    nals = ps + nals
                ref_vcl = [n for n in ref if (n[0] & 0x1F) in (1, 5)]
                our_vcl = [n for n in nals if (n[0] & 0x1F) in (1, 5)]
                if ref_vcl != our_vcl:
                    raise Fail("AVCC slice NALs differ from the Annex-B reference "
                               "(%d vs %d NALs)" % (len(our_vcl), len(ref_vcl)))
            out = b"".join(b"\x00\x00\x00\x01" + nal for nal in nals)
        else:
            nals = split_annexb(data)
            out = b"".join(len(nal).to_bytes(args.length, "big") + nal for nal in nals)
        with open(args.dst, "wb") as f:
            f.write(out)
        print("CONVERT %s nals=%d in=%d out=%d"
              % (args.mode, len(nals), len(data), len(out)))
        return 0
    except (Fail, OSError) as e:
        print("CONVERT FAIL: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
