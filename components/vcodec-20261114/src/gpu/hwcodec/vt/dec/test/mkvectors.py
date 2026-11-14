#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mkvectors.py -- build DEC06's parameter-set/sample vectors with ffmpeg as the independent oracle.

For each of four streams it runs ffmpeg/libx264/libx265, splits the Annex B output at access-unit delimiters (AUD), moves
the parameter sets into an avcC (ISO/IEC 14496-15 5.2.4.1.1) / hvcC (8.3.3.1.1) record built here (an independent
implementation, not the C one under test), writes length-prefixed samples, and prints what ffprobe says about the stream.

Vector file, little endian:
    u32 magic 'N2VD', u32 version 1, u32 codec fourcc, u32 nal length size,
    u32 record_bytes, record, u32 sample_count, { u32 bytes, sample }...

stdout: one JSON object per stream:
    name, path, codec, width, height, pix_fmt, color_range, full_range, profile, level,
    record_bytes, sps, pps, vps, samples

Usage: python3 mkvectors.py <outdir>
"""
import json
import os
import struct
import subprocess
import sys


def run(cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def ffprobe(path):
    out = subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height,pix_fmt,color_range,profile,level",
        "-of", "json", path,
    ])
    return json.loads(out)["streams"][0]


def split_nals(data):
    """Return [(nal_type, payload_bytes)] of an Annex B stream (start codes removed)."""
    starts = []
    i = 0
    while i + 3 <= len(data):
        if data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 1:
            starts.append(i + 3)
            i += 3
        elif i + 4 <= len(data) and data[i] == 0 and data[i + 1] == 0 and data[i + 2] == 0 and data[i + 3] == 1:
            starts.append(i + 4)
            i += 4
        else:
            i += 1
    nals = []
    for k, s in enumerate(starts):
        e = starts[k + 1] - 3 if k + 1 < len(starts) else len(data)
        # Trim the zero bytes that belong to the next start code.
        while e > s and data[e - 1] == 0:
            e -= 1
        if e <= s:
            continue
        nals.append((s, e))
    return [data[s:e] for s, e in nals]


def nal_type_avc(nal):
    return nal[0] & 0x1F


def nal_type_hevc(nal):
    return (nal[0] >> 1) & 0x3F


def build_avcc(sps, pps, length_size=4):
    rec = bytearray()
    rec += bytes([1, sps[0][1], sps[0][2], sps[0][3], 0xFC | (length_size - 1), 0xE0 | len(sps)])
    for s in sps:
        rec += struct.pack(">H", len(s)) + s
    rec += bytes([len(pps)])
    for p in pps:
        rec += struct.pack(">H", len(p)) + p
    return bytes(rec)


def build_hvcc(vps, sps, pps, length_size=4):
    s = sps[0]                      # two NAL header bytes, then the SPS RBSP
    rec = bytearray()
    rec += bytes([1, s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]])
    rec += struct.pack(">H", 0xF000)            # min_spatial_segmentation_idc = 0
    rec += bytes([0xFC, 0xFC | 1, 0xF8, 0xF8])  # parallelism, chromaFormat 4:2:0, bit depths 8
    rec += struct.pack(">H", 0)                 # avgFrameRate
    rec += bytes([0x0F])                        # numTemporalLayers 1, temporalIdNested 1, lengthSizeMinusOne 3
    arrays = [(32, vps), (33, sps), (34, pps)]
    arrays = [(t, n) for t, n in arrays if n]
    rec += bytes([len(arrays)])
    for t, nals in arrays:
        rec += bytes([0x80 | t])
        rec += struct.pack(">H", len(nals))
        for n in nals:
            rec += struct.pack(">H", len(n)) + n
    return bytes(rec)


def build(name, codec, width, height, frames, codec_args, outdir):
    raw = os.path.join(outdir, name + (".h264" if codec == "avc1" else ".h265"))
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
         "-i", "testsrc2=size=%dx%d:rate=30" % (width, height), "-frames:v", str(frames),
         "-c:v", "libx264" if codec == "avc1" else "libx265"] + codec_args + ["-f", "h264" if codec == "avc1" else "hevc", raw, "-y"])
    data = open(raw, "rb").read()
    nals = split_nals(data)
    params = {}
    samples = []
    current = []
    for nal in nals:
        t = nal_type_avc(nal) if codec == "avc1" else nal_type_hevc(nal)
        is_param = (t in (7, 8)) if codec == "avc1" else (t in (32, 33, 34))
        is_aud = (t == 9) if codec == "avc1" else (t == 35)
        if is_aud:
            if current:
                samples.append(current)
            current = []
            continue
        if is_param:
            params.setdefault(t, []).append(nal)
            continue
        current.append(nal)
    if current:
        samples.append(current)
    if codec == "avc1":
        record = build_avcc(params.get(7, []), params.get(8, []))
        nsps, npps, nvps = len(params.get(7, [])), len(params.get(8, [])), 0
    else:
        record = build_hvcc(params.get(32, []), params.get(33, []), params.get(34, []))
        nsps, npps, nvps = len(params.get(33, [])), len(params.get(34, [])), len(params.get(32, []))

    path = os.path.join(outdir, name + ".vec")
    with open(path, "wb") as f:
        f.write(b"N2VD")
        f.write(struct.pack("<III", 1, int.from_bytes(codec.encode(), "little"), 4))
        f.write(struct.pack("<I", len(record)) + record)
        f.write(struct.pack("<I", len(samples)))
        for s in samples:
            blob = b"".join(struct.pack(">I", len(n)) + n for n in s)
            f.write(struct.pack("<I", len(blob)) + blob)
    probe = ffprobe(raw)
    full = 1 if probe.get("color_range") in ("pc", "full") else 0
    return {
        "name": name, "path": path, "codec": codec,
        "width": probe["width"], "height": probe["height"],
        "pix_fmt": probe.get("pix_fmt"), "color_range": probe.get("color_range"), "full_range": full,
        "profile": probe.get("profile"), "level": probe.get("level"),
        "record_bytes": len(record), "sps": nsps, "pps": npps, "vps": nvps, "samples": len(samples),
    }


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/n2hw-DEC06/vectors"
    os.makedirs(outdir, exist_ok=True)
    result = []
    result.append(build("avc-high", "avc1", 176, 144, 5,
                        ["-profile:v", "high", "-level", "4.1",
                         "-x264-params", "keyint=10:min-keyint=10:scenecut=0:aud=1"], outdir))
    result.append(build("avc-full", "avc1", 320, 180, 5,
                        ["-profile:v", "high", "-level", "4.1", "-color_range", "pc",
                         "-x264-params", "keyint=10:min-keyint=10:scenecut=0:aud=1:fullrange=on"], outdir))
    result.append(build("hevc-main", "hvc1", 176, 144, 5,
                        ["-profile:v", "main", "-level", "4.1",
                         "-x265-params", "keyint=10:min-keyint=10:scenecut=0:aud=1:log-level=error"], outdir))
    result.append(build("hevc-full", "hvc1", 320, 180, 5,
                        ["-profile:v", "main", "-level", "4.1", "-color_range", "pc",
                         "-x265-params", "keyint=10:min-keyint=10:scenecut=0:aud=1:log-level=error:range=full"], outdir))
    # 10-bit: a stream the plug-in must refuse per frame (8-bit 4:2:0 output only) rather than mis-decode.
    result.append(build("hevc-main10", "hvc1", 176, 144, 3,
                        ["-profile:v", "main10", "-pix_fmt", "yuv420p10le",
                         "-x265-params", "keyint=10:min-keyint=10:scenecut=0:aud=1:log-level=error:bframes=0"], outdir))
    for r in result:
        print(json.dumps(r))


if __name__ == "__main__":
    main()
