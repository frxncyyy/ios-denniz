#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Turn the golden streams of manifest.json into gpu/video/vectors/embedded_vectors.h.

The header is for the on-phone probe (WP07): the H.264 and HEVC streams are
split into parameter sets (given separately, without start codes) and access
units in AVCC form (a 4-byte big-endian length before each NAL, parameter-set
and AUD NALs removed), so that the probe can call
CMVideoFormatDescriptionCreateFrom{H264,HEVC}ParameterSets and wrap each byte
range in a CMSampleBuffer without parsing Annex B on the phone.  The JPEG and
HEIC files go in whole.

Every conversion is verified here, from the standard's rules only:
  * the Annex B scanner accepts 3- and 4-byte start codes;
  * parameter sets and AUD are removed, SEI stays with its access unit;
  * converting every AVCC access unit back to NAL units and concatenating
    must reproduce the original NAL sequence minus parameter sets/AUD;
  * the number of access units equals the manifest's frame count, and only
    the first one is a keyframe for these streams;
  * the total embedded data stays under 48 KiB.

Run with --verify to regenerate in memory and compare with the existing file
(that is what check_vectors.sh does).
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUDGET = 48 * 1024
MAX_LINE = 12


class ParseError(Exception):
    pass


def split_annexb(data):
    """Annex B byte stream -> list of NAL units (start codes removed)."""
    nals = []
    i = 0
    n = len(data)
    while i < n - 2:
        if data[i] == 0 and data[i + 1] == 0:
            if data[i + 2] == 1:
                start = i + 3
                i = start
            elif i + 3 < n and data[i + 2] == 0 and data[i + 3] == 1:
                start = i + 4
                i = start
            else:
                i += 1
                continue
            j = start
            while j < n - 2:
                if (data[j] == 0 and data[j + 1] == 0 and
                        (data[j + 2] == 1 or
                         (j + 3 < n and data[j + 2] == 0 and data[j + 3] == 1))):
                    break
                j += 1
            if j <= start:
                raise ParseError("empty NAL unit at offset %d" % start)
            nals.append(data[start:j])
            i = j
        else:
            i += 1
    if not nals:
        raise ParseError("no Annex B NAL units found")
    return nals


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def bit(self):
        if self.pos >= len(self.data) * 8:
            raise ParseError("bit reader past the end")
        byte = self.data[self.pos >> 3]
        value = (byte >> (7 - (self.pos & 7))) & 1
        self.pos += 1
        return value

    def ue(self):
        zeros = 0
        while self.bit() == 0:
            zeros += 1
            if zeros > 31:
                raise ParseError("Exp-Golomb code too long")
        value = 0
        for _ in range(zeros):
            value = (value << 1) | self.bit()
        return (1 << zeros) - 1 + value


def unescape_rbsp(nal):
    out = bytearray()
    zeros = 0
    for b in nal:
        if zeros >= 2 and b == 3:
            zeros = 0
            continue
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def h264_header(nal):
    if len(nal) < 1:
        raise ParseError("short H.264 NAL")
    return nal[0] & 0x1F


def h264_first_mb(nal):
    rbsp = unescape_rbsp(nal[1:])
    return BitReader(rbsp).ue()


def hevc_header(nal):
    if len(nal) < 3:
        raise ParseError("short HEVC NAL")
    return (nal[0] >> 1) & 0x3F


def hevc_first_slice(nal):
    if len(nal) < 3:
        raise ParseError("short HEVC NAL")
    return (nal[2] >> 7) & 1


def build_access_units(nals, codec):
    params = []
    aus = []
    cur = []
    cur_has_vcl = False
    removed = 0
    for nal in nals:
        if codec == "h264":
            typ = h264_header(nal)
            is_vcl = 1 <= typ <= 5
            is_param = typ in (7, 8)
            is_drop = typ in (9, 10, 11, 12)   # AUD, end of sequence/stream, filler
            first_slice = is_vcl and h264_first_mb(nal) == 0
            is_key = typ == 5
        else:
            typ = hevc_header(nal)
            is_vcl = 0 <= typ <= 31
            is_param = typ in (32, 33, 34)
            is_drop = typ in (35, 36, 37, 38)   # AUD, EOS, EOB, filler
            first_slice = is_vcl and hevc_first_slice(nal) == 1
            is_key = 16 <= typ <= 23
        if is_param:
            params.append(nal)
            removed += 1
            continue
        if is_drop:
            removed += 1
            continue
        if is_vcl:
            if first_slice and cur_has_vcl:
                aus.append(cur)
                cur = []
                cur_has_vcl = False
            cur.append(nal)
            cur_has_vcl = True
        else:
            cur.append(nal)
    if cur:
        aus.append(cur)
    if not aus:
        raise ParseError("no access units")
    for au in aus:
        if not any((h264_header(n) in range(1, 6)) if codec == "h264"
                   else (hevc_header(n) <= 31) for n in au):
            raise ParseError("access unit without a slice")
    kept = [nal for au in aus for nal in au]
    if removed + len(kept) != len(nals):
        raise ParseError("NAL accounting mismatch")
    return params, aus


