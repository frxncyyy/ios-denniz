#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent judge for MP4/MOV files written by the phone (or by ffmpeg).

Usage:
  check_mp4.py FILE --expect-video avc1|hvc1 --size WxH --min-frames N [--audio aac]

Two independent views of the file:
  * ffprobe -v error -show_streams -show_format -of json: codec, dimensions,
    frame count (nb_frames, or packets counted with -count_packets),
    duration, audio stream; then a full ffmpeg decode must exit 0;
  * a box walker written here (ISO/IEC 14496-12): ftyp/moov/mdat present,
    the video trak's stsd sample entry type, avcC/hvcC present, and the SPS
    inside it parsed down to the same width and height as --size.

Exit 0 with "ORACLE mp4 PASS ..."; exit 1 with "ORACLE mp4 FAIL: reason".
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys

FFMPEG = os.environ.get("N2_FFMPEG", "ffmpeg")
FFPROBE = os.environ.get("N2_FFPROBE", "ffprobe")

VIDEO_CODECS = {"avc1": "h264", "hvc1": "hevc"}
CONTAINERS = {"moov", "trak", "mdia", "minf", "stbl", "dinf", "edts", "udta",
              "mvex", "moof", "traf"}
VISUAL_ENTRIES = {"avc1", "avc3", "hvc1", "hev1", "mp4v", "jpeg", "mjpg"}


class Fail(Exception):
    pass


def be16(b, o):
    return (b[o] << 8) | b[o + 1]


def be32(b, o):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def box_children(data, start, end, where="file"):
    """Yield (type, payload_start, payload_end) for boxes in [start, end)."""
    off = start
    out = []
    while off + 8 <= end:
        size = be32(data, off)
        typ = data[off + 4:off + 8].decode("latin-1")
        hdr = 8
        if size == 1:
            if off + 16 > end:
                raise Fail("%s: truncated 64-bit box size for %r" % (where, typ))
            size = (be32(data, off + 8) << 32) | be32(data, off + 12)
            hdr = 16
        elif size == 0:
            size = end - off
        if size < hdr:
            raise Fail("%s: box %r at byte %d has impossible size %d" % (where, typ, off, size))
        if off + size > end:
            raise Fail("%s: box %r at byte %d extends %d bytes past the end of the file"
                       % (where, typ, off, off + size - end))
        out.append((typ, off + hdr, off + size))
        off += size
    if off != end:
        raise Fail("%s: %d trailing byte(s) after the last box" % (where, end - off))
    return out


def top_level(data):
    return box_children(data, 0, len(data), "top level")


def find_all(data, start, end, want, depth=0):
    """All boxes of type `want` below [start,end) through known containers."""
    found = []
    if depth > 8:
        return found
    for typ, ps, pe in box_children(data, start, end):
        if typ == want:
            found.append((ps, pe))
        if typ in CONTAINERS:
            found.extend(find_all(data, ps, pe, want, depth + 1))
    return found


def trak_info(data, ts, te):
    """(handler_type, [(sample_entry_type, payload_start, payload_end)])."""
    handler = None
    entries = []

    def rec(start, end):
        nonlocal handler
        for typ, ps, pe in box_children(data, start, end, "trak"):
            if typ == "hdlr" and pe - ps >= 12:
                handler = data[ps + 8:ps + 12].decode("latin-1")
            elif typ == "stsd":
                if pe - ps < 8:
                    raise Fail("stsd box too short")
                for etyp, eps, epe in box_children(data, ps + 8, pe, "stsd"):
                    entries.append((etyp, eps, epe))
            elif typ in CONTAINERS:
                rec(ps, pe)

    rec(ts, te)
    return handler, entries


