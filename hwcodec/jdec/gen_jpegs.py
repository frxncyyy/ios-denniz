#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the J04 JPEG corpus and its expectations.

The expectations come from independent oracles:
  - djpeg -verbose -verbose: SOF marker, width/height/components, per-component sampling and
    quantisation-table ids, DQT/DHT presence, restart interval;
  - ffprobe: width/height cross-check;
  - the T.81 MCU/data-unit formulas, written out again here (not taken from the C code);
  - the file size (all generated files end exactly with EOI).

Nothing here imports the implementation under test.  Output: <outdir>/corpus.txt with one line of
26 whitespace-separated fields per JPEG (see t_jdec.c) and the JPEG/PPM files themselves.
"""
import os
import re
import shutil
import struct
import subprocess
import sys

SOF_RE = re.compile(r"Start Of Frame 0x([0-9a-fA-F]+): width=(\d+), height=(\d+), components=(\d+)")
COMP_RE = re.compile(r"^\s+Component (\d+): (\d+)hx(\d+)v q=(\d+)")
DQT_RE = re.compile(r"Define Quantization Table (\d+)  precision (\d+)")
DHT_RE = re.compile(r"Define Huffman Table 0x([0-9a-fA-F]+)")
DRI_RE = re.compile(r"Define Restart Interval (\d+)")


def run(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        sys.stderr.write("command failed: %r\n%s\n" % (cmd, p.stderr.decode("utf-8", "replace")))
        raise SystemExit(1)
    return p.stdout


def write_ppm(path, w, h, seed):
    row = bytearray()
    for y in range(h):
        for x in range(w):
            row += bytes(((x * 7 + y * 3 + seed * 11) & 255,
                          (x * 3 + y * 11 + seed * 5) & 255,
                          (x * 13 + y * 7 + seed * 2) & 255))
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(row)


def jpeg_ids(data):
    """Return [(id, h, v, tq)] from the first SOF0, by direct byte reading."""
    i = 2
    while i + 1 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        m = data[i + 1]
        i += 2
        if m in (0xD8, 0xD9, 0x01) or 0xD0 <= m <= 0xD7:
            continue
        if i + 2 > len(data):
            break
        ln = struct.unpack(">H", data[i:i + 2])[0]
        if m == 0xC0:
            seg = data[i + 2:i + ln]
            n = seg[5]
            out = []
            for k in range(n):
                cid = seg[6 + 3 * k]
                hv = seg[7 + 3 * k]
                tq = seg[8 + 3 * k]
                out.append((cid, hv >> 4, hv & 15, tq))
            return m, out
        if m == 0xDA:
            break
        i += ln
    raise SystemExit("no SOF0 in %r" % data[:16])


def djpeg_info(path):
    p = subprocess.run(["djpeg", "-verbose", "-verbose", "-outfile", "/dev/null", path],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode != 0:
        raise SystemExit("djpeg failed on %s:\n%s" % (path, p.stdout.decode("latin-1")))
    text = p.stdout.decode("latin-1")
    m = SOF_RE.search(text)
    if not m:
        raise SystemExit("djpeg: no SOF line for %s" % path)
    sof = int(m.group(1), 16)
    w, h, nc = int(m.group(2)), int(m.group(3)), int(m.group(4))
    comps = []
    for line in text.splitlines():
        c = COMP_RE.match(line)
        if c:
            comps.append((int(c.group(2)), int(c.group(3)), int(c.group(4))))
    if len(comps) != nc:
        raise SystemExit("djpeg: %s says %d components, found %d" % (path, nc, len(comps)))
    dqt = {}
    for line in text.splitlines():
        q = DQT_RE.search(line)
        if q:
            dqt[int(q.group(1))] = int(q.group(2))
    dht = set()
    for line in text.splitlines():
        d = DHT_RE.search(line)
        if d:
            dht.add(int(d.group(1), 16))
    d = DRI_RE.search(text)
    restart = int(d.group(1)) if d else 0
    return sof, w, h, nc, comps, dqt, dht, restart


def ffprobe_dim(path):
    out = run(["ffprobe", "-v", "error", "-select_streams", "v:0",
               "-show_entries", "stream=width,height", "-of", "csv=p=0", path])
    w, h = out.decode().strip().split(",")[:2]
    return int(w), int(h)


def classify(comps):
    """Return (sampling_code, out_format, uv_brz_w, words_ok) using the MTK format table."""
    hs = [c[0] for c in comps] + [0, 0, 0]
    vs = [c[1] for c in comps] + [0, 0, 0]
    sc = (hs[0] << 20) | (vs[0] << 16) | (hs[1] << 12) | (vs[1] << 8) | (hs[2] << 4) | vs[2]
    table = {
        0x00111111: (4, 5, 1),   # 444 -> 422P
        0x00412121: (6, 5, 0),   # 422X2 -> 422P
        0x00211111: (3, 5, 0),   # 422 -> 422P
        0x00121111: (5, 4, 1),   # 422V -> 420P
        0x00222121: (7, 4, 1),   # 422VX2 -> 420P
        0x00221111: (2, 4, 0),   # 420 -> 420P
        0x00110000: (1, 6, 0),   # 400 -> GREY
    }
    if sc not in table:
        return 0, 0, 0, 0
    sampling, fmt, brz = table[sc]
    ok = 1
    if hs[0] > 2 or vs[0] > 2:
        ok = 0
    return sampling, fmt, brz, ok


def expectations(path):
    data = open(path, "rb").read()
    if data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
        raise SystemExit("%s does not start with SOI and end with EOI" % path)
    sof, w, h, nc, comps, dqt, dht, restart = djpeg_info(path)
    fw, fh = ffprobe_dim(path)
    if (fw, fh) != (w, h):
        raise SystemExit("%s: ffprobe %dx%d != djpeg %dx%d" % (path, fw, fh, w, h))
    if sof != 0xC0:
        raise SystemExit("%s: not baseline (SOF 0x%02x)" % (path, sof))
    marker, ids = jpeg_ids(data)
    if len(ids) != nc:
        raise SystemExit("%s: SOF has %d components, djpeg %d" % (path, len(ids), nc))
    for k in range(nc):
        if (ids[k][1], ids[k][2], ids[k][3]) != comps[k]:
            raise SystemExit("%s: SOF comp%d %r != djpeg %r" % (path, k, ids[k], comps[k]))
    hs = [c[0] for c in comps] + [0, 0, 0]
    vs = [c[1] for c in comps] + [0, 0, 0]
    tq = [c[2] for c in comps] + [0, 0, 0]
    cid = [c[0] for c in ids] + [0, 0, 0]
    hmax, vmax = max(hs[:nc]), max(vs[:nc])
    mcu_w = (w + 8 * hmax - 1) // (8 * hmax)
    mcu_h = (h + 8 * vmax - 1) // (8 * vmax)
    total = mcu_w * mcu_h
    unit = ((w + 7) // 8) * ((h + 7) // 8)
    sampling, fmt, brz, words_ok = classify(comps)
    dqtmask = 0
    for k, prec in dqt.items():
        if prec != 0:
            raise SystemExit("%s: 16-bit DQT %d" % (path, k))
        dqtmask |= 1 << k
    dhtmask = 0
    for k in dht:
        tc, th = (k >> 4) & 1, k & 15
        dhtmask |= 1 << (th + 4 * tc)
    if nc > 1:
        du_num = ((((hs[0] - 1) & 3) << 2 | ((vs[0] - 1) & 3)) << 8 |
                  (((hs[1] - 1) & 3) << 2 | ((vs[1] - 1) & 3)) << 4 |
                  (((hs[2] - 1) & 3) << 2 | ((vs[2] - 1) & 3)))
    else:
        du_num = 0
    comp_id = ((cid[0] & 0xFF) << 24) | ((cid[1] & 0xFF) << 16) | ((cid[2] & 0xFF) << 8)
    qt_id = ((tq[0] & 15) << 8) | ((tq[1] & 15) << 4) | (tq[2] & 15)
    stride_y = stride_uv = 0
    if nc > 0:
        comp_w = (mcu_w * 8 * hs[0]) >> 0
        comp_w = (comp_w + 7) & ~7
        stride_y = (comp_w + 31) & ~31
    if nc > 1:
        comp_w = (mcu_w * 8 * hs[1]) >> brz
        comp_w = (comp_w + 7) & ~7
        stride_uv = (comp_w + 15) & ~15
    size = os.path.getsize(path)
    return dict(w=w, h=h, nc=nc, hs=hs, vs=vs, mcu_w=mcu_w, mcu_h=mcu_h, total=total,
                unit=unit, restart=restart, dqtmask=dqtmask, dhtmask=dhtmask, fmt=fmt,
                size=size, sampling=sampling, du_num=du_num, comp_id=comp_id, qt_id=qt_id,
                stride_y=stride_y, stride_uv=stride_uv, words_ok=words_ok)


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: gen_jpegs.py OUTDIR\n")
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for tool in ("cjpeg", "djpeg", "ffmpeg", "ffprobe"):
        if shutil.which(tool) is None:
            sys.stderr.write("missing tool: %s\n" % tool)
            return 2

    jobs = []          # (filename, ppm, cjpeg args)
    sizes = [(1, 1), (7, 3), (8, 8), (17, 19), (33, 9), (64, 48), (640, 480), (641, 479)]
    for (w, h) in sizes:
        write_ppm(os.path.join(out, "p_%dx%d.ppm" % (w, h)), w, h, (w * 3 + h) & 255)
    for w, h in sizes:
        ppm = os.path.join(out, "p_%dx%d.ppm" % (w, h))
        for sname, sample in (("420", "2x2,1x1,1x1"), ("422", "2x1,1x1,1x1"),
                              ("444", "1x1,1x1,1x1"), ("422v", "1x2,1x1,1x1")):
            jobs.append(("c_%s_%dx%d.jpg" % (sname, w, h), ppm,
                         ["-sample", sample, "-quality", "75"]))
    # sampling modes outside the MTK table (parser must still get the geometry right)
    for w, h in ((17, 19), (33, 9)):
        ppm = os.path.join(out, "p_%dx%d.ppm" % (w, h))
        jobs.append(("c_411_%dx%d.jpg" % (w, h), ppm,
                     ["-sample", "4x1,1x1,1x1", "-quality", "75"]))
        jobs.append(("c_mixv_%dx%d.jpg" % (w, h), ppm,
                     ["-sample", "2x2,2x1,1x2", "-quality", "75"]))
    # the remaining two entries of the MTK table: 422VX2 (supported) and 422X2 (H0=4, refused)
    jobs.append(("c_422vx2_17x19.jpg", os.path.join(out, "p_17x19.ppm"),
                 ["-sample", "2x2,2x1,2x1", "-quality", "75"]))
    jobs.append(("c_422x2_17x19.jpg", os.path.join(out, "p_17x19.ppm"),
                 ["-sample", "4x1,2x1,2x1", "-quality", "75"]))
    # restart intervals, qualities
    for w, h, r, q, name in ((17, 19, 1, 75, "c_r1_17x19.jpg"),
                             (33, 9, 3, 30, "c_r3_33x9_q30.jpg"),
                             (64, 48, 5, 95, "c_r5_64x48_q95.jpg"),
                             (17, 19, 2, 90, "c_r2_17x19_q90.jpg"),
                             (17, 19, 4, 30, "c_r4_17x19_q30.jpg")):
        ppm = os.path.join(out, "p_%dx%d.ppm" % (w, h))
        jobs.append((name, ppm, ["-sample", "2x2,1x1,1x1", "-restart", str(r),
                                 "-quality", str(q)]))
    # grey
    for w, h, r, q, name in ((33, 9, 0, 75, "c_grey_33x9.jpg"),
                             (17, 19, 3, 95, "c_grey_17x19_q95.jpg")):
        ppm = os.path.join(out, "p_%dx%d.ppm" % (w, h))
        arg = ["-grayscale", "-quality", str(q)]
        if r:
            arg += ["-restart", str(r)]
        jobs.append((name, ppm, arg))
    # hand-literal files
    jobs.append(("hand420.jpg", os.path.join(out, "p_17x19.ppm"),
                 ["-sample", "2x2,1x1,1x1", "-quality", "75"]))
    jobs.append(("hand444.jpg", os.path.join(out, "p_640x480.ppm"),
                 ["-sample", "1x1,1x1,1x1", "-quality", "75"]))
    jobs.append(("handgrey.jpg", os.path.join(out, "p_33x9.ppm"),
                 ["-grayscale", "-quality", "75"]))

    made = []
    for name, ppm, args in jobs:
        dst = os.path.join(out, name)
        run(["cjpeg"] + args + ["-outfile", dst, ppm])
        made.append(dst)

    # a few ffmpeg-produced baseline files (different DQT/DHT tables)
    ff_src = os.path.join(out, "p_33x9.ppm")
    for name, pf, q in (("ff_420.jpg", "yuvj420p", "2"), ("ff_422.jpg", "yuvj422p", "5"),
                        ("ff_444.jpg", "yuvj444p", "2"), ("ff_420b.jpg", "yuvj420p", "8")):
        dst = os.path.join(out, name)
        run(["ffmpeg", "-v", "error", "-y", "-i", ff_src, "-frames:v", "1",
             "-pix_fmt", pf, "-c:v", "mjpeg", "-q:v", q, dst])
        made.append(dst)

    # progressive file for the negative tool test (not in the corpus)
    run(["cjpeg", "-progressive", "-outfile", os.path.join(out, "progressive.jpg"),
         os.path.join(out, "p_33x9.ppm")])

    lines = []
    for path in made:
        e = expectations(path)
        fields = [path, e["w"], e["h"], e["nc"], e["hs"][0], e["vs"][0], e["hs"][1],
                  e["vs"][1], e["hs"][2], e["vs"][2], e["mcu_w"], e["mcu_h"], e["total"],
                  e["unit"], e["restart"], e["dqtmask"], e["dhtmask"], e["fmt"], e["size"],
                  e["sampling"], e["du_num"], e["comp_id"], e["qt_id"], e["stride_y"],
                  e["stride_uv"], e["words_ok"]]
        lines.append(" ".join(str(x) for x in fields))
    with open(os.path.join(out, "corpus.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("N2JDEC generated %d JPEGs" % len(made))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
