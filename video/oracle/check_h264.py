#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent judge for H.264 streams produced by gpu/video/avc (n2avc).

Usage:
  check_h264.py --stream out.264 --size WxH --frames N
                [--recon recon.yuv] [--source src.yuv] [--stats stats.json]
                [--expect key=value ...]
  check_h264.py --scan file.264          (Annex-B scanner only)

Checks, in order:
  1. the Annex-B scanner (independent of ffmpeg): start codes, NAL headers,
     forbidden_zero_bit, legal nal_unit_type, and no unescaped 00 00 00/01/02
     inside a NAL payload;
  2. ffmpeg decodes the stream with exit 0 and EMPTY stderr into exactly N
     frames of WxH;
  3. with --recon, every decoded sample is byte-identical to the encoder's
     reconstruction, and the first difference is reported as
     frame=.. plane=.. x=.. y=.. mb=..;
  4. with --source, per-frame luma PSNR is at least the floor for that
     picture's QP (stats.json): QP<=12 44 dB, <=26 36, <=32 32, <=40 27,
     else 22;
  5. the SPS/PPS/slice headers via ffmpeg's trace_headers bitstream filter:
     Constrained Baseline profile, level per n2avc.h, POC type 2, one
     reference frame, frame_mbs_only, CAVLC, the exact frame cropping for
     WxH, max_num_reorder_frames 0, slices per picture, and the NAL type
     sequence starting 7, 8, 5.

Exit 0 with last line "ORACLE h264 PASS ..."; exit 1 with
"ORACLE h264 FAIL: <first reason>".
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import yuv  # noqa: E402

FFMPEG = os.environ.get("N2_FFMPEG", "ffmpeg")

# nal_unit_type values that may appear in an H.264 (08/2021) bit stream we
# judge.  Table 7-1: 0 and 24..31 are unspecified, 23 is reserved; a stream
# that uses them is not one of ours.
LEGAL_NAL_TYPES = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                   16, 17, 18, 19, 20, 21, 22}

# H.264 Table A-1 limits for the levels n2avc.h allows: (level_idc, MaxFS, MaxMBPS).
# The frozen header promises "the smallest" of these that covers the picture;
# MaxFS is in macroblocks per picture and MaxMBPS in macroblocks per second.
LEVEL_LIMITS = ((30, 1620, 40500), (31, 3600, 108000), (40, 8192, 245760),
                (42, 8704, 522240), (51, 36864, 983040))


class Fail(Exception):
    pass


def parse_size(s: str):
    m = re.fullmatch(r"(\d+)[xX](\d+)", s.strip())
    if not m:
        raise Fail("bad --size %r (want WxH)" % s)
    w, h = int(m.group(1)), int(m.group(2))
    if w <= 0 or h <= 0:
        raise Fail("bad --size %r" % s)
    return w, h


def expected_level(w: int, h: int, fps_num: int = 30, fps_den: int = 1) -> int:
    """Smallest n2avc.h level whose Table A-1 limits cover this stream."""
    mbw, mbh = (w + 15) // 16, (h + 15) // 16
    fs = mbw * mbh
    for level, max_fs, max_mbps in LEVEL_LIMITS:
        if fs <= max_fs and fs * fps_num <= max_mbps * fps_den:
            return level
    return 51


def expected_crop(w: int, h: int):
    mbw, mbh = (w + 15) // 16, (h + 15) // 16
    dr, db = 16 * mbw - w, 16 * mbh - h
    if dr % 2 or db % 2:
        raise Fail("size %dx%d cannot be cropped in units of 2 luma samples" % (w, h))
    return mbw, mbh, dr // 2, db // 2


# ---------------------------------------------------------------- Annex B

def find_start_codes(data: bytes):
    """[(offset, length), ...] for every 00 00 01 / 00 00 00 01 start code."""
    out = []
    i, n = 0, len(data)
    while i + 3 <= n:
        if data[i] == 0 and data[i + 1] == 0:
            if data[i + 2] == 1:
                out.append((i, 3))
                i += 3
                continue
            if data[i + 2] == 0 and i + 4 <= n and data[i + 3] == 1:
                out.append((i, 4))
                i += 4
                continue
        i += 1
    return out


