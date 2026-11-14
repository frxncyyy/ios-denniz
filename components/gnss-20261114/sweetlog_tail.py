#!/usr/bin/env python3
"""gnss-20261114/sweetlog_tail.py: print the tail of the persistent GNSS trace (kernel 0583) and name the access that hung.
Input: a file with the text the rescue shows in /sys/fs/pstore/console-ramoops* (sweetlog_tail.sh fetches it), OR a raw dump of
the ramoops console zone (struct persistent_ram_buffer: u32 sig 0x43474244, u32 start, u32 size, data[] circular), OR a SWEETLOG
area dump (header "SWEETLOG" | u32 version | u32 size | u64 total | u64 seq, ring after one 4 KiB block; kernel 0047, not in the
current q10jcz line).  usage: sweetlog_tail.py FILE [N_LINES=80]"""
import struct, sys, re

def text_of(raw):
    if raw[:4] == struct.pack('<I', 0x43474244) and len(raw) >= 12:          # raw persistent_ram_buffer
        st, sz = struct.unpack_from('<II', raw, 4); data = raw[12:]; cap = len(data)
        if st <= cap and sz <= cap:
            return (data[st:] + data[:st])[-sz:] if sz == cap else data[(st - sz) % cap:st] if st >= sz else data[cap - (sz - st):] + data[:st]
    if raw[:8] == b'SWEETLOG':
        ver, size, total, seq = struct.unpack_from('<IIQQ', raw, 8); ring = raw[4096:4096 + size]
        n = min(total, size); end = total % size
        return (ring[end:] + ring[:end])[-n:] if total >= size else ring[:total]
    return raw

def main():
    raw = open(sys.argv[1], 'rb').read(); n = int(sys.argv[2]) if len(sys.argv) > 2 else 80
    lines = text_of(raw).decode('utf-8', 'replace').splitlines()
    gt = [l for l in lines if re.match(r'^GT \d+ \d+ ', l)]
    other = [l for l in lines if 'NORD2 GNSS' in l][-20:]
    print('== %d lines in the file, %d GNSS trace lines (GT), %d NORD2 GNSS lines' % (len(lines), len(gt), len(other)))
    if not gt:
        print('NO GT LINES: the trace zone was empty/overwritten (kernel without 0583, trace off by kern.n2gnss bit 3, or the reset cleared DRAM)')
        for l in lines[-n:]: print('  ' + l)
        return 1
    seqs = [int(l.split()[1]) for l in gt]
    gaps = sum(1 for a, b in zip(seqs, seqs[1:]) if b != a + 1)
    print('== seq %d .. %d (%d gaps), t %s .. %s ms' % (seqs[0], seqs[-1], gaps, gt[0].split()[2], gt[-1].split()[2]))
    for l in gt[-n:]: print('  ' + l)
    last = gt[-1].split(None, 3)[3]
    m = re.match(r'^(R|W) (0x[0-9a-f]+)', last)
    if m:
        print('== VERDICT: the last event is "%s" logged BEFORE the access and never completed: the phone died ON this %s of %s' %
              (last, 'read' if m.group(1) == 'R' else 'write', m.group(2)))
    else:
        print('== VERDICT: the last event "%s" completed; the hang came after it without a GNSS register access (look at the preceding lines)' % last)
    return 0

if __name__ == '__main__':
    sys.exit(main())
