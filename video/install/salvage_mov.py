#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""LEAD. Rescues the H.264 pictures of a Camera .MOV that was never finished (no moov atom: mediaserverd died, or the phone
was restarted, while recording). The mdat holds our encoder's samples as 4-byte lengths + NAL units between audio and metadata
chunks; they are found by their shape and written as an Annex-B stream, which ffmpeg turns into a playable file (no sound).
usage: salvage_mov.py BROKEN.MOV GOOD.MOV OUT.mp4 [fps=30]     GOOD.MOV: any finished recording of the same size, for SPS/PPS."""
import struct, subprocess, sys, tempfile, os
broken, good, out = sys.argv[1:4]; fps = sys.argv[4] if len(sys.argv) > 4 else '30'
t = tempfile.mkdtemp()
subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-i', good, '-map', '0:v:0', '-c', 'copy', '-bsf:v', 'h264_mp4toannexb',
                '-frames:v', '1', t + '/first.264'], check=True)
h = open(t + '/first.264', 'rb').read()
hdr = b''.join(b'\x00\x00\x00\x01' + p for p in h.split(b'\x00\x00\x00\x01')[1:] if p and (p[0] & 31) in (7, 8))
d = open(broken, 'rb').read(); n = len(d); o = 36; es = bytearray(hdr); nal = pics = idr = 0
while o + 5 <= n:
    L = struct.unpack('>I', d[o:o+4])[0]; b = d[o+4]
    if b in (0x65, 0x41) and 2 <= L < 2000000 and o + 4 + L <= n:
        if d[o+5] & 0x80:                       # first_mb_in_slice == 0: a new picture
            pics += 1; idr += b == 0x65
        es += b'\x00\x00\x00\x01' + d[o+4:o+4+L]; nal += 1; o += 4 + L
    else:
        o += 1
open(t + '/es.264', 'wb').write(es)
print('NAL units', nal, 'pictures', pics, 'key pictures', idr)
subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-r', fps, '-i', t + '/es.264', '-c', 'copy', out], check=True)
os.remove(t + '/first.264'); os.remove(t + '/es.264'); os.rmdir(t)
print('wrote', out, os.path.getsize(out), 'bytes')