def scan_annexb(data: bytes):
    """Validate an Annex-B byte stream independently of any decoder.

    Returns (types, n_nal) where types is the nal_unit_type of every NAL in
    order.  Raises Fail with the first violation.  00 00 01 always starts a
    NAL in Annex B, so a NAL that "contains" 00 00 01 is split there; the
    resulting NAL headers are then validated (a hand-made NAL that hides a
    start code is caught by the illegal type / forbidden bit / empty NAL
    rules).  00 00 00 and 00 00 02 inside a payload are unescaped and are
    caught directly.
    """
    n = len(data)
    if n == 0:
        raise Fail("empty stream")
    sc = find_start_codes(data)
    if not sc:
        raise Fail("no Annex-B start code (00 00 01 / 00 00 00 01) in the stream")
    if any(b != 0 for b in data[:sc[0][0]]):
        raise Fail("%d non-zero byte(s) before the first start code" % sc[0][0])

    types = []
    for k, (off, sclen) in enumerate(sc):
        start = off + sclen
        end = sc[k + 1][0] if k + 1 < len(sc) else n
        # trailing_zero_8bits belong to the next start code, not to this NAL
        while end > start and data[end - 1] == 0:
            end -= 1
        if end <= start:
            raise Fail("empty NAL unit after start code at byte %d" % off)
        hdr = data[start]
        if hdr & 0x80:
            raise Fail("NAL at byte %d: forbidden_zero_bit is 1 (header 0x%02x)" % (off, hdr))
        nut = hdr & 0x1F
        if nut not in LEGAL_NAL_TYPES:
            raise Fail("NAL at byte %d: illegal nal_unit_type %d (header 0x%02x)"
                       % (off, nut, hdr))
        payload = data[start + 1:end]
        for pat, what in ((b"\x00\x00\x00", "00 00 00"),
                          (b"\x00\x00\x01", "00 00 01"),
                          (b"\x00\x00\x02", "00 00 02")):
            p = payload.find(pat)
            if p >= 0:
                raise Fail("NAL at byte %d: unescaped %s inside the payload (byte %d)"
                           % (off, what, start + 1 + p))
        types.append(nut)
    return types, len(types)


# ------------------------------------------------------------ ffmpeg helpers

def run_ffmpeg(args, timeout=300):
    try:
        return subprocess.run([FFMPEG] + args, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=timeout)
    except FileNotFoundError:
        raise Fail("ffmpeg not found (set N2_FFMPEG)")
    except subprocess.TimeoutExpired:
        raise Fail("ffmpeg timed out")


def decode_stream(stream: str, out_path: str):
    r = run_ffmpeg(["-v", "error", "-y", "-i", stream, "-f", "rawvideo",
                    "-pix_fmt", "yuv420p", out_path])
    err = r.stderr.decode("utf-8", "replace").strip()
    if r.returncode != 0:
        raise Fail("ffmpeg could not decode the stream (exit %d): %s"
                   % (r.returncode, err[:800] or "(no stderr)"))
    if err:
        raise Fail("ffmpeg printed to stderr while decoding: %s" % err[:800])
    if not os.path.exists(out_path):
        raise Fail("ffmpeg did not write a decoded file")


TRACE_RE = re.compile(r"^\s*(\d+)\s+(\S+)\s+([01]+)\s*=\s*(-?\d+)\s*$")
TRACE_TAG = re.compile(r"^\[trace_headers[^\]]*\]\s?(.*)$")