class Bits:
    def __init__(self, data):
        self.d = data
        self.pos = 0
        self.n = len(data) * 8

    def u(self, k):
        if self.pos + k > self.n:
            raise Fail("SPS: bit reader ran past the end")
        v = 0
        for _ in range(k):
            byte = self.d[self.pos >> 3]
            bit = (byte >> (7 - (self.pos & 7))) & 1
            v = (v << 1) | bit
            self.pos += 1
        return v

    def ue(self):
        zeros = 0
        while True:
            if self.pos >= self.n:
                raise Fail("SPS: truncated Exp-Golomb code")
            b = self.u(1)
            if b:
                break
            zeros += 1
            if zeros > 31:
                raise Fail("SPS: invalid Exp-Golomb code")
        return (1 << zeros) - 1 + (self.u(zeros) if zeros else 0)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k & 1 else -(k // 2)


def unescape(rbsp):
    out = bytearray()
    zeros = 0
    for b in rbsp:
        if zeros >= 2 and b == 3:
            zeros = 0
            continue
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def parse_sps_avc(nal):
    if len(nal) < 4:
        raise Fail("avcC SPS is %d bytes, too short" % len(nal))
    r = unescape(nal[1:])
    br = Bits(r)
    profile = br.u(8)
    br.u(8)  # constraint flags + reserved
    br.u(8)  # level_idc
    br.ue()  # seq_parameter_set_id
    chroma = 1
    if profile in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        chroma = br.ue()
        if chroma == 3:
            br.u(1)
        br.ue()  # bit_depth_luma_minus8
        br.ue()  # bit_depth_chroma_minus8
        br.u(1)  # qpprime_y_zero_transform_bypass_flag
        if br.u(1):  # seq_scaling_matrix_present_flag
            for i in range(8 if chroma != 3 else 12):
                if br.u(1):
                    size = 16 if i < 6 else 64
                    last, nxt = 8, 8
                    for _ in range(size):
                        if nxt:
                            nxt = (last + br.se() + 256) % 256
                        v = nxt if nxt else last
                        last = v
    br.ue()  # log2_max_frame_num_minus4
    poc = br.ue()
    if poc == 0:
        br.ue()
    elif poc == 1:
        br.u(1)
        br.se()
        br.se()
        for _ in range(br.ue()):
            br.se()
    br.ue()  # max_num_ref_frames
    br.u(1)  # gaps_in_frame_num_allowed_flag
    mbw = br.ue() + 1
    mbh = br.ue() + 1
    frame_mbs_only = br.u(1)
    if not frame_mbs_only:
        br.u(1)
    br.u(1)  # direct_8x8_inference_flag
    crop = (0, 0, 0, 0)
    if br.u(1):
        crop = (br.ue(), br.ue(), br.ue(), br.ue())
    crop_x = 2 if chroma == 1 else 1
    crop_y = (2 if chroma == 1 else 1) * (2 - frame_mbs_only)
    width = mbw * 16 - crop_x * (crop[0] + crop[1])
    height = (2 - frame_mbs_only) * mbh * 16 - crop_y * (crop[2] + crop[3])
    return width, height


def parse_sps_hevc(nal):
    if len(nal) < 4:
        raise Fail("hvcC SPS is %d bytes, too short" % len(nal))
    r = unescape(nal[2:])
    br = Bits(r)
    br.u(4)  # sps_video_parameter_set_id
    max_sub = br.u(3)
    br.u(1)  # sps_temporal_id_nesting_flag
    # profile_tier_level(profilePresentFlag=1, max_sub)
    br.u(2)   # general_profile_space
    br.u(1)   # general_tier_flag
    br.u(5)   # general_profile_idc
    br.u(32)  # general_profile_compatibility_flags
    br.u(48)  # general_constraint_indicator_flags
    br.u(8)   # general_level_idc
    prof = [br.u(1) for _ in range(max_sub)]
    lvl = [br.u(1) for _ in range(max_sub)]
    if max_sub > 0:
        for _ in range(8 - max_sub):
            br.u(2)
    for i in range(max_sub):
        if prof[i]:
            br.u(88)
        if lvl[i]:
            br.u(8)
    br.ue()  # sps_seq_parameter_set_id
    chroma = br.ue()
    if chroma == 3:
        br.u(1)
    width = br.ue()
    height = br.ue()
    if br.u(1):  # conformance_window_flag
        left, right, top, bottom = br.ue(), br.ue(), br.ue(), br.ue()
        sub_w = 2 if chroma in (1, 2) else 1
        sub_h = 2 if chroma == 1 else 1
        width -= sub_w * (left + right)
        height -= sub_h * (top + bottom)
    return width, height


def parse_avcc(payload):
    if len(payload) < 7:
        raise Fail("avcC box is %d bytes, too short" % len(payload))
    n = payload[5] & 0x1F
    off = 6
    sps = None
    for _ in range(n):
        if off + 2 > len(payload):
            raise Fail("avcC: truncated SPS length")
        ln = be16(payload, off)
        off += 2
        if off + ln > len(payload):
            raise Fail("avcC: truncated SPS")
        if sps is None:
            sps = payload[off:off + ln]
        off += ln
    if sps is None:
        raise Fail("avcC contains no SPS")
    return parse_sps_avc(sps)


def parse_hvcc(payload):
    if len(payload) < 23:
        raise Fail("hvcC box is %d bytes, too short" % len(payload))
    n_arrays = payload[22]
    off = 23
    sps = None
    for _ in range(n_arrays):
        if off + 3 > len(payload):
            raise Fail("hvcC: truncated array header")
        typ = payload[off] & 0x3F
        num = be16(payload, off + 1)
        off += 3
        for _ in range(num):
            if off + 2 > len(payload):
                raise Fail("hvcC: truncated NAL length")
            ln = be16(payload, off)
            off += 2
            if off + ln > len(payload):
                raise Fail("hvcC: truncated NAL")
            if typ == 33 and sps is None:
                sps = payload[off:off + ln]
            off += ln
    if sps is None:
        raise Fail("hvcC contains no SPS (NAL type 33)")
    return parse_sps_hevc(sps)


def box_check(path, data, want, w, h):
    types = [t for t, _ps, _pe in top_level(data)]
    for typ in ("ftyp", "moov", "mdat"):
        if typ not in types:
            raise Fail("top-level box %r is missing (found %s)" % (typ, ",".join(types)))

    moov = [p for t, ps, pe in top_level(data) if t == "moov" for p in [(ps, pe)]]
    if not moov:
        raise Fail("no moov box")
    traks = find_all(data, moov[0][0], moov[0][1], "trak")
    if not traks:
        raise Fail("moov contains no trak")
    video = None
    audio = 0
    for ts, te in traks:
        handler, entries = trak_info(data, ts, te)
        if handler == "vide" and video is None:
            video = entries
        elif handler == "soun":
            audio += 1
    if video is None:
        raise Fail("no video trak (hdlr 'vide')")
    if not video:
        raise Fail("video trak has no sample description (stsd)")

    entry = None
    for etyp, eps, epe in video:
        if etyp == want:
            entry = (etyp, eps, epe)
            break
    if entry is None:
        found = ",".join(t for t, _a, _b in video)
        raise Fail("stsd video entry is %s, expected %r" % (found or "(none)", want))

    etyp, eps, epe = entry
    if epe - eps < 78:
        raise Fail("%s sample entry is only %d bytes" % (etyp, epe - eps))
    ew, eh = be16(data, eps + 24), be16(data, eps + 26)
    cfg_type = "avcC" if want in ("avc1", "avc3") else "hvcC"
    cfg = [(ps, pe) for t, ps, pe in box_children(data, eps + 78, epe, etyp)
           if t == cfg_type]
    if not cfg:
        raise Fail("%s sample entry has no %s box" % (etyp, cfg_type))
    payload = data[cfg[0][0]:cfg[0][1]]
    sw, sh = parse_avcc(payload) if cfg_type == "avcC" else parse_hvcc(payload)
    if (sw, sh) != (w, h):
        raise Fail("%s SPS in %s says %dx%d, expected %dx%d"
                   % (etyp, cfg_type, sw, sh, w, h))
    if (ew, eh) != (w, h):
        raise Fail("%s sample entry says %dx%d, expected %dx%d" % (etyp, ew, eh, w, h))
    return audio


def ffprobe_json(path):
    try:
        r = subprocess.run([FFPROBE, "-v", "error", "-show_streams", "-show_format",
                            "-of", "json", path], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=120)
    except FileNotFoundError:
        raise Fail("ffprobe not found (set N2_FFPROBE)")
    if r.returncode != 0:
        raise Fail("ffprobe failed (exit %d): %s"
                   % (r.returncode, r.stderr.decode("utf-8", "replace").strip()[:600]))
    try:
        return json.loads(r.stdout.decode("utf-8", "replace"))
    except json.JSONDecodeError as e:
        raise Fail("ffprobe produced invalid JSON: %s" % e)


def count_packets(path):
    try:
        r = subprocess.run([FFPROBE, "-v", "error", "-select_streams", "v:0",
                            "-count_packets", "-show_entries", "stream=nb_read_packets",
                            "-of", "csv=p=0", path], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=180)
    except FileNotFoundError:
        raise Fail("ffprobe not found (set N2_FFPROBE)")
    if r.returncode != 0:
        raise Fail("ffprobe -count_packets failed: %s"
                   % r.stderr.decode("utf-8", "replace").strip()[:400])
    txt = r.stdout.decode("utf-8", "replace").strip().splitlines()
    if not txt:
        raise Fail("ffprobe -count_packets returned no packet count")
    try:
        return int(txt[0].strip())
    except ValueError:
        raise Fail("ffprobe -count_packets returned %r" % txt[0])


def parse_size(s):
    m = re.fullmatch(r"(\d+)[xX](\d+)", s.strip())
    if not m:
        raise Fail("bad --size %r (want WxH)" % s)
    return int(m.group(1)), int(m.group(2))


def check(args):
    path = args.file
    if not os.path.exists(path):
        raise Fail("%s does not exist" % path)
    if os.path.getsize(path) < 16:
        raise Fail("%s is only %d bytes" % (path, os.path.getsize(path)))
    w, h = parse_size(args.size)
    want = args.expect_video
    if want not in VIDEO_CODECS:
        raise Fail("--expect-video must be avc1 or hvc1")

    data = open(path, "rb").read()
    audio_boxes = box_check(path, data, want, w, h)

    info = ffprobe_json(path)
    streams = info.get("streams", [])
    video = [s for s in streams if s.get("codec_type") == "video"]
    if not video:
        raise Fail("ffprobe sees no video stream")
    v = video[0]
    codec = v.get("codec_name")
    if codec != VIDEO_CODECS[want]:
        raise Fail("ffprobe codec_name is %r, expected %r for %s"
                   % (codec, VIDEO_CODECS[want], want))
    tag = v.get("codec_tag_string")
    if tag != want:
        raise Fail("ffprobe codec_tag_string is %r, expected %r" % (tag, want))
    if int(v.get("width", 0)) != w or int(v.get("height", 0)) != h:
        raise Fail("ffprobe says %sx%s, expected %dx%d"
                   % (v.get("width"), v.get("height"), w, h))

    frames = v.get("nb_frames")
    try:
        nf = int(frames)
    except (TypeError, ValueError):
        nf = -1
    if nf < 0:
        nf = count_packets(path)
    if nf < args.min_frames:
        raise Fail("video has %d frame(s), expected at least %d" % (nf, args.min_frames))

    dur = v.get("duration") or info.get("format", {}).get("duration")
    try:
        durf = float(dur)
    except (TypeError, ValueError):
        raise Fail("no usable duration (video %r, format %r)"
                   % (v.get("duration"), info.get("format", {}).get("duration")))
    if durf <= 0:
        raise Fail("duration is %r" % dur)

    audio = [s for s in streams if s.get("codec_type") == "audio"]
    if args.audio:
        if args.audio == "aac":
            if not any(s.get("codec_name") == "aac" for s in audio):
                raise Fail("no AAC audio stream (found %s)"
                           % (",".join(str(s.get("codec_name")) for s in audio) or "none"))
        else:
            raise Fail("--audio only supports 'aac' for now")
    if bool(audio) != bool(audio_boxes):
        raise Fail("audio tracks disagree between the box walker (%d) and ffprobe (%d)"
                   % (audio_boxes, len(audio)))

    r = subprocess.run([FFMPEG, "-v", "error", "-i", path, "-f", "null", "-"],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
    if r.returncode != 0:
        raise Fail("ffmpeg full decode failed (exit %d): %s"
                   % (r.returncode, r.stderr.decode("utf-8", "replace").strip()[:600]))

    return ("video=%s %dx%d frames=%d duration=%.3fs audio=%s decode=ok"
            % (want, w, h, nf, durf, ",".join(str(s.get("codec_name")) for s in audio) or "none"))


def main(argv):
    ap = argparse.ArgumentParser(description="independent MP4/MOV judge (WP10)")
    ap.add_argument("file")
    ap.add_argument("--expect-video", required=True, help="avc1 or hvc1")
    ap.add_argument("--size", required=True, help="WxH")
    ap.add_argument("--min-frames", type=int, required=True)
    ap.add_argument("--audio", help="aac")
    args = ap.parse_args(argv)
    try:
        summary = check(args)
        print("ORACLE mp4 PASS " + summary)
        return 0
    except Fail as e:
        print("ORACLE mp4 FAIL: %s" % e)
        return 1
    except (OSError, ValueError) as e:
        print("ORACLE mp4 FAIL: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