def au_keyframe(au, codec):
    for nal in au:
        if codec == "h264":
            if h264_header(nal) == 5:
                return 1
        else:
            typ = hevc_header(nal)
            if 16 <= typ <= 23:
                return 1
    return 0


def avcc(au):
    out = bytearray()
    for nal in au:
        if len(nal) > 0xFFFFFFFF:
            raise ParseError("NAL too large")
        out += len(nal).to_bytes(4, "big")
        out += nal
    return bytes(out)


def avcc_back(data):
    nals = []
    pos = 0
    while pos < len(data):
        if pos + 4 > len(data):
            raise ParseError("truncated AVCC length")
        n = int.from_bytes(data[pos:pos + 4], "big")
        pos += 4
        if n == 0 or pos + n > len(data):
            raise ParseError("bad AVCC length")
        nals.append(data[pos:pos + n])
        pos += n
    return nals


def c_array(name, data):
    lines = ["static const uint8_t %s[] = {" % name]
    for off in range(0, len(data), MAX_LINE):
        chunk = data[off:off + MAX_LINE]
        lines.append("    " + " ".join("0x%02x," % b for b in chunk))
    lines.append("};")
    lines.append("static const unsigned %s_size = sizeof(%s);" % (name, name))
    return "\n".join(lines)


def load_stream(manifest, name):
    for s in manifest["streams"]:
        if s["name"] == name:
            return s
    raise SystemExit("make_embedded: no stream %r in the manifest" % name)


def read_rel(data_dir, rel):
    path = os.path.join(data_dir, rel)
    with open(path, "rb") as fp:
        return fp.read()


