#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""opus-decint: check a phone decode run against ffmpeg on the PC.

usage: compare_dec.py STREAM DPROBE_LOG [DEC_YUV] [--stream N]

STREAM      the Annex B file the phone decoded (the payload copy: decint/streams/...)
DPROBE_LOG  the phone's /private/var/db/nord2-vcodec/dprobe.log (lines "N2VCDD ... picture N ... fnv_y=.. fnv_uv=..")
DEC_YUV     optional: the phone's out/dec-<pid>-WxH.nv12.yuv (cropped NV12 frames in display order)

ffmpeg decodes STREAM to cropped NV12 (display order, -fps_mode passthrough).  Per picture the FNV-1a 64 hashes of Y and
UV are computed exactly as n2vcd_dprobe does and compared with the log: equal hashes = bit-exact.  With DEC_YUV also
PSNR(Y,U,V) per frame.  Exit 0 when every picture is bit-exact (H.264/HEVC decoders are bit-exact by specification:
anything else is a defect: detile order, plane offset, stride, cache/DMA, or a hardware decode error).
"""
import re
import subprocess
import sys
import math


def fnv64(b):
    h = 0xcbf29ce484222325
    for x in b:
        h ^= x
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h


def fnv64_fast(b):
    # same value as fnv64, chunked through int ops (pure Python is slow at 1080p but fine for 10 frames)
    return fnv64(b)


def main():
    args = sys.argv[1:]
    idx = None
    if '--stream' in args:
        k = args.index('--stream')
        idx = int(args[k + 1])
        del args[k:k + 2]
    if len(args) < 2:
        print(__doc__)
        return 2
    stream, logp = args[0], args[1]
    yuvp = args[2] if len(args) > 2 else None
    log = open(logp, errors='replace').read()
    if idx is not None:   # the dprobe section "stream N: ..." up to the next "stream M:" line
        parts = re.split(r'(?m)^N2VCDD [0-9.]+ stream (\d+): ', log)
        sect = {int(parts[i]): parts[i + 1] for i in range(1, len(parts) - 1, 2)}
        if idx not in sect:
            print('no "stream %d:" section in %s' % (idx, logp))
            return 1
        log = sect[idx]
    m = re.search(r'HEADER (\d+)x(\d+) ', log)
    if not m:
        print('no HEADER line in the log: the decode never got its geometry')
        return 1
    w, h = int(m.group(1)), int(m.group(2))
    pics = {}
    rowhash = 'fnvr_y=' in log      # logs since vc27 carry the row hash (decint/fnv_frames.c)
    key = 'fnvr' if rowhash else 'fnv'
    for mm in re.finditer(r'picture (\d+) index=\d+ pts=(\d+) poc=(-?\d+) .*?' + key + r'_y=([0-9a-f]{16}) ' + key +
                          r'_uv=([0-9a-f]{16})', log):
        pics[int(mm.group(1))] = (int(mm.group(2)), int(mm.group(4), 16), int(mm.group(5), 16))
    ys, cs = w * h, (2 * ((w + 1) // 2)) * ((h + 1) // 2)
    fs = ys + cs
    hashes = None
    if True:   # hashes always in C (fnv_frames.c): fast at any size, both hash kinds
        # large pictures (4K: 12 MB/frame): hash in C while ffmpeg streams, keep no raw frames (opus-decint)
        import os
        exe = '/tmp/n2hw-decint-fnv_frames'
        src = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fnv_frames.c')
        if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
            subprocess.run(['cc', '-O2', '-o', exe, src], check=True)
        ff = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', stream, '-fps_mode', 'passthrough', '-f', 'rawvideo',
                               '-pix_fmt', 'nv12', '-'], stdout=subprocess.PIPE)
        out = subprocess.run([exe, str(w), str(h)], stdin=ff.stdout, capture_output=True, text=True, check=True).stdout
        ff.wait()
        hashes = [tuple(int(x, 16) for x in l.split()) for l in out.splitlines()]
        hashes = [(t[2], t[3]) if rowhash else (t[0], t[1]) for t in hashes]
        ref = b''
        nref = len(hashes)
    if yuvp:
        ref = subprocess.run(['ffmpeg', '-v', 'error', '-i', stream, '-fps_mode', 'passthrough', '-f', 'rawvideo',
                              '-pix_fmt', 'nv12', '-'], capture_output=True, check=True).stdout
        nref = len(ref) // fs
    print('stream %s: %dx%d, ffmpeg %d frames; phone log: %d pictures' % (stream, w, h, nref, len(pics)))
    exact = 0
    for i in range(nref):
        if hashes is not None:
            ry, rc = hashes[i]
        else:
            fr = ref[i * fs:(i + 1) * fs]
            ry, rc = fnv64_fast(fr[:ys]), fnv64_fast(fr[ys:])
        p = pics.get(i)
        if p is None:
            print('  frame %d: MISSING on the phone' % i)
            continue
        ok = p[1] == ry and p[2] == rc
        exact += ok
        print('  frame %d pts=%d: Y %s UV %s%s' % (i, p[0], 'exact' if p[1] == ry else 'DIFF', 'exact' if p[2] == rc else 'DIFF',
                                                 '' if ok else '  (phone %016x/%016x, ffmpeg %016x/%016x)' % (p[1], p[2], ry, rc)))
    if yuvp:
        dec = open(yuvp, 'rb').read()
        nd = len(dec) // fs
        for i in range(min(nd, nref)):
            a, b = dec[i * fs:(i + 1) * fs], ref[i * fs:(i + 1) * fs]
            res = []
            for lo, hi in ((0, ys), (ys, fs)):
                se = sum((x - y) * (x - y) for x, y in zip(a[lo:hi], b[lo:hi]))
                mse = se / max(1, hi - lo)
                res.append(float('inf') if mse == 0 else 10 * math.log10(255 * 255 / mse))
            print('  yuv frame %d PSNR Y %.2f dB, UV %.2f dB' % (i, res[0], res[1]))
    print('BIT-EXACT %d/%d' % (exact, nref))
    return 0 if exact == nref and nref > 0 else 1


if __name__ == '__main__':
    sys.exit(main())