def trace_sections(stream: str):
    """Parse the trace_headers dump into [{'title','packet','fields'}]."""
    lines = []
    last = None
    # ffmpeg n9.0.2 prints no trace lines at -v error; retry at -v info and
    # keep only the [trace_headers] lines.  Same bitstream filter either way.
    for level in ("error", "info"):
        r = run_ffmpeg(["-v", level, "-i", stream, "-c", "copy",
                        "-bsf:v", "trace_headers", "-f", "null", "-"])
        last = r
        lines = [l for l in r.stderr.decode("utf-8", "replace").splitlines()
                 if "[trace_headers" in l]
        if lines:
            break
    if not lines:
        err = (last.stderr.decode("utf-8", "replace").strip() if last else "")
        raise Fail("trace_headers produced no header dump%s"
                   % (": " + err[:500] if err else ""))

    sections = []
    packet = 0
    cur = None
    for line in lines:
        m = TRACE_TAG.match(line)
        if not m:
            continue
        rest = m.group(1).strip()
        if not rest:
            continue
        if rest.startswith("Packet:"):
            packet += 1
            cur = None
            continue
        fm = TRACE_RE.match(rest)
        if fm:
            if cur is None:
                raise Fail("trace_headers: field %r outside a section" % fm.group(2))
            cur["fields"][fm.group(2)] = int(fm.group(4))
        else:
            cur = {"title": rest, "packet": packet, "fields": {}}
            sections.append(cur)
    if not sections:
        raise Fail("trace_headers: no sections parsed")
    return sections, packet


def need(sec, field, want, errors):
    if field not in sec["fields"]:
        errors.append("%s: %s missing" % (sec["title"], field))
    elif sec["fields"][field] != want:
        errors.append("%s: %s is %d, expected %d"
                      % (sec["title"], field, sec["fields"][field], want))


def check_headers(sections, packets, types, w, h, nframes, expect):
    errors = []
    if expect.get("skip-headers"):
        return []
    mbw, mbh, crop_r, crop_b = expected_crop(w, h)
    chroma_qp = expect.get("chroma-qp-index", 0)

    sps = [s for s in sections if s["title"] == "Sequence Parameter Set"]
    pps = [s for s in sections if s["title"] == "Picture Parameter Set"]
    if not sps:
        errors.append("no Sequence Parameter Set in the stream")
    if not pps:
        errors.append("no Picture Parameter Set in the stream")

    # The encoder's own VUI timing gives the exact frame rate for the level
    # check (n2avc.h writes num_units_in_tick = fps_den, time_scale = 2*fps_num).
    fps_num, fps_den = 30, 1
    if sps:
        nu = sps[0]["fields"].get("num_units_in_tick")
        ts = sps[0]["fields"].get("time_scale")
        if nu and ts and nu > 0:
            fps_num, fps_den = ts, 2 * nu
    level = expect.get("level", expected_level(w, h, fps_num, fps_den))

    for s in sps:
        need(s, "profile_idc", expect.get("profile", 66), errors)
        need(s, "constraint_set0_flag", 1, errors)
        need(s, "constraint_set1_flag", 1, errors)
        need(s, "level_idc", level, errors)
        need(s, "pic_order_cnt_type", 2, errors)
        need(s, "max_num_ref_frames", 1, errors)
        need(s, "frame_mbs_only_flag", 1, errors)
        need(s, "pic_width_in_mbs_minus1", mbw - 1, errors)
        need(s, "pic_height_in_map_units_minus1", mbh - 1, errors)
        if crop_r or crop_b:
            need(s, "frame_cropping_flag", 1, errors)
            need(s, "frame_crop_left_offset", 0, errors)
            need(s, "frame_crop_top_offset", 0, errors)
            need(s, "frame_crop_right_offset", crop_r, errors)
            need(s, "frame_crop_bottom_offset", crop_b, errors)
        else:
            need(s, "frame_cropping_flag", 0, errors)
        need(s, "max_num_reorder_frames", 0, errors)
    for p in pps:
        need(p, "entropy_coding_mode_flag", 0, errors)
        need(p, "pic_init_qp_minus26", 0, errors)
        need(p, "chroma_qp_index_offset", chroma_qp, errors)
        need(p, "deblocking_filter_control_present_flag", 1, errors)

    seq = [t for t in types if not (expect.get("allow-sei") and t == 6)]
    if seq[:3] != [7, 8, 5]:
        errors.append("NAL type sequence starts %s, expected 7, 8, 5%s"
                      % (seq[:3], " (SEI allowed)" if expect.get("allow-sei") else ""))

    slices_want = expect.get("slices")
    per_packet = [0] * (packets + 1)
    for s in sections:
        if s["title"] == "Slice Header" and 0 < s["packet"] <= packets:
            per_packet[s["packet"]] += 1
    counted = sum(per_packet)
    if packets != nframes:
        errors.append("ffmpeg demuxed %d packets for %d frames" % (packets, nframes))
    if slices_want is not None:
        bad = [(i, c) for i, c in enumerate(per_packet) if i and c != slices_want]
        if bad:
            errors.append("slices per picture: packet %d has %d, expected %d"
                          % (bad[0][0], bad[0][1], slices_want))
        if counted != nframes * slices_want:
            errors.append("counted %d slice headers for %d pictures x %d slices"
                          % (counted, nframes, slices_want))
    elif counted < nframes:
        errors.append("counted only %d slice headers for %d pictures"
                      % (counted, nframes))
    return errors


