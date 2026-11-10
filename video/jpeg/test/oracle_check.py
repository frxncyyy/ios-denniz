#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""oracle_check.py -- black-box checks of the n2jpeg output for jpeg/test/run_tests.sh.

The tools it drives (djpeg, cjpeg, ffmpeg) are test oracles only: they are installed binaries on the build machine and
nothing they contain is linked into the encoder.  Nothing here decodes JPEG with the package's own code.

subcommands
  psnr      SOURCE.nv12 DECODED.yuv W H          -> "PSNR_Y <db>"
  structure FILE.jpg                             -> validates the marker structure; prints a JSON summary, exit 1 on a fault
  tables    OURS.jpg THEIRS.jpg                  -> exit 0 when the DQT and DHT maps are identical
  ppmsize   FILE.ppm W H                         -> exit 0 when the PPM header has those dimensions
  luma_noise NV12 PPM W H                        -> writes a luma-noise NV12 (neutral chroma) and the matching PPM
"""
import json
import math
import struct
import sys


def read(path):
    with open(path, "rb") as f:
        return f.read()


def parse_markers(data):
    if len(data) < 4 or data[0:2] != b"\xff\xd8":
        raise SystemExit("not a JPEG file (no SOI)")
    i = 2
    out = []
    entropy = None
    while i + 4 <= len(data):
        if data[i] != 0xFF:
            raise SystemExit("marker expected at %d, found %02x" % (i, data[i]))
        marker = data[i + 1]
        if marker == 0xD9:
            out.append((0xD9, b""))
            if entropy is None:
                raise SystemExit("EOI before SOS")
            return out, entropy, i
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            raise SystemExit("unexpected standalone marker %02x at %d" % (marker, i))
        length = (data[i + 2] << 8) | data[i + 3]
        if length < 2 or i + 2 + length > len(data):
            raise SystemExit("bad length %d at %d" % (length, i))
        payload = data[i + 4:i + 2 + length]
        out.append((marker, payload))
        i += 2 + length
        if marker == 0xDA:
            start = i
            j = start
            while j + 1 < len(data):
                if data[j] == 0xFF:
                    n = data[j + 1]
                    if n == 0xD9:
                        entropy = (start, j)
                        return out, entropy, j
                    if n == 0x00:
                        j += 2
                        continue
                    if 0xD0 <= n <= 0xD7:
                        j += 2
                        continue
                    raise SystemExit("stray FF %02x inside the entropy data at %d" % (n, j))
                j += 1
            raise SystemExit("no EOI after SOS")
    raise SystemExit("truncated file")


def structure(path):
    data = read(path)
    markers, (start, end), eoi = parse_markers(data)
    info = {"bytes": len(data), "markers": [], "width": 0, "height": 0, "components": [], "restart_interval": 0,
            "jfif": False, "entropy_bytes": end - start}
    saw_sof = saw_sos = saw_dqt = saw_dht = False
    for m, p in markers:
        info["markers"].append("%02x" % m)
        if m == 0xE0 and p[:5] == b"JFIF\0":
            info["jfif"] = True
        elif m == 0xDB:
            saw_dqt = True
        elif m == 0xC4:
            saw_dht = True
        elif m == 0xDD:
            info["restart_interval"] = (p[0] << 8) | p[1]
        elif m == 0xC0:
            saw_sof = True
            info["height"] = (p[1] << 8) | p[2]
            info["width"] = (p[3] << 8) | p[4]
            nf = p[5]
            if nf != 3:
                raise SystemExit("SOF0 has %d components, expected 3" % nf)
            for c in range(nf):
                info["components"].append([p[6 + 3 * c], p[7 + 3 * c] >> 4, p[7 + 3 * c] & 15, p[8 + 3 * c]])
        elif m == 0xDA:
            saw_sos = True
    if not (saw_sof and saw_sos and saw_dqt and saw_dht):
        raise SystemExit("missing SOF0/SOS/DQT/DHT")
    if markers[0][0] != 0xE0 and not info["jfif"]:
        raise SystemExit("no APP0 JFIF at the start")
    # every FF inside the entropy data must be a stuffed FF00 or an RSTn; parse_markers already enforced that
    if data[-2:] != b"\xff\xd9":
        raise SystemExit("file does not end with EOI")
    print(json.dumps(info, sort_keys=True))
    return 0


def tables(path):
    data = read(path)
    markers, _, _ = parse_markers(data)
    dqt, dht = {}, {}
    for m, p in markers:
        if m == 0xDB:
            k = 0
            while k + 65 <= len(p):
                dqt[p[k] & 15] = list(p[k + 1:k + 65])
                k += 65
        elif m == 0xC4:
            k = 0
            while k + 17 <= len(p):
                tc = p[k] >> 4
                th = p[k] & 15
                n = sum(p[k + 1:k + 17])
                dht[(tc, th)] = list(p[k + 1:k + 17]) + list(p[k + 17:k + 17 + n])
                k += 17 + n
    return dqt, dht


def psnr(source, decoded, w, h):
    src = read(source)[:w * h]
    dec = read(decoded)
    if len(dec) < w * h:
        raise SystemExit("decoded file is %d bytes, need %d for the luma plane" % (len(dec), w * h))
    dec = dec[:w * h]
    mse = sum((a - b) ** 2 for a, b in zip(src, dec)) / float(w * h)
    if mse == 0:
        print("PSNR_Y inf")
        return 0
    print("PSNR_Y %.4f" % (10.0 * math.log10(255.0 * 255.0 / mse)))
    return 0


def luma_noise(nv12_path, ppm_path, w, h):
    state = 1
    y = bytearray(w * h)
    for i in range(w * h):
        state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
        y[i] = state >> 24
    uv = bytes([128]) * (w * h // 2)
    with open(nv12_path, "wb") as f:
        f.write(bytes(y) + uv)
    with open(ppm_path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(bytes(v for value in y for v in (value, value, value)))
    return 0


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    cmd = argv[1]
    if cmd == "psnr" and len(argv) == 6:
        return psnr(argv[2], argv[3], int(argv[4]), int(argv[5]))
    if cmd == "structure" and len(argv) == 3:
        return structure(argv[2])
    if cmd == "tables" and len(argv) == 4:
        a, b = tables(argv[2]), tables(argv[3])
        if a != b:
            if a[0] != b[0]:
                print("DQT maps differ: %s vs %s" % (sorted(a[0]), sorted(b[0])))
            if a[1] != b[1]:
                keys = sorted(set(a[1]) | set(b[1]))
                for k in keys:
                    if a[1].get(k) != b[1].get(k):
                        print("DHT %s differs" % (k,))
            return 1
        print("TABLES equal (%d quant, %d huffman)" % (len(a[0]), len(a[1])))
        return 0
    if cmd == "ppmsize" and len(argv) == 5:
        data = read(argv[2])
        header = data.split(b"\n", 3)
        if header[0] != b"P6" or header[1].split() != [argv[3].encode(), argv[4].encode()]:
            raise SystemExit("PPM header %r does not match %sx%s" % (header[:2], argv[3], argv[4]))
        print("PPM %sx%s" % (argv[3], argv[4]))
        return 0
    if cmd == "luma_noise" and len(argv) == 6:
        return luma_noise(argv[2], argv[3], int(argv[4]), int(argv[5]))
    raise SystemExit(__doc__)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