def build_header(data_dir, manifest):
    if manifest.get("version") != 1:
        raise SystemExit("make_embedded: unsupported manifest version")

    h264 = load_stream(manifest, "h264-baseline")
    hevc = load_stream(manifest, "hevc")
    jpeg = load_stream(manifest, "jpeg-q85")
    heic = load_stream(manifest, "heic")

    h264_data = read_rel(data_dir, h264["path"])
    hevc_data = read_rel(data_dir, hevc["path"])
    jpeg_data = read_rel(data_dir, jpeg["path"])
    heic_data = read_rel(data_dir, heic["path"])

    h264_nals = split_annexb(h264_data)
    hevc_nals = split_annexb(hevc_data)

    h264_params, h264_aus = build_access_units(h264_nals, "h264")
    hevc_params, hevc_aus = build_access_units(hevc_nals, "hevc")

    if len(h264_params) != 2:
        raise SystemExit("make_embedded: expected SPS+PPS, got %d H.264 parameter sets"
                         % len(h264_params))
    if len(hevc_params) != 3:
        raise SystemExit("make_embedded: expected VPS+SPS+PPS, got %d HEVC parameter sets"
                         % len(hevc_params))
    if len(h264_aus) != h264["frames"] or len(hevc_aus) != hevc["frames"]:
        raise SystemExit("make_embedded: access unit count %d/%d does not match %d/%d"
                         % (len(h264_aus), len(hevc_aus), h264["frames"], hevc["frames"]))

    h264_avcc = [avcc(au) for au in h264_aus]
    hevc_avcc = [avcc(au) for au in hevc_aus]

    # Round trip: AVCC access units -> NAL units -> the original minus params/AUD.
    h264_kept = [nal for au in h264_aus for nal in au]
    h264_rt = [nal for au in h264_avcc for nal in avcc_back(au)]
    if h264_rt != h264_kept:
        raise SystemExit("make_embedded: H.264 AVCC round trip differs")
    hevc_kept = [nal for au in hevc_aus for nal in au]
    hevc_rt = [nal for au in hevc_avcc for nal in avcc_back(au)]
    if hevc_rt != hevc_kept:
        raise SystemExit("make_embedded: HEVC AVCC round trip differs")

    for codec, aus, stream in (("h264", h264_aus, h264), ("hevc", hevc_aus, hevc)):
        keys = [au_keyframe(au, codec) for au in aus]
        if keys[0] != 1:
            raise SystemExit("make_embedded: %s frame 0 is not a keyframe" % codec)
        if any(keys[1:]) and stream["frames"] > 1:
            raise SystemExit("make_embedded: %s has an unexpected later keyframe" % codec)

    total = (sum(len(x) for x in h264_params) + sum(len(x) for x in h264_avcc) +
             sum(len(x) for x in hevc_params) + sum(len(x) for x in hevc_avcc) +
             len(jpeg_data) + len(heic_data))
    if total > BUDGET:
        raise SystemExit("make_embedded: %d bytes of vector data exceed the %d byte budget"
                         % (total, BUDGET))

    out = []
    out.append("/* SPDX-License-Identifier: MIT */")
    out.append("/*")
    out.append(" * embedded_vectors.h -- tiny golden streams for the on-phone probe.")
    out.append(" *")
    out.append(" * GENERATED by gpu/video/vectors/make_embedded.py from the golden streams")
    out.append(" * described in gpu/video/vectors/manifest.json.  Do not edit by hand.")
    out.append(" *")
    out.append(" * H.264/HEVC: parameter sets are separate (no start code, no length);")
    out.append(" * each access unit is AVCC: every kept NAL is prefixed with its 4-byte")
    out.append(" * big-endian length.  Parameter-set, AUD and filler NAL units were removed;")
    out.append(" * SEI NAL units stay with their access unit.  The H.264 stream is")
    out.append(" * Constrained Baseline, the HEVC stream is Main, both 176x144.")
    out.append(" *")
    out.append(" * JPEG and HEIC are the whole files.  All objects are static const; the")
    out.append(" * header has an include guard, so including it twice is harmless.")
    out.append(" *")
    out.append(" * Sources: h264-baseline sha256 %s" % h264["sha256"])
    out.append(" *          hevc          sha256 %s" % hevc["sha256"])
    out.append(" *          jpeg-q85      sha256 %s" % jpeg["sha256"])
    out.append(" *          heic          sha256 %s" % heic["sha256"])
    out.append(" *")
    out.append(" */")
    out.append("#ifndef N2VEC_EMBEDDED_VECTORS_H")
    out.append("#define N2VEC_EMBEDDED_VECTORS_H")
    out.append("")
    out.append("#include <stddef.h>")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("#ifdef __cplusplus")
    out.append('extern "C" {')
    out.append("#endif")
    out.append("")
    out.append("struct n2vec_frame {")
    out.append("\tconst uint8_t *data;")
    out.append("\tunsigned size;")
    out.append("\tunsigned keyframe;")
    out.append("};")
    out.append("")
    out.append("/* One video stream: parameter sets separately, access units in AVCC. */")
    out.append("struct n2vec_stream {")
    out.append("\tconst char *name;")
    out.append("\tunsigned codec;              /* four-character code as an integer: 'avc1', 'hvc1' */")
    out.append("\tunsigned width, height;")
    out.append("\tunsigned nal_length_size;    /* 4 in every embedded access unit */")
    out.append("\tunsigned ps_count;")
    out.append("\tconst uint8_t *ps[4];        /* parameter sets, without start code or length */")
    out.append("\tunsigned ps_size[4];")
    out.append("\tunsigned frame_count;")
    out.append("\tconst struct n2vec_frame *frames;")
    out.append("\tconst char *luma_md5_first;  /* md5 of decoded frame 0's luma, width bytes/row */")
    out.append("};")
    out.append("")
    out.append("/* One still image file. */")
    out.append("struct n2vec_image {")
    out.append("\tconst char *name;")
    out.append("\tconst char *uti;")
    out.append("\tconst uint8_t *data;")
    out.append("\tunsigned size;")
    out.append("\tunsigned width, height;")
    out.append("};")
    out.append("")
    out.append("#define N2VEC_H264_WIDTH %du" % h264["width"])
    out.append("#define N2VEC_H264_HEIGHT %du" % h264["height"])
    out.append("#define N2VEC_HEVC_WIDTH %du" % hevc["width"])
    out.append("#define N2VEC_HEVC_HEIGHT %du" % hevc["height"])
    out.append("#define N2VEC_JPEG_WIDTH %du" % jpeg["width"])
    out.append("#define N2VEC_JPEG_HEIGHT %du" % jpeg["height"])
    out.append("#define N2VEC_HEIC_WIDTH %du" % heic["width"])
    out.append("#define N2VEC_HEIC_HEIGHT %du" % heic["height"])
    out.append("")
    out.append(c_array("n2vec_h264_sps", h264_params[0]))
    out.append("")
    out.append(c_array("n2vec_h264_pps", h264_params[1]))
    out.append("")
    out.append(c_array("n2vec_hevc_vps", hevc_params[0]))
    out.append("")
    out.append(c_array("n2vec_hevc_sps", hevc_params[1]))
    out.append("")
    out.append(c_array("n2vec_hevc_pps", hevc_params[2]))
    out.append("")

    def frames_table(codec, avcc_list, aus):
        lines = ["static const struct n2vec_frame n2vec_%s_frames[] = {" % codec]
        for i, data in enumerate(avcc_list):
            arr = "n2vec_%s_au%d" % (codec, i)
            out.append(c_array(arr, data))
            out.append("")
            lines.append("\t{ %s, sizeof(%s), %du }," % (arr, arr, au_keyframe(aus[i], codec)))
        lines.append("};")
        lines.append("static const unsigned n2vec_%s_frame_count = sizeof(n2vec_%s_frames) / "
                     "sizeof(n2vec_%s_frames[0]);" % (codec, codec, codec))
        return "\n".join(lines)

    out.append(frames_table("h264", h264_avcc, h264_aus))
    out.append("")
    out.append(frames_table("hevc", hevc_avcc, hevc_aus))
    out.append("")

    out.append(c_array("n2vec_jpeg", jpeg_data))
    out.append("")
    out.append(c_array("n2vec_heic", heic_data))
    out.append("")

    out.append('static const char n2vec_h264_luma_md5_first_frame[] = "%s";'
               % h264["luma_md5_first_frame"])
    out.append('static const char n2vec_hevc_luma_md5_first_frame[] = "%s";'
               % hevc["luma_md5_first_frame"])
    out.append('static const char n2vec_jpeg_luma_md5_first_frame[] = "%s";'
               % jpeg["luma_md5_first_frame"])
    out.append('static const char n2vec_heic_luma_md5_first_frame[] = "%s";'
               % heic["luma_md5_first_frame"])
    out.append("")
    out.append("static const struct n2vec_stream n2vec_streams[] = {")
    out.append('\t{ "h264", 0x61766331u, %du, %du, 4u, 2u,'
               % (h264["width"], h264["height"]))
    out.append("\t  { n2vec_h264_sps, n2vec_h264_pps, NULL, NULL },")
    out.append("\t  { n2vec_h264_sps_size, n2vec_h264_pps_size, 0u, 0u },")
    out.append("\t  %du, n2vec_h264_frames, n2vec_h264_luma_md5_first_frame },"
               % h264["frames"])
    out.append('\t{ "hevc", 0x68766331u, %du, %du, 4u, 3u,'
               % (hevc["width"], hevc["height"]))
    out.append("\t  { n2vec_hevc_vps, n2vec_hevc_sps, n2vec_hevc_pps, NULL },")
    out.append("\t  { n2vec_hevc_vps_size, n2vec_hevc_sps_size, n2vec_hevc_pps_size, 0u },")
    out.append("\t  %du, n2vec_hevc_frames, n2vec_hevc_luma_md5_first_frame }"
               % hevc["frames"])
    out.append("};")
    out.append("static const unsigned n2vec_stream_table_count =")
    out.append("\tsizeof(n2vec_streams) / sizeof(n2vec_streams[0]);")
    out.append("")
    out.append("static const struct n2vec_image n2vec_images[] = {")
    out.append('\t{ "jpeg", "public.jpeg", n2vec_jpeg, n2vec_jpeg_size, %du, %du },'
               % (jpeg["width"], jpeg["height"]))
    out.append('\t{ "heic", "public.heic", n2vec_heic, n2vec_heic_size, %du, %du }'
               % (heic["width"], heic["height"]))
    out.append("};")
    out.append("static const unsigned n2vec_image_table_count =")
    out.append("\tsizeof(n2vec_images) / sizeof(n2vec_images[0]);")
    out.append("")
    out.append("enum {")
    out.append("\tN2VEC_STREAM_H264 = 0,")
    out.append("\tN2VEC_STREAM_HEVC = 1,")
    out.append("\tN2VEC_STREAM_JPEG = 2,")
    out.append("\tN2VEC_STREAM_HEIC = 3,")
    out.append("\tN2VEC_STREAM_COUNT = 4")
    out.append("};")
    out.append("")
    out.append("static const char *const n2vec_names[N2VEC_STREAM_COUNT] = {")
    out.append('\t"h264", "hevc", "jpeg", "heic"')
    out.append("};")
    out.append("")
    out.append("/* Number of video streams (H.264, HEVC) and still images (JPEG, HEIC). */")
    out.append("static inline unsigned n2vec_stream_count(void)")
    out.append("{")
    out.append("\treturn n2vec_stream_table_count;")
    out.append("}")
    out.append("static inline unsigned n2vec_image_count(void)")
    out.append("{")
    out.append("\treturn n2vec_image_table_count;")
    out.append("}")
    out.append("/* Number of embedded vector groups: 2 streams + 2 images = 4. */")
    out.append("static inline unsigned n2vec_count(void) { return N2VEC_STREAM_COUNT; }")
    out.append("static inline const char *n2vec_name(unsigned i) {")
    out.append("\treturn i < N2VEC_STREAM_COUNT ? n2vec_names[i] : \"?\";")
    out.append("}")
    out.append("")
    out.append("#ifdef __cplusplus")
    out.append("}")
    out.append("#endif")
    out.append("#endif /* N2VEC_EMBEDDED_VECTORS_H */")
    out.append("")

    stats = {
        "h264": (len(h264_aus), len(h264_kept), sum(len(x) for x in h264_params)),
        "hevc": (len(hevc_aus), len(hevc_kept), sum(len(x) for x in hevc_params)),
        "jpeg": (1, 1, len(jpeg_data)),
        "heic": (1, 1, len(heic_data)),
        "total": total,
    }
    return "\n".join(out), stats


