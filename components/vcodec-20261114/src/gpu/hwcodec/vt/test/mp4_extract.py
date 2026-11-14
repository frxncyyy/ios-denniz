#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mp4_extract.py -- the independent oracle for gpu/hwcodec/vt/annexb.c.

Reads an MP4 written by ffmpeg (moov/trak/mdia/minf/stbl: stsd avcC/hvcC, stsz, stsc, stco/co64, stss) and writes one
binary vector file the C test reads.  Nothing here comes from annexb.c: the boxes are parsed from ISO/IEC 14496-12,
the extradata is copied verbatim out of the sample description, and the per-frame Annex B / length-prefixed pair is
rebuilt in Python from that same data.

Vector layout (all integers little-endian unless stated):
    'N2HV'                     4 bytes
    u32 version (= 1)
    u32 codec (0 AVC, 1 HEVC)
    u32 width, u32 height
    u32 extradata_bytes, bytes
    u32 param_count, then param_count * { u8 type, u8 pad[3], u32 bytes, bytes }
    u32 frame_count, then frame_count * { u32 key, u32 annexb_bytes, bytes, u32 avcc_bytes, bytes }

The per-frame Annex B blob has a four-byte start code before the first NAL unit and three-byte start codes between NAL
units; the length-prefixed blob has four-byte big-endian lengths.  Trailing zero bytes in front of a start code are
removed from each NAL unit (they are not part of it, ITU-T H.264 7.4.1 / H.265 7.3.1), so the AVCC blob is the
specification's answer rather than ffmpeg's muxer quirk; the script checks that the raw mdat sample differs from it by
nothing but those zeros and exits non-zero otherwise.
"""

import struct
import sys


def boxes(data, start, end):
    off = start
    while off + 8 <= end:
        size, kind = struct.unpack(">I4s", data[off:off + 8])
        header = 8
        if size == 1:
            size = struct.unpack(">Q", data[off + 8:off + 16])[0]
            header = 16
        elif size == 0:
            size = end - off
        if size < header or off + size > end:
            raise ValueError("bad box %r at %d (size %d)" % (kind, off, size))
        yield kind, off + header, off + size
        off += size


def child(data, start, end, path):
    for kind, s, e in boxes(data, start, end):
        if kind == path[0]:
            if len(path) == 1:
                return s, e
            return child(data, s, e, path[1:])
    return None


def find_all(data, start, end, kind):
    return [(s, e) for k, s, e in boxes(data, start, end) if k == kind]


def parse_sps_type(codec, nal):
    if codec == "avc":
        return nal[0] & 0x1F
    return (nal[0] >> 1) & 0x3F


def trim_zeros(nal):
    return nal.rstrip(b"\x00")


def parse_avcc(extradata):
    if len(extradata) < 7 or extradata[0] != 1:
        raise ValueError("not an avcC record")
    off = 5
    n_sps = extradata[off] & 0x1F
    off += 1
    params = []
    for _ in range(n_sps):
        n = struct.unpack(">H", extradata[off:off + 2])[0]
        off += 2
        params.append((7, extradata[off:off + n]))
        off += n
    n_pps = extradata[off]
    off += 1
    for _ in range(n_pps):
        n = struct.unpack(">H", extradata[off:off + 2])[0]
        off += 2
        params.append((8, extradata[off:off + n]))
        off += n
    return params


def parse_hvcc(extradata):
    if len(extradata) < 23 or extradata[0] != 1:
        raise ValueError("not an hvcC record")
    off = 22
    n_arrays = extradata[off]
    off += 1
    params = []
    for _ in range(n_arrays):
        header = extradata[off]
        off += 1
        nal_type = header & 0x3F
        n = struct.unpack(">H", extradata[off:off + 2])[0]
        off += 2
        for _ in range(n):
            length = struct.unpack(">H", extradata[off:off + 2])[0]
            off += 2
            params.append((nal_type, extradata[off:off + length]))
            off += length
    return params


def split_avcc(sample, nal_header):
    out = []
    off = 0
    while off < len(sample):
        if off + 4 > len(sample):
            raise ValueError("truncated length")
        n = struct.unpack(">I", sample[off:off + 4])[0]
        off += 4
        if n == 0 or off + n > len(sample):
            raise ValueError("bad NAL length %d" % n)
        nal = sample[off:off + n]
        off += n
        if len(nal) < nal_header:
            raise ValueError("short NAL")
        out.append(nal)
    return out


def annexb_from_nals(nals):
    out = bytearray()
    for i, nal in enumerate(nals):
        out += b"\x00\x00\x00\x01" if i == 0 else b"\x00\x00\x01"
        out += nal
    return bytes(out)


def avcc_from_nals(nals):
    out = bytearray()
    for nal in nals:
        out += struct.pack(">I", len(nal))
        out += nal
    return bytes(out)


def main(argv):
    if len(argv) != 4:
        sys.stderr.write("usage: mp4_extract.py <in.mp4> <codec avc|hevc> <out.bin>\n")
        return 2
    path, codec, out_path = argv[1], argv[2].lower(), argv[3]
    if codec not in ("avc", "hevc"):
        sys.stderr.write("codec must be avc or hevc\n")
        return 2
    data = open(path, "rb").read()

    moov = child(data, 0, len(data), [b"moov"])
    if moov is None:
        raise SystemExit("no moov")
    trak = child(data, moov[0], moov[1], [b"trak"])
    if trak is None:
        raise SystemExit("no trak")
    stbl = child(data, trak[0], trak[1], [b"mdia", b"minf", b"stbl"])
    if stbl is None:
        raise SystemExit("no stbl")
    stsd = child(data, stbl[0], stbl[1], [b"stsd"])
    if stsd is None:
        raise SystemExit("no stsd")
    entries = list(boxes(data, stsd[0] + 8, stsd[1]))   # version/flags + entry_count before the entries
    if not entries:
        raise SystemExit("empty stsd")
    entry_kind, es, ee = entries[0]
    extradata = None
    width = height = None
    if entry_kind == b"avc1":
        # VisualSampleEntry: width/height at offset 8+24 / 8+26 relative to the entry payload start
        width, height = struct.unpack(">HH", data[es + 24:es + 28])
        want = b"avcC"
    elif entry_kind in (b"hvc1", b"hev1"):
        width, height = struct.unpack(">HH", data[es + 24:es + 28])
        want = b"hvcC"
    else:
        raise SystemExit("unexpected sample entry %r" % entry_kind)
    for kind, s, e in boxes(data, es + 8 + 70, ee):
        if kind == want:
            extradata = data[s:e]
    if extradata is None:
        raise SystemExit("no %r in the sample entry" % want)
    params = parse_avcc(extradata) if codec == "avc" else parse_hvcc(extradata)
    if codec == "avc":
        types = [t for t, _ in params]
        if 7 not in types or 8 not in types:
            raise SystemExit("avcC without SPS/PPS")
    else:
        types = [t for t, _ in params]
        if 33 not in types:
            raise SystemExit("hvcC without SPS")

    stsz = child(data, stbl[0], stbl[1], [b"stsz"])
    stsc = child(data, stbl[0], stbl[1], [b"stsc"])
    stco = child(data, stbl[0], stbl[1], [b"stco"])
    co64 = child(data, stbl[0], stbl[1], [b"co64"])
    stss = child(data, stbl[0], stbl[1], [b"stss"])
    if stsz is None or stsc is None or (stco is None and co64 is None):
        raise SystemExit("missing stsz/stsc/stco")
    s = stsz[0]
    sample_size, count = struct.unpack(">II", data[s + 4:s + 12])
    sizes = [sample_size] * count if sample_size else [
        struct.unpack(">I", data[s + 12 + 4 * i:s + 16 + 4 * i])[0] for i in range(count)]

    s = stsc[0]
    n = struct.unpack(">I", data[s + 4:s + 8])[0]
    stsc_entries = []
    for i in range(n):
        first_chunk, per_chunk, desc = struct.unpack(">III", data[s + 8 + 12 * i:s + 20 + 12 * i])
        stsc_entries.append((first_chunk, per_chunk))

    if stco is not None:
        s = stco[0]
        n = struct.unpack(">I", data[s + 4:s + 8])[0]
        chunks = [struct.unpack(">I", data[s + 8 + 4 * i:s + 12 + 4 * i])[0] for i in range(n)]
    else:
        s = co64[0]
        n = struct.unpack(">I", data[s + 4:s + 8])[0]
        chunks = [struct.unpack(">Q", data[s + 8 + 8 * i:s + 16 + 8 * i])[0] for i in range(n)]

    sync = set(range(count))
    if stss is not None:
        s = stss[0]
        n = struct.unpack(">I", data[s + 4:s + 8])[0]
        sync = {struct.unpack(">I", data[s + 8 + 4 * i:s + 12 + 4 * i])[0] - 1 for i in range(n)}

    # Walk the chunks and cut the samples out of the mdat region.
    offsets = []
    sample = 0
    for chunk_index, chunk_offset in enumerate(chunks, start=1):
        per_chunk = None
        for first_chunk, n_samples in stsc_entries:
            if first_chunk <= chunk_index:
                per_chunk = n_samples
            else:
                break
        if per_chunk is None:
            raise SystemExit("stsc does not cover chunk %d" % chunk_index)
        off = chunk_offset
        for _ in range(per_chunk):
            if sample >= count:
                break
            offsets.append(off)
            off += sizes[sample]
            sample += 1
    if sample != count:
        raise SystemExit("chunk table covers %d of %d samples" % (sample, count))

    frames = []
    for i in range(count):
        sample = data[offsets[i]:offsets[i] + sizes[i]]
        nals = split_avcc(sample, 1 if codec == "avc" else 2)
        trimmed = [trim_zeros(n) for n in nals]
        for a, b in zip(nals, trimmed):
            if a.rstrip(b"\x00") != b.rstrip(b"\x00") or a[:len(b)] != b:
                raise SystemExit("frame %d: trimming split a NAL unit" % i)
            if a[len(b):] != b"\x00" * (len(a) - len(b)):
                raise SystemExit("frame %d: mdat sample differs by more than trailing zeros" % i)
        avcc = avcc_from_nals(trimmed)
        # The mdat sample must be exactly the same lengths and payloads apart from the trailing zeros.
        rebuilt = bytearray()
        for a, b in zip(nals, trimmed):
            rebuilt += struct.pack(">I", len(a)) + a
        if bytes(rebuilt) != sample:
            raise SystemExit("frame %d: re-assembled sample differs from mdat" % i)
        frames.append((1 if i in sync else 0, annexb_from_nals(trimmed), avcc))

    out = bytearray(b"N2HV")
    out += struct.pack("<IIIII", 1, 0 if codec == "avc" else 1, width, height, len(extradata))
    out += extradata
    out += struct.pack("<I", len(params))
    for nal_type, nal in params:
        out += struct.pack("<B3xI", nal_type, len(nal)) + nal
    out += struct.pack("<I", len(frames))
    for key, annexb, avcc in frames:
        out += struct.pack("<II", key, len(annexb)) + annexb
        out += struct.pack("<I", len(avcc)) + avcc
    open(out_path, "wb").write(out)
    print("mp4_extract: %s %s %dx%d extradata=%d params=%s frames=%d keys=%d bytes=%d" % (
        path, codec.upper(), width, height, len(extradata), types, len(frames),
        sum(1 for f in frames if f[0]), len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
