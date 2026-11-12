# SPDX-License-Identifier: MIT
# fake_dsp_rom.py -- DSP-ROM reply script for mnl_emu2.py --dsp-script (exec'd; defines on_tx).
# Host->DSP and DSP->host frames share one format (libmnl hal_MTK_Binary_EncodePacket / MTK_Binary_Ctl):
#   AA F0 | len u16 LE (low 12 bits = payload_len + 4) | id u16 LE (high byte 0xFE) | payload | cksum u16 LE | AA 0F
#   cksum = 16-bit sum of the unstuffed bytes len[0], len[1], id[0], id[1], payload[*].
#   Byte stuffing (MTK_Binary_DataStuffing) of len/id/payload/cksum: AA -> DE DF, DE -> DE E0.
# Replies modelled (libmnl Host_Based_Data_Dec, verified by disassembly + this emulation):
#   id 0xFE31 payload <u16 seq><u16 ver>: clears BootDSP flag bit15, 0x587f38 = seq, 0x5881f0 = ver  (ack of header FE08)
#   id 0xFE32 payload <u16 seq>          : clears flag bit5, 0x587f38 = seq                       (ack of code frag FE0C)
import struct as _st
def unstuff(b):
    out = bytearray(); i = 0
    while i < len(b):
        if b[i] == 0xde and i + 1 < len(b): out.append(0xaa if b[i + 1] == 0xdf else 0xde); i += 2
        else: out.append(b[i]); i += 1
    return bytes(out)
def stuff(b):
    out = bytearray()
    for c in b: out += b'\xde\xdf' if c == 0xaa else b'\xde\xe0' if c == 0xde else bytes([c])
    return bytes(out)
def frame(fid, payload):
    body = _st.pack('<HH', len(payload) + 4, fid) + payload
    return b'\xaa\xf0' + stuff(body + _st.pack('<H', sum(body) & 0xffff)) + b'\xaa\x0f'
def parse(raw):
    """split a TX chunk into (id, payload, cksum_ok) tuples"""
    res = []; i = 0
    while True:
        s = raw.find(b'\xaa\xf0', i)
        if s < 0: break
        e = raw.find(b'\xaa\x0f', s + 2)
        if e < 0: break
        body = unstuff(raw[s + 2:e]); i = e + 2
        ln, fid = _st.unpack_from('<HH', body); pl = body[4:4 + (ln & 0xfff) - 4]; ck, = _st.unpack_from('<H', body, len(body) - 2)
        res.append((fid, pl, ck == (sum(body[:-2]) & 0xffff)))
    return res
import os as _os
DSP_VER = int(_os.environ.get('FAKE_DSP_VER', '0'), 0)
PAD4 = _os.environ.get('FAKE_DSP_PAD4', '0') == '1'      # 1: zero-pad every reply to a 4-byte multiple (USRT 4-byte mode)
DUP_ACK = _os.environ.get('FAKE_DSP_DUP_ACK', '0') == '1'   # 1: ack every copy (cascades into libmnl retry exhaustion)
_state = {'next': 0}                                       # next sequence number the ROM expects (0 = header)
def on_tx(seq, data, vt, rx, log):
    for fid, pl, ok in parse(data):
        r = None
        if fid == 0xfe08:
            if DUP_ACK or _state['next'] == 0: r = frame(0xfe31, _st.pack('<HH', 0, DSP_VER)); _state['next'] = 1
        elif fid == 0xfe0c:
            s, = _st.unpack_from('<H', pl)
            if DUP_ACK or s == _state['next']: r = frame(0xfe32, pl[:2]); _state['next'] = s + 1
        if r is None:
            log('FAKE-DSP tx id %#x len %d ck %s: no reply (next=%d)' % (fid, len(pl), ok, _state['next'])); continue
        if PAD4: r += b'\0' * (-len(r) % 4)
        rx += r; log('FAKE-DSP tx id %#x len %d ck %s -> rx %s' % (fid, len(pl), ok, r.hex()))
