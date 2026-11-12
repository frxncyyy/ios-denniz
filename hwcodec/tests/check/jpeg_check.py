#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""jpeg_check.py -- structural + pixel check of a JPEG against an NV12 source (T02).

Generalises the checker of gpu/hwcodec/tools/hwc.py:
  * walks the markers (SOI, APPn, DQT, SOF0/1/2, DHT, SOS, EOI), accepts the
    padding the hardware may leave after EOI, prints the marker list, the SOF
    geometry, the component sampling and the DQT table ids;
  * decodes with ffmpeg (exit 0, exact w*h*3/2 bytes, full-range yuvj420p) and
    computes per-plane PSNR against a raw NV12 file (pure Python, no numpy);
  * decodes independently with djpeg -ppm and checks its geometry and luma
    PSNR against the same source (pixel comparison is sampled above
    --djpeg-max-pixels to keep 12 MP pictures practical).

Usage:
  jpeg_check.py FILE --width W --height H --source picture.nv12
                 [--stride S] [--chroma-stride CS] [--rows R]
                 [--min-psnr 30] [--sampling 420|422|444|any]
                 [--json out.json] [--no-djpeg] [--quiet]

Exit 0 = PASS, 1 = FAIL, 2 = usage/tool/parse error.
"""

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path


class CheckError(Exception):
    pass


class ToolError(CheckError):
    """ffmpeg/djpeg missing: a setup error (exit 2), not a bad picture."""
    pass


def be16(b):
    return (b[0] << 8) | b[1]


def parse_jpeg(data):
    rep = {"ok": False, "errors": [], "markers": [], "tables": {}, "huff": {}, "components": []}
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        rep["errors"].append("no SOI")
        return rep
    end = data.rfind(b"\xff\xd9")
    if end < 0:
        rep["errors"].append("no EOI (FF D9)")
        return rep
    rep["trailing_bytes"] = len(data) - (end + 2)
    if any(data[end + 2:]):
        rep["errors"].append("nonzero bytes after EOI")
    body = data[:end + 2]
    i = 2
    seen_sos = False
    rep["markers"].append("D8")
    while i < len(body):
        if body[i] != 0xFF:
            rep["errors"].append(f"marker expected at {i}")
            return rep
        while i < len(body) and body[i] == 0xFF:
            i += 1
        if i >= len(body):
            rep["errors"].append("truncated marker")
            return rep
        m = body[i]
        i += 1
        rep["markers"].append(f"{m:02X}")
        if m == 0xD9:
            break
        if m == 0xD8 or m == 0x01 or 0xD0 <= m <= 0xD7:
            continue
        if i + 2 > len(body):
            rep["errors"].append(f"truncated length at {i}")
            return rep
        ln = be16(body[i:i + 2])
        if ln < 2 or i + ln > len(body):
            rep["errors"].append(f"bad segment length {ln} at {i}")
            return rep
        seg = body[i + 2:i + ln]
        i += ln
        if m == 0xDB:
            j = 0
            while j < len(seg):
                pq = seg[j] >> 4
                tq = seg[j] & 15
                j += 1
                n = 64 * (2 if pq else 1)
                if j + n > len(seg):
                    rep["errors"].append("truncated DQT")
                    return rep
                rep["tables"][tq] = list(seg[j:j + n])
                j += n
        elif m in (0xC0, 0xC1, 0xC2):
            if len(seg) < 6:
                rep["errors"].append("short SOF")
                return rep
            rep["sof"] = m
            rep["precision"] = seg[0]
            if seg[0] != 8:
                rep["errors"].append(f"sample precision {seg[0]} != 8")
            rep["height"] = be16(seg[1:3])
            rep["width"] = be16(seg[3:5])
            nf = seg[5]
            if len(seg) != 6 + 3 * nf:
                rep["errors"].append("bad SOF component count")
                return rep
            for k in range(nf):
                cid = seg[6 + 3 * k]
                hv = seg[7 + 3 * k]
                tq = seg[8 + 3 * k]
                rep["components"].append({"id": cid, "h": hv >> 4, "v": hv & 15, "tq": tq})
        elif m == 0xC4:
            j = 0
            while j < len(seg):
                tc = seg[j] >> 4
                th = seg[j] & 15
                j += 1
                if j + 16 > len(seg):
                    rep["errors"].append("truncated DHT")
                    return rep
                counts = list(seg[j:j + 16])
                j += 16
                total = sum(counts)
                if j + total > len(seg):
                    rep["errors"].append("truncated DHT values")
                    return rep
                rep["huff"][f"{tc}/{th}"] = total
                j += total
        elif m == 0xDA:
            if len(seg) < 4:
                rep["errors"].append("short SOS")
                return rep
            ns = seg[0]
            if len(seg) != 1 + 2 * ns + 3:
                rep["errors"].append("bad SOS length")
                return rep
            rep["sos_components"] = [seg[1 + 2 * k] for k in range(ns)]
            rep["sos_tables"] = [(seg[2 + 2 * k] >> 4, seg[2 + 2 * k] & 15) for k in range(ns)]
            seen_sos = True
            # skip entropy-coded data: FF00 is stuffing, FFD0-D7 are RSTn
            j = i
            while j + 1 < len(body):
                if body[j] == 0xFF:
                    n = body[j + 1]
                    if n == 0x00 or 0xD0 <= n <= 0xD7:
                        j += 2
                        continue
                    break
                j += 1
            rep["scan_bytes"] = j - i
            i = j
    if not seen_sos:
        rep["errors"].append("no SOS")
    if "sof" not in rep:
        rep["errors"].append("no SOF")
    if not rep["tables"]:
        rep["errors"].append("no DQT")
    if not rep["huff"]:
        rep["errors"].append("no DHT")
    if rep["markers"] and rep["markers"][-1] != "D9":
        rep["errors"].append("last marker is not EOI")
    if rep.get("sof") is not None:
        for c in rep["components"]:
            if c["tq"] not in rep["tables"]:
                rep["errors"].append(f"component {c['id']} uses missing DQT {c['tq']}")
    rep["ok"] = not rep["errors"]
    return rep


def sampling_name(comps):
    if len(comps) != 3:
        return "other"
    if comps[0]["h"] == 2 and comps[0]["v"] == 2 and comps[1]["h"] == 1 and comps[1]["v"] == 1 \
            and comps[2]["h"] == 1 and comps[2]["v"] == 1:
        return "420"
    if comps[0]["h"] == 2 and comps[0]["v"] == 1 and comps[1]["h"] == 1 and comps[1]["v"] == 1 \
            and comps[2]["h"] == 1 and comps[2]["v"] == 1:
        return "422"
    if all(c["h"] == 1 and c["v"] == 1 for c in comps):
        return "444"
    return "other"


def run(cmd, what):
    try:
        return subprocess.run(cmd, capture_output=True, check=False)
    except FileNotFoundError:
        raise ToolError(f"{what}: executable not found: {cmd[0]}")


def psnr(a, b):
    if len(a) != len(b):
        raise CheckError("PSNR length mismatch")
    se = 0
    for x, y in zip(a, b):
        d = x - y
        se += d * d
    if se == 0:
        return 99.0
    return 10.0 * math.log10(255.0 * 255.0 * len(a) / se)


def source_planes(path, w, h, stride, chroma_stride, rows):
    if stride < w or chroma_stride < w or rows < h:
        raise CheckError(f"source layout: stride {stride} >= {w}, chroma_stride {chroma_stride} >= {w}, rows {rows} >= {h}")
    data = Path(path).read_bytes()
    need = stride * rows + chroma_stride * (h // 2)
    if len(data) < need:
        raise CheckError(f"source is {len(data)} bytes, need at least {need} "
                         f"(stride {stride}, chroma_stride {chroma_stride}, rows {rows})")
    y = b"".join(data[r * stride:r * stride + w] for r in range(h))
    coff = stride * rows
    u = b"".join(data[coff + r * chroma_stride:coff + r * chroma_stride + w:2] for r in range(h // 2))
    v = b"".join(data[coff + r * chroma_stride + 1:coff + r * chroma_stride + w:2] for r in range(h // 2))
    return y, u, v


def ffmpeg_decode(path, w, h):
    r = run(["ffmpeg", "-v", "error", "-nostdin", "-i", str(path), "-f", "rawvideo",
             "-pix_fmt", "yuvj420p", "-"], "ffmpeg")
    if r.returncode != 0:
        raise CheckError("ffmpeg decode failed: " + r.stderr.decode("utf-8", "replace").strip())
    need = w * h + 2 * ((w + 1) // 2) * ((h + 1) // 2)
    if len(r.stdout) != need:
        raise CheckError(f"ffmpeg decoded {len(r.stdout)} bytes, expected {need}")
    ysize = w * h
    csize = ((w + 1) // 2) * ((h + 1) // 2)
    return r.stdout[:ysize], r.stdout[ysize:ysize + csize], r.stdout[ysize + csize:ysize + 2 * csize]


def parse_ppm(data):
    if not data.startswith(b"P6"):
        raise CheckError("djpeg did not produce P6 PPM")
    i = 2
    toks = []
    while len(toks) < 3:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if i < len(data) and data[i:i + 1] == b"#":
            while i < len(data) and data[i:i + 1] != b"\n":
                i += 1
            continue
        start = i
        while i < len(data) and not data[i:i + 1].isspace():
            i += 1
        if start == i:
            raise CheckError("bad PPM header")
        toks.append(int(data[start:i]))
    while i < len(data) and data[i:i + 1].isspace():
        i += 1
    w, h, maxv = toks
    if maxv != 255:
        raise CheckError(f"djpeg PPM maxval {maxv} != 255")
    pix = data[i:]
    if len(pix) != w * h * 3:
        raise CheckError(f"djpeg PPM has {len(pix)} pixel bytes, expected {w * h * 3}")
    return w, h, pix


def djpeg_check(path, w, h, src_y, min_psnr, max_pixels):
    r = run(["djpeg", "-ppm", str(path)], "djpeg")
    if r.returncode != 0:
        raise CheckError("djpeg failed: " + r.stderr.decode("utf-8", "replace").strip())
    dw, dh, pix = parse_ppm(r.stdout)
    out = {"width": dw, "height": dh, "ok": dw == w and dh == h}
    if dw != w or dh != h:
        out["error"] = f"djpeg geometry {dw}x{dh} != {w}x{h}"
        return out
    total = w * h
    step = 1 if total <= max_pixels else int(math.sqrt(total / max_pixels)) + 1
    se = 0
    n = 0
    for y in range(0, h, step):
        base = y * w
        for x in range(0, w, step):
            o = 3 * (base + x)
            rr, gg, bb = pix[o], pix[o + 1], pix[o + 2]
            yy = (19595 * rr + 38470 * gg + 7471 * bb + 32768) >> 16
            d = yy - src_y[base + x]
            se += d * d
            n += 1
    out["sampled_pixels"] = n
    out["step"] = step
    out["psnr_y"] = round(99.0 if se == 0 else 10.0 * math.log10(255.0 * 255.0 * n / se), 3)
    out["ok"] = out["psnr_y"] >= min_psnr
    if not out["ok"]:
        out["error"] = f"djpeg luma PSNR {out['psnr_y']:.2f} < {min_psnr}"
    return out


def check(path, w, h, source=None, stride=None, chroma_stride=None, rows=None,
          min_psnr=30.0, sampling="420", no_djpeg=False, djpeg_max_pixels=4_000_000):
    data = Path(path).read_bytes()
    rep = {"file": str(path), "bytes": len(data), "expected": {"width": w, "height": h},
           "errors": []}
    st = parse_jpeg(data)
    rep["structure"] = st
    rep["errors"] += st["errors"]
    if st.get("width") != w or st.get("height") != h:
        rep["errors"].append(f"SOF geometry {st.get('width')}x{st.get('height')} != {w}x{h}")
    rep["sampling"] = sampling_name(st["components"])
    if sampling != "any" and rep["sampling"] != sampling:
        rep["errors"].append(f"sampling {rep['sampling']} != {sampling}")
    if source is not None:
        stride = w if stride is None else stride
        chroma_stride = stride if chroma_stride is None else chroma_stride
        rows = h if rows is None else rows
        sy, su, sv = source_planes(source, w, h, stride, chroma_stride, rows)
        dy, du, dv = ffmpeg_decode(path, w, h)
        rep["psnr_y"] = round(psnr(dy, sy), 3)
        rep["psnr_u"] = round(psnr(du, su), 3)
        rep["psnr_v"] = round(psnr(dv, sv), 3)
        for name, val in (("Y", rep["psnr_y"]), ("U", rep["psnr_u"]), ("V", rep["psnr_v"])):
            if val < min_psnr:
                rep["errors"].append(f"{name} PSNR {val:.2f} < {min_psnr}")
        if not no_djpeg:
            try:
                rep["djpeg"] = djpeg_check(path, w, h, sy, min_psnr, djpeg_max_pixels)
                if not rep["djpeg"]["ok"]:
                    rep["errors"].append(rep["djpeg"].get("error", "djpeg check failed"))
            except CheckError as e:
                rep["errors"].append(str(e))
    rep["verdict"] = "PASS" if not rep["errors"] else "FAIL"
    return rep


def print_report(rep, quiet=False):
    st = rep["structure"]
    print(f"file: {rep['file']} bytes={rep['bytes']}")
    print(f"markers: {' '.join(st['markers'])}")
    print(f"sof: {'0x%02X' % st['sof'] if 'sof' in st else 'none'} "
          f"{st.get('width')}x{st.get('height')} components={len(st['components'])} "
          f"sampling={rep['sampling']}")
    print(f"dqt: {','.join(str(k) for k in sorted(st['tables']))} "
          f"dht: {','.join(sorted(st['huff']))} trailing_bytes={st.get('trailing_bytes', 0)}")
    if "psnr_y" in rep:
        print(f"psnr: y={rep['psnr_y']:.3f} u={rep['psnr_u']:.3f} v={rep['psnr_v']:.3f}")
    if "djpeg" in rep:
        d = rep["djpeg"]
        print(f"djpeg: {d.get('width')}x{d.get('height')} sampled={d.get('sampled_pixels')} "
              f"step={d.get('step')} psnr_y={d.get('psnr_y')}")
    if not quiet:
        for c in st["components"]:
            print(f"component {c['id']}: h={c['h']} v={c['v']} dqt={c['tq']}")
    for err in rep["errors"][:20]:
        print(f"ERROR: {err}")
    if len(rep["errors"]) > 20:
        print(f"ERROR: ... {len(rep['errors']) - 20} more")
    print(f"verdict: {rep['verdict']}")


def main(argv=None):
    ap = argparse.ArgumentParser(description="JPEG + NV12 source checker (T02)")
    ap.add_argument("file")
    ap.add_argument("--width", type=int, required=True)
    ap.add_argument("--height", type=int, required=True)
    ap.add_argument("--source", help="raw NV12 picture (Y h*stride, then CbCr h/2*chroma_stride)")
    ap.add_argument("--stride", type=int, help="source luma stride (default width)")
    ap.add_argument("--chroma-stride", type=int, help="source chroma stride (default stride)")
    ap.add_argument("--rows", type=int, help="source luma rows in the file (default height; padding rows are ignored)")
    ap.add_argument("--min-psnr", type=float, default=30.0)
    ap.add_argument("--sampling", choices=("420", "422", "444", "any"), default="420")
    ap.add_argument("--no-djpeg", action="store_true")
    ap.add_argument("--djpeg-max-pixels", type=int, default=4_000_000)
    ap.add_argument("--json", help="write the full report as JSON here")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args(argv)
    if a.width <= 0 or a.height <= 0 or a.width % 2 or a.height % 2:
        print("width and height must be positive even numbers", file=sys.stderr)
        return 2
    try:
        rep = check(a.file, a.width, a.height, a.source, a.stride, a.chroma_stride, a.rows,
                    a.min_psnr, a.sampling, a.no_djpeg, a.djpeg_max_pixels)
    except ToolError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    except CheckError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    except (OSError, ValueError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    print_report(rep, a.quiet)
    if a.json:
        Path(a.json).write_text(json.dumps(rep, indent=1) + "\n")
    return 0 if rep["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