def load_stats(path: str):
    with open(path, "r", encoding="utf-8") as f:
        d = json.load(f)
    if isinstance(d, dict):
        for key in ("pictures", "frames", "stats"):
            if key in d and isinstance(d[key], list):
                d = d[key]
                break
    if not isinstance(d, list):
        raise Fail("stats.json: expected a list of per-picture objects")
    qps = []
    for i, e in enumerate(d):
        if not isinstance(e, dict) or "qp" not in e:
            raise Fail("stats.json: picture %d has no qp" % i)
        qps.append(int(e["qp"]))
    return qps


def psnr_floor(qp: int) -> float:
    if qp <= 12:
        return 44.0
    if qp <= 26:
        return 36.0
    if qp <= 32:
        return 32.0
    if qp <= 40:
        return 27.0
    return 22.0


# ------------------------------------------------------------------- checks

def check_stream(args):
    stream = args.stream
    if not os.path.exists(stream):
        raise Fail("stream %s does not exist" % stream)
    data = open(stream, "rb").read()
    types, n_nals = scan_annexb(data)
    w, h = parse_size(args.size)
    if args.frames < 1:
        raise Fail("--frames must be >= 1")
    expect = parse_expect(args.expect)
    if expect.get("slices") is not None and expect["slices"] < 1:
        raise Fail("--expect slices must be >= 1")

    # (b) the reconstruction, before spending time on ffmpeg
    fs = yuv.frame_size(w, h)
    if args.recon:
        if not os.path.exists(args.recon):
            raise Fail("reconstruction %s does not exist" % args.recon)
        rsize = os.path.getsize(args.recon)
        if rsize != args.frames * fs:
            raise Fail("reconstruction is %d bytes, expected %d (%d frames of %dx%d I420)"
                       % (rsize, args.frames * fs, args.frames, w, h))

    tmpdir = tempfile.mkdtemp(prefix="n2h264-")
    try:
        dec = os.path.join(tmpdir, "decoded.yuv")
        decode_stream(stream, dec)
        dsize = os.path.getsize(dec)
        if dsize != args.frames * fs:
            raise Fail("ffmpeg decoded %d bytes = %.3f frames, expected exactly %d frames of %dx%d"
                       % (dsize, dsize / float(fs), args.frames, w, h))

        if args.recon:
            d = yuv.first_diff(args.recon, dec, w, h, args.frames)
            if d is not None:
                frame, plane, x, y = d
                mb = (y // 16) * ((w + 15) // 16) + x // 16
                raise Fail("reconstruction mismatch at frame=%d plane=%d x=%d y=%d mb=%d "
                           "(0=Y 1=Cb 2=Cr; recon vs ffmpeg)"
                           % (frame, plane, x, y, mb))

        # (d) the headers -- before the PSNR gate, so a quality failure still
        # proves the stream syntax was checked
        sections, packets = trace_sections(stream)
        errors = check_headers(sections, packets, types, w, h, args.frames, expect)
        if errors:
            raise Fail("header check: " + "; ".join(errors[:6]))

        # (c) PSNR against the source
        min_psnr = None
        if args.source:
            if not os.path.exists(args.source):
                raise Fail("source %s does not exist" % args.source)
            ssize = os.path.getsize(args.source)
            if ssize != args.frames * fs:
                raise Fail("source is %d bytes, expected %d (%d frames of %dx%d I420)"
                           % (ssize, args.frames * fs, args.frames, w, h))
            if args.stats:
                qps = load_stats(args.stats)
            elif expect.get("qp") is not None:
                qps = [int(expect["qp"])] * args.frames
            else:
                qps = None
            if args.psnr_floors:
                with open(args.psnr_floors, "r", encoding="utf-8") as f:
                    floors = json.load(f)
                if not isinstance(floors, list) or len(floors) < args.frames:
                    raise Fail("--psnr-floors needs a JSON list of at least %d numbers"
                               % args.frames)
            elif qps is None:
                raise Fail("--source needs --stats (per-picture qp), --psnr-floors "
                           "or --expect qp=N")
            elif len(qps) < args.frames:
                raise Fail("stats.json has %d pictures, the stream has %d"
                           % (len(qps), args.frames))
            gen = yuv.psnr_frames(args.source, dec, w, h, args.frames)
            for i, (py, _pcb, _pcr) in enumerate(gen):
                floor = float(floors[i]) if args.psnr_floors else psnr_floor(qps[i])
                if py < floor:
                    why = ("--psnr-floors[%d]" % i) if args.psnr_floors else ("QP %d" % qps[i])
                    raise Fail("frame %d: luma PSNR %.2f dB is below the %.1f dB floor for %s"
                               % (i, py, floor, why))
                if min_psnr is None or py < min_psnr:
                    min_psnr = py

        bits = ["frames=%d" % args.frames, "size=%dx%d" % (w, h),
                "nals=%d" % n_nals, "decoded=exact"]
        if args.recon:
            bits.append("recon=byte-identical")
        if args.source:
            bits.append("minY=%.2fdB" % min_psnr)
        bits.append("headers=ok")
        if expect.get("slices") is not None:
            bits.append("slices=%d" % expect["slices"])
        return " ".join(bits)
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def parse_expect(items):
    out = {}
    for it in items or []:
        if "=" in it:
            k, v = it.split("=", 1)
        else:
            k, v = it, True
        k = k.strip()
        if k == "skip-headers":
            out["skip-headers"] = True
        elif k == "allow-sei":
            out["allow-sei"] = True
        elif k in ("slices", "level", "profile", "qp", "chroma-qp-index"):
            try:
                out[k] = int(v)
            except ValueError:
                raise Fail("--expect %s needs an integer, got %r" % (k, v))
        else:
            raise Fail("unknown --expect key %r (supported: skip-headers, allow-sei, "
                       "slices=N, level=N, profile=N, qp=N, chroma-qp-index=N)" % k)
    return out


def main(argv):
    ap = argparse.ArgumentParser(description="independent H.264 stream judge (WP10)")
    ap.add_argument("--stream")
    ap.add_argument("--recon")
    ap.add_argument("--source")
    ap.add_argument("--size")
    ap.add_argument("--frames", type=int)
    ap.add_argument("--stats")
    ap.add_argument("--psnr-floors", help="JSON list of per-frame luma PSNR floors "
                                          "(overrides the QP table; for content-aware runs)")
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--scan", help="run only the Annex-B scanner on this file")
    args = ap.parse_args(argv)

    try:
        if args.scan:
            data = open(args.scan, "rb").read()
            types, n = scan_annexb(data)
            print("SCAN ok nals=%d types=%s" % (n, ",".join(str(t) for t in types)))
            print("ORACLE h264 PASS scan nals=%d" % n)
            return 0
        if not args.stream:
            raise Fail("--stream is required (or --scan)")
        if not args.size or args.frames is None:
            raise Fail("--size WxH and --frames N are required")
        summary = check_stream(args)
        print("ORACLE h264 PASS " + summary)
        return 0
    except Fail as e:
        print("ORACLE h264 FAIL: %s" % e)
        return 1
    except (OSError, ValueError) as e:
        print("ORACLE h264 FAIL: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