def main(argv=None):
    ap = argparse.ArgumentParser(description="make embedded_vectors.h")
    ap.add_argument("--data", default=os.path.join(HERE, "data"))
    ap.add_argument("--manifest", default=os.path.join(HERE, "manifest.json"))
    ap.add_argument("--out", default=os.path.join(HERE, "embedded_vectors.h"))
    ap.add_argument("--verify", action="store_true",
                    help="compare the regenerated header with --out instead of writing")
    args = ap.parse_args(argv)

    if not os.path.isfile(args.manifest):
        raise SystemExit("make_embedded: %s does not exist; run make_vectors.sh quick first"
                         % args.manifest)
    with open(args.manifest, "r", encoding="utf-8") as fp:
        manifest = json.load(fp)
    text, stats = build_header(args.data, manifest)

    if args.verify:
        if not os.path.isfile(args.out):
            print("N2VEC verify FAIL: %s missing" % args.out)
            return 1
        with open(args.out, "r", encoding="utf-8") as fp:
            old = fp.read()
        if old != text:
            print("N2VEC verify FAIL: %s differs from the regenerated header" % args.out)
            return 1
        print("N2VEC verify PASS: h264 %d AUs, hevc %d AUs, jpeg %d B, heic %d B, "
              "total %d B (budget %d)"
              % (stats["h264"][0], stats["hevc"][0], stats["jpeg"][2],
                 stats["heic"][2], stats["total"], BUDGET))
        return 0

    tmp = args.out + ".tmp.%d" % os.getpid()
    with open(tmp, "w", encoding="utf-8") as fp:
        fp.write(text)
    os.replace(tmp, args.out)
    print("N2VEC wrote %s: h264 %d AUs (%d NALs kept, %d param bytes), hevc %d AUs "
          "(%d NALs kept, %d param bytes), total %d B (budget %d)"
          % (args.out, stats["h264"][0], stats["h264"][1], stats["h264"][2],
             stats["hevc"][0], stats["hevc"][1], stats["hevc"][2],
             stats["total"], BUDGET))
    return 0


if __name__ == "__main__":
    sys.exit(main())
