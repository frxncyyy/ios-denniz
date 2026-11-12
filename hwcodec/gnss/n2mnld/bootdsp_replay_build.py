#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# bootdsp_replay_build.py -- turn an mnl_emu2.py run (TAG.tx.json, made with --dsp-script fake_dsp_rom.py) into
# runs/bootdsp_replay.json: the ordered host->DSP frames of libmnl's _BootDSP_UART_V2 (L1, MT6893) and the DSP->host
# reply each step waits for. Code fragments also carry {libmnl_off,len} of their raw payload inside libmnl.so.
# usage: python3 -I bootdsp_replay_build.py LIBMNL.so runs/TAG.tx.json runs/bootdsp_replay.json
import json, struct, sys
lib = open(sys.argv[1], 'rb').read(); run = json.load(open(sys.argv[2]))
BLOB_OFF, BLOB_LEN, FRAG = 0x1558a2, 69816, 1400       # "MT6893_E1 L1 DSP": libmnl vaddr == file offset (1st LOAD)
def unstuff(b):
    o = bytearray(); i = 0
    while i < len(b):
        if b[i] == 0xde and i + 1 < len(b): o.append(0xaa if b[i + 1] == 0xdf else 0xde); i += 2
        else: o.append(b[i]); i += 1
    return bytes(o)
def stuff(b):
    o = bytearray()
    for c in b: o += b'\xde\xdf' if c == 0xaa else b'\xde\xe0' if c == 0xde else bytes([c])
    return bytes(o)
def frame(fid, pl):
    body = struct.pack('<HH', len(pl) + 4, fid) + pl
    return b'\xaa\xf0' + stuff(body + struct.pack('<H', sum(body) & 0xffff)) + b'\xaa\x0f'
def parse(raw):
    body = unstuff(raw[2:-2]); ln, fid = struct.unpack_from('<HH', body); pl = body[4:4 + (ln & 0xfff) - 4]
    ck, = struct.unpack_from('<H', body, len(body) - 2)
    return fid, pl, ck == (sum(body[:-2]) & 0xffff)
out = []; seq = 0; last = None; acked = set(); hdr_count = 0
def add(**k):
    global seq
    seq += 1; k = dict(seq=seq, **k); out.append(k)
for t in run['tx']:
    raw = bytes.fromhex(t['hex']); fid, pl, ok = parse(raw)
    assert ok and raw == frame(fid, pl), t['seq']
    e = {'dir': 'tx', 'hex': raw.hex(), 'len': len(raw), 'pad4': -len(raw) % 4, 'vt_ms': round(t['vt_ns'] / 1e6, 3),
         'id': '0x%04x' % fid, 'libmnl_write_no': t['seq']}
    if fid == 0xfe08:
        hdr_count += 1
        e['note'] = ('BootDSP stage-1 header (code info), payload 106 B; libmnl sends it, then (retry 0 has no wait) 3x COMD4, '
                     'sleeps 500 ms, sends it again and polls up to 200*sqrt(retry) ms for the FE31 ack' if hdr_count == 1 else
                     'header re-sent after COMD4x3 + 500 ms sleep; the FE31 ack (seq 0) may answer either copy')
        add(**e)
        if hdr_count == 2:
            add(dir='rx-expect', hex=frame(0xfe31, struct.pack('<HH', 0, 0)).hex(), id='0xfe31',
                note='DSP ROM ack of the header: payload <u16 seq=0><u16 dsp_ver> (dsp_ver goes to libmnl 0x5881f0, '
                     'only checked for MT3332). Host matches seq against its counter (0) and moves to stage 2.')
    elif fid == 0xfe05 and pl == b'\x04\x00':
        e['note'] = 'COMD4 = Host_Based_Send_Data(5,1,1,4): id 0xFE05 payload 04 00, sent 3x per stage-1 retry'; add(**e)
    elif fid == 0xfe0c:
        fs, = struct.unpack_from('<H', pl); data = pl[2:]
        off = BLOB_OFF + 8 + (fs - 1) * FRAG                 # blob = 8-byte info header + code words
        assert lib[off:off + len(data)] == data, ('fragment payload is not the raw blob', fs)
        e['frag_seq'] = fs; e['src'] = {'libmnl_off': off, 'len': len(data)}
        e['dup'] = fs == last
        e['note'] = ('code fragment %d/50: payload <u16 seq=%d> + %d raw blob bytes (libmnl 0x%x), stuffed on the wire'
                     % (fs, fs, len(data), off)) + (' -- immediate duplicate (retry 0 polls 0 ms, so libmnl always sends each '
                     'fragment twice back-to-back)' if e['dup'] else '')
        add(**e)
        if e['dup'] and fs not in acked:
            acked.add(fs)
            add(dir='rx-expect', hex=frame(0xfe32, struct.pack('<H', fs)).hex(), id='0xfe32', frag_seq=fs,
                note='DSP ack of fragment %d: payload <u16 seq=%d>. A stale ack (seq != host seq) costs one of 4 retries; '
                     'the ROM must ack each seq once (acking the duplicate too made libmnl give up at frag 5 in emulation)'
                     % (fs, fs))
        last = fs
    else:
        e['note'] = ('first post-boot command (API_Set_AIC_CMD -> vMCU_vrd_Command_N): id 0xFE05 payload 32 00 00 01 00; '
                     'this is the 15-byte frame the old harness/kernel sent first because the boot was skipped')
        add(**e)
        if fid == 0xfe05: break
json.dump({'what': 'libmnl.so (sha256 9a1e1be7...) _BootDSP_UART_V2 L1 exchange for MT6893 (cfg1 x=0x250 y=0xffff6893), '
                   'recorded under mnl_emu2.py + fake_dsp_rom.py',
           'frame_format': 'AA F0 | u16 len (low 12 bits = payload+4) | u16 id (0xFExx) | payload | u16 sum16(len,id,payload) | AA 0F; '
                           'len/id/payload/cksum byte-stuffed AA->DE DF, DE->DE E0 (both directions)',
           'blob': {'name': 'MT6893_E1 L1 DSP', 'libmnl_off': BLOB_OFF, 'len': BLOB_LEN, 'code_size_words': 34904,
                    'start_addr': 0x7400, 'exec_addr': 0x7404, 'cipher_key': 123, 'blob_header': '8 bytes: u16 code_size_words, start_addr, exec_addr, cipher_key', 'code_off': BLOB_OFF + 8, 'frags': 50, 'frag_bytes': FRAG,
                    'last_frag_bytes': 1208},
           'steps': out}, open(sys.argv[3], 'w'), indent=1)
print(len(out), 'steps')
