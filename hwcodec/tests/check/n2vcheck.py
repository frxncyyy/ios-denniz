#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Common Annex B validator for T02's h264_check.py and hevc_check.py.

No numpy: PSNR is computed with plain Python integers.  ffmpeg/ffprobe are
used as black-box decoders and metadata readers; the SPS is parsed here so a
stream whose headers disagree with its payload is caught even if ffmpeg is
lenient.

A stream passes when:
  * the SPS parses and its geometry equals the expected width/height,
  * ffprobe reports the expected codec, geometry and frame count,
  * ffmpeg decodes every frame (exit 0) to exactly frames*W*H*3/2 bytes,
  * the first frame is a key frame and ffprobe reports at least one key frame,
  * with --source, every frame's Y/U/V PSNR against the raw source is at least
    the threshold (default 30 dB).
"""

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

H264_PROFILES = {
    66: "Baseline", 77: "Main", 88: "Extended",
    100: "High", 110: "High 10", 122: "High 4:2:2", 244: "High 4:4:4",
    44: "CAVLC 4:4:4",
}
HEVC_PROFILES = {
    1: "Main", 2: "Main 10", 3: "Main Still Picture", 4: "Range Extensions",
    5: "High Throughput", 9: "Screen Content Coding", 10: "Screen Content Coding",
}


class CheckError(Exception):
    pass


class ToolError(CheckError):
    """ffmpeg/ffprobe missing: a setup error (exit 2), not a bad stream."""
    pass


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.nbits = len(data) * 8

    def u(self, n):
        if n < 0 or self.pos + n > self.nbits:
            raise CheckError("SPS truncated")
        v = 0
        for _ in range(n):
            byte = self.data[self.pos >> 3]
            bit = (byte >> (7 - (self.pos & 7))) & 1
            v = (v << 1) | bit
            self.pos += 1
        return v

    def ue(self):
        zeros = 0
        while True:
            if self.pos >= self.nbits:
                raise CheckError("SPS truncated in ue")
            b = self.u(1)
            if b:
                break
            zeros += 1
            if zeros > 31:
                raise CheckError("invalid ue(v)")
        if zeros == 0:
            return 0
        return (1 << zeros) - 1 + self.u(zeros)

    def se(self):
        v = self.ue()
        return (v + 1) // 2 if (v & 1) else -(v // 2)


def unescape(rbsp):
    out = bytearray()
    i = 0
    while i < len(rbsp):
        if i + 2 < len(rbsp) and rbsp[i] == 0 and rbsp[i + 1] == 0 and rbsp[i + 2] == 3:
            out += b"\x00\x00"
            i += 3
        else:
            out.append(rbsp[i])
            i += 1
    return bytes(out)


def annexb_nals(data):
    """-> [(offset, nal_bytes)] with start codes removed and trailing zeros cut."""
    starts = []
    i = 0
    while i + 3 <= len(data):
        if data[i:i + 3] == b"\x00\x00\x01":
            starts.append(i + 3)
            i += 3
        else:
            i += 1
    nals = []
    for n, start in enumerate(starts):
        end = starts[n + 1] - 3 if n + 1 < len(starts) else len(data)
        nal = data[start:end].rstrip(b"\x00")
        if nal:
            nals.append((start, nal))
    return nals


def skip_scaling_list(br, size):
    last = 8
    nxt = 8
    for _ in range(size):
        if nxt != 0:
            nxt = (last + br.se() + 256) % 256
            last = nxt if nxt != 0 else last


def chroma_subsampling(chroma_format_idc, separate_colour_plane):
    if separate_colour_plane or chroma_format_idc == 0:
        return 1, 1
    if chroma_format_idc == 1:
        return 2, 2
    if chroma_format_idc == 2:
        return 2, 1
    return 1, 1


def h264_sps(nal):
    if not nal or (nal[0] & 0x1F) != 7:
        raise CheckError("no H.264 SPS NAL (type 7)")
    br = BitReader(unescape(nal[1:]))
    profile_idc = br.u(8)
    constraint = br.u(8)
    level_idc = br.u(8)
    sps_id = br.ue()
    chroma = 1
    separate = 0
    depth_luma = 8
    depth_chroma = 8
    if profile_idc in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        chroma = br.ue()
        if chroma == 3:
            separate = br.u(1)
        depth_luma = br.ue() + 8
        depth_chroma = br.ue() + 8
        br.u(1)  # qpprime_y_zero_transform_bypass_flag
        if br.u(1):
            for i in range(8 if chroma != 3 else 12):
                if br.u(1):
                    skip_scaling_list(br, 16 if i < 6 else 64)
    br.ue()  # log2_max_frame_num_minus4
    poc_type = br.ue()
    if poc_type == 0:
        br.ue()
    elif poc_type == 1:
        br.u(1)
        br.se()
        br.se()
        for _ in range(br.ue()):
            br.se()
    br.ue()  # max_num_ref_frames
    br.u(1)  # gaps_in_frame_num_value_allowed_flag
    width_mbs = br.ue() + 1
    height_map = br.ue() + 1
    frame_mbs_only = br.u(1)
    if not frame_mbs_only:
        br.u(1)
    br.u(1)  # direct_8x8_inference_flag
    crop_left = crop_right = crop_top = crop_bottom = 0
    if br.u(1):
        crop_left = br.ue()
        crop_right = br.ue()
        crop_top = br.ue()
        crop_bottom = br.ue()
    sub_w, sub_h = chroma_subsampling(chroma, separate)
    width = width_mbs * 16 - (crop_left + crop_right) * sub_w
    height = (2 - frame_mbs_only) * height_map * 16 - (crop_top + crop_bottom) * sub_h * (2 - frame_mbs_only)
    return {
        "profile_idc": profile_idc,
        "profile": H264_PROFILES.get(profile_idc, f"profile {profile_idc}"),
        "constraint_flags": constraint,
        "level_idc": level_idc,
        "level": level_idc / 10.0,
        "sps_id": sps_id,
        "width": width,
        "height": height,
        "chroma_format_idc": chroma,
        "bit_depth_luma": depth_luma,
        "bit_depth_chroma": depth_chroma,
        "frame_mbs_only": frame_mbs_only,
    }


def hevc_sps(nal):
    if len(nal) < 3 or ((nal[0] >> 1) & 0x3F) != 33:
        raise CheckError("no HEVC SPS NAL (type 33)")
    br = BitReader(unescape(nal[2:]))
    br.u(4)  # sps_video_parameter_set_id
    max_sub = br.u(3)
    br.u(1)  # sps_temporal_id_nesting_flag
    profile_space = br.u(2)
    tier = br.u(1)
    profile_idc = br.u(5)
    compat = br.u(32)
    constraints = br.u(48)
    level_idc = br.u(8)
    sub_profile = [br.u(1) for _ in range(max_sub)]
    sub_level = [br.u(1) for _ in range(max_sub)]
    if max_sub > 0:
        for _ in range(max_sub, 8):
            br.u(2)
    for i in range(max_sub):
        if sub_profile[i]:
            br.u(2)
            br.u(1)
            br.u(5)
            br.u(32)
            br.u(48)
        if sub_level[i]:
            br.u(8)
    br.ue()  # sps_seq_parameter_set_id
    chroma = br.ue()
    separate = 0
    if chroma == 3:
        separate = br.u(1)
    width = br.ue()
    height = br.ue()
    crop_left = crop_right = crop_top = crop_bottom = 0
    if br.u(1):
        crop_left = br.ue()
        crop_right = br.ue()
        crop_top = br.ue()
        crop_bottom = br.ue()
    depth_luma = br.ue() + 8
    depth_chroma = br.ue() + 8
    sub_w, sub_h = chroma_subsampling(chroma, separate)
    width -= (crop_left + crop_right) * sub_w
    height -= (crop_top + crop_bottom) * sub_h
    return {
        "profile_idc": profile_idc,
        "profile_space": profile_space,
        "tier": tier,
        "profile": HEVC_PROFILES.get(profile_idc, f"profile {profile_idc}"),
        "compatibility_flags": compat,
        "constraint_flags": constraints,
        "level_idc": level_idc,
        "level": level_idc / 30.0,
        "width": width,
        "height": height,
        "chroma_format_idc": chroma,
        "bit_depth_luma": depth_luma,
        "bit_depth_chroma": depth_chroma,
    }


def run(cmd, what):
    try:
        return subprocess.run(cmd, capture_output=True, check=False)
    except FileNotFoundError:
        raise ToolError(f"{what}: executable not found: {cmd[0]}")


def ffprobe_stream(path, fmt):
    r = run(["ffprobe", "-v", "error", "-f", fmt, "-select_streams", "v:0", "-count_frames",
             "-show_entries", "stream=codec_name,profile,level,width,height,pix_fmt,nb_read_frames",
             "-of", "json", str(path)], "ffprobe")
    if r.returncode != 0:
        raise CheckError("ffprobe failed: " + r.stderr.decode("utf-8", "replace").strip())
    try:
        streams = json.loads(r.stdout.decode("utf-8", "replace")).get("streams", [])
    except json.JSONDecodeError as e:
        raise CheckError(f"ffprobe JSON: {e}")
    if not streams:
        raise CheckError("ffprobe found no video stream")
    s = streams[0]
    frames = s.get("nb_read_frames")
    try:
        frames = int(frames)
    except (TypeError, ValueError):
        frames = None
    return {
        "codec_name": s.get("codec_name"),
        "profile": s.get("profile"),
        "level": s.get("level"),
        "width": s.get("width"),
        "height": s.get("height"),
        "pix_fmt": s.get("pix_fmt"),
        "frames": frames,
    }


def ffprobe_keyframes(path, fmt):
    r = run(["ffprobe", "-v", "error", "-f", fmt, "-select_streams", "v:0", "-show_frames",
             "-show_entries", "frame=key_frame,pict_type", "-of", "csv=p=0", str(path)], "ffprobe")
    if r.returncode != 0:
        raise CheckError("ffprobe frames failed: " + r.stderr.decode("utf-8", "replace").strip())
    keys = []
    count = 0
    for line in r.stdout.decode("utf-8", "replace").splitlines():
        line = line.strip()
        if not line or "," not in line:
            continue
        parts = line.split(",")
        try:
            key = int(parts[0])
        except ValueError:
            continue
        if key:
            keys.append(count)
        count += 1
    return keys, count


def decode_raw(path, fmt, frames, width, height, pix_fmt):
    r = run(["ffmpeg", "-v", "error", "-nostdin", "-f", fmt, "-i", str(path), "-map", "0:v:0",
             "-f", "rawvideo", "-pix_fmt", pix_fmt, "-"], "ffmpeg")
    if r.returncode != 0:
        raise CheckError("ffmpeg decode failed: " + r.stderr.decode("utf-8", "replace").strip())
    frame_size = width * height + 2 * ((width + 1) // 2) * ((height + 1) // 2)
    if len(r.stdout) != frames * frame_size:
        raise CheckError(f"decoded {len(r.stdout)} bytes, expected {frames} frames x {frame_size} = {frames * frame_size}")
    return r.stdout, frame_size


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


def frame_planes(raw, frame_size, width, height):
    ysize = width * height
    csize = ((width + 1) // 2) * ((height + 1) // 2)
    y = raw[:ysize]
    u = raw[ysize:ysize + csize]
    v = raw[ysize + csize:ysize + 2 * csize]
    if len(y) + len(u) + len(v) != frame_size:
        raise CheckError("internal plane size mismatch")
    return y, u, v


def source_planes(src, frames, width, height, fmt="nv12"):
    ysize = width * height
    csize = ((width + 1) // 2) * ((height + 1) // 2)
    frame_size = ysize + 2 * csize
    if len(src) != frames * frame_size:
        raise CheckError(f"source is {len(src)} bytes, expected {frames} x {frame_size} = {frames * frame_size}")
    for n in range(frames):
        off = n * frame_size
        y = src[off:off + ysize]
        if fmt == "nv12":
            c = src[off + ysize:off + ysize + 2 * csize]
            u = c[0::2]
            v = c[1::2]
        else:
            u = src[off + ysize:off + ysize + csize]
            v = src[off + ysize + csize:off + ysize + 2 * csize]
        yield y, u, v


def check(kind, path, width, height, frames, source=None, min_psnr=30.0, min_psnr_chroma=None,
          allow_any_pix_fmt=False, source_format="nv12"):
    if min_psnr_chroma is None:
        min_psnr_chroma = min_psnr
    rep = {
        "file": str(path), "kind": kind, "expected": {"width": width, "height": height, "frames": frames},
        "errors": [], "frames": [],
    }
    data = Path(path).read_bytes()
    nals = annexb_nals(data)
    if not nals:
        raise CheckError("no Annex B start code (00 00 01) in the file")
    rep["nals"] = len(nals)

    sps = None
    sps_error = None
    for _off, nal in nals:
        try:
            sps = h264_sps(nal) if kind == "h264" else hevc_sps(nal)
            break
        except CheckError as e:
            sps_error = str(e)
            continue
    if sps is None:
        raise CheckError("no usable SPS: " + (sps_error or "none found"))
    rep["sps"] = sps

    if sps["width"] != width or sps["height"] != height:
        rep["errors"].append(f"SPS geometry {sps['width']}x{sps['height']} != {width}x{height}")

    st = ffprobe_stream(path, kind)
    rep["ffprobe"] = st
    if st["codec_name"] != kind:
        rep["errors"].append(f"ffprobe codec {st['codec_name']} != {kind}")
    if st["width"] != width or st["height"] != height:
        rep["errors"].append(f"ffprobe geometry {st['width']}x{st['height']} != {width}x{height}")
    if st["frames"] is not None and st["frames"] != frames:
        rep["errors"].append(f"ffprobe frame count {st['frames']} != {frames}")
    if not allow_any_pix_fmt and st["pix_fmt"] and not (st["pix_fmt"].startswith("yuv420p") or st["pix_fmt"].startswith("yuvj420p")):
        rep["errors"].append(f"pixel format {st['pix_fmt']} is not 4:2:0")

    keys, key_count = ffprobe_keyframes(path, kind)
    rep["key_frames"] = keys
    rep["key_frame_count"] = len(keys)
    if not keys:
        rep["errors"].append("no key frame reported")
    elif keys[0] != 0:
        rep["errors"].append(f"first frame is not a key frame (first key at {keys[0]})")
    if key_count != frames:
        rep["errors"].append(f"ffprobe decoded {key_count} frames, expected {frames}")

    raw, frame_size = decode_raw(path, kind, frames, width, height, "yuv420p")
    rep["decoded_bytes"] = len(raw)

    src = None
    src_iter = None
    if source is not None:
        src = Path(source).read_bytes()
        src_iter = source_planes(src, frames, width, height, source_format)

    min_y = min_u = min_v = 99.0
    min_all = 99.0
    for n in range(frames):
        y, u, v = frame_planes(raw[n * frame_size:(n + 1) * frame_size], frame_size, width, height)
        row = {"frame": n, "key": n in keys}
        if src_iter is not None:
            sy, su, sv = next(src_iter)
            py, pu, pv = psnr(y, sy), psnr(u, su), psnr(v, sv)
            all_a = y + u + v
            all_b = sy + su + sv
            pa = psnr(all_a, all_b)
            row.update({"psnr_y": round(py, 3), "psnr_u": round(pu, 3), "psnr_v": round(pv, 3), "psnr": round(pa, 3)})
            min_y = min(min_y, py)
            min_u = min(min_u, pu)
            min_v = min(min_v, pv)
            min_all = min(min_all, pa)
            if py < min_psnr:
                rep["errors"].append(f"frame {n}: Y PSNR {py:.2f} < {min_psnr}")
            if pu < min_psnr_chroma:
                rep["errors"].append(f"frame {n}: U PSNR {pu:.2f} < {min_psnr_chroma}")
            if pv < min_psnr_chroma:
                rep["errors"].append(f"frame {n}: V PSNR {pv:.2f} < {min_psnr_chroma}")
        rep["frames"].append(row)

    rep["min_psnr_y"] = round(min_y, 3)
    rep["min_psnr_u"] = round(min_u, 3)
    rep["min_psnr_v"] = round(min_v, 3)
    rep["min_psnr"] = round(min_all, 3)
    rep["verdict"] = "PASS" if not rep["errors"] else "FAIL"
    return rep


def print_report(rep, quiet=False):
    e = rep["expected"]
    sps = rep["sps"]
    print(f"file: {rep['file']}")
    print(f"codec: {rep['kind']} profile={sps['profile']} level={sps['level']:g} "
          f"(profile_idc={sps['profile_idc']} level_idc={sps['level_idc']})")
    print(f"sps: {sps['width']}x{sps['height']} chroma_format_idc={sps['chroma_format_idc']} "
          f"bit_depth={sps['bit_depth_luma']}/{sps['bit_depth_chroma']}")
    fp = rep["ffprobe"]
    print(f"ffprobe: {fp['codec_name']} {fp['width']}x{fp['height']} frames={fp['frames']} pix_fmt={fp['pix_fmt']} "
          f"profile={fp['profile']} level={fp['level']}")
    print(f"expected: {e['width']}x{e['height']} frames={e['frames']}")
    print(f"key_frames: {','.join(str(k) for k in rep['key_frames'][:40])}"
          f"{' ...' if len(rep['key_frames']) > 40 else ''} ({rep['key_frame_count']})")
    if not quiet:
        for row in rep["frames"]:
            if "psnr_y" in row:
                print(f"frame {row['frame']} key={int(row['key'])} psnr_y={row['psnr_y']:.3f} "
                      f"psnr_u={row['psnr_u']:.3f} psnr_v={row['psnr_v']:.3f} psnr={row['psnr']:.3f}")
            else:
                print(f"frame {row['frame']} key={int(row['key'])}")
    if "min_psnr_y" in rep and rep["min_psnr_y"] != 99.0:
        print(f"min_psnr: y={rep['min_psnr_y']:.3f} u={rep['min_psnr_u']:.3f} v={rep['min_psnr_v']:.3f} "
              f"all={rep['min_psnr']:.3f}")
    for err in rep["errors"][:20]:
        print(f"ERROR: {err}")
    if len(rep["errors"]) > 20:
        print(f"ERROR: ... {len(rep['errors']) - 20} more")
    print(f"verdict: {rep['verdict']}")


def main(kind, argv=None):
    ap = argparse.ArgumentParser(description=f"{kind} Annex B validator (T02)")
    ap.add_argument("file")
    ap.add_argument("width", type=int)
    ap.add_argument("height", type=int)
    ap.add_argument("frames", type=int)
    ap.add_argument("--source", help="raw source, frames*W*H*3/2 bytes (NV12 by default)")
    ap.add_argument("--source-format", choices=("nv12", "i420"), default="nv12",
                    help="layout of --source (default nv12, the VENC input)")
    ap.add_argument("--min-psnr", type=float, default=30.0, help="Y and overall threshold in dB (default 30)")
    ap.add_argument("--min-psnr-chroma", type=float, default=None, help="U/V threshold (default --min-psnr)")
    ap.add_argument("--json", help="write the full report as JSON here")
    ap.add_argument("--quiet", action="store_true", help="no per-frame lines")
    ap.add_argument("--allow-any-pix-fmt", action="store_true", help="do not require 4:2:0")
    a = ap.parse_args(argv)
    if a.width <= 0 or a.height <= 0 or a.frames <= 0:
        print("width, height and frames must be positive", file=sys.stderr)
        return 2
    try:
        rep = check(kind, a.file, a.width, a.height, a.frames, a.source, a.min_psnr,
                    a.min_psnr_chroma, a.allow_any_pix_fmt, a.source_format)
    except ToolError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    except CheckError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    print_report(rep, a.quiet)
    if a.json:
        Path(a.json).write_text(json.dumps(rep, indent=1) + "\n")
    return 0 if rep["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main("h264"))
