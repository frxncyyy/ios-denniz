#!/usr/bin/env python3
"""Replay a bounded internal HFS journal in a LOCAL CLONE, never a device.

Validates the complete journal and every enabled payload checksum before any
write. Requires byte-identical replay output from Apple's fsck journal_open
oracle. Unknown formats/transactions are refused, never silently discarded.
Format reference: apple-oss-distributions/hfs core/hfs_journal.{c,h} and
lib_fsck_hfs/fsck_journal.{c,h}. Linux hfsprogs 540.1 stubs journal_replay out.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import subprocess


def checksum(data):
    c = 0
    for x in data:
        c = ((c << 8) ^ (c + x)) & 0xffffffff
    return (~c) & 0xffffffff


def checked_sum(data, offset):
    old = struct.unpack_from('<I', data, offset)[0]
    value = bytearray(data)
    struct.pack_into('<I', value, offset, 0)
    if checksum(value) != old:
        raise ValueError('Invalid journal checksum (header/block list mid-update or corrupt: replay the journal '
                         'in the rescue image or boot iOS once and shut down cleanly before the next txn)')


def decode(raw, volume_bytes, journal_offset):
    if len(raw) < 512:
        raise ValueError('Short journal')
    magic, endian, start, end, size, blhdr, _, jhdr, sequence = struct.unpack_from('<IIQQQIIII', raw)
    if (magic, endian) != (0x4a4e4c78, 0x12345678):
        raise ValueError('Unsupported journal format/endian')
    checked_sum(raw[:44], 36)
    if size != len(raw) or not 512 <= jhdr <= 4096 or jhdr != 512:
        raise ValueError('Unexpected journal block geometry')
    if not jhdr <= start < size or not jhdr <= end < size:
        raise ValueError(f'Journal range: start=0x{start:x} end=0x{end:x} outside [0x{jhdr:x}, 0x{size:x}) '
                         '(corrupt header; replay/fsck in rescue or boot iOS once and shut down cleanly)')
    # blhdr_size is chosen by xnu size_up_tbuffer() from hw.memsize: (tbuffer/jhdr)*16, tbuffer
    # capped at MAX_TRANSACTION_BUFFER_SIZE=3 MiB -> at most 0x18000 (min phys_blksz). It legitimately
    # exceeds 64 KiB on >4.25 GiB devices (seen: 0x13000, cc-txn-r5c). journal_open's own check too.
    if not jhdr <= blhdr <= (3072*1024)//jhdr*16 or blhdr//16-1 > size//jhdr:
        raise ValueError(f'Journal blhdr_size 0x{blhdr:x} incompatible with jhdr 0x{jhdr:x} / size 0x{size:x}')
    if any(n % jhdr for n in (start, end, size, blhdr)):
        raise ValueError('Unaligned journal')
    remaining = (end-start) if end >= start else (size-start)+(end-jhdr)
    pos, previous = start, None
    writes, txns = [], []

    def ring(at, n):
        if not jhdr <= at < size or n < 0 or n > size-jhdr:
            raise ValueError('Journal read bounds')
        first = min(n, size-at)
        return raw[at:at+first]+raw[jhdr:jhdr+n-first]

    end_pos = None
    tail = False
    while True:
        if not remaining:
            if tail:
                break
            # Apple's journal_open also replays complete, in-sequence transactions written past
            # jhdr->end (header end not yet updated at crash time): follow them, but only while the
            # block list, bounds, sequence and data checksums are all valid. Anything else ends the walk.
            tail = True
            end_pos = pos
            free = (start-pos) if start > pos else (size-pos)+(start-jhdr)
            remaining = free
            if not remaining:
                break
        saved = (list(writes), list(txns), previous)
        try:
            head = ring(pos, 32)
            checked_sum(head, 8)
            maxblocks, numblocks, used, _, flags = struct.unpack_from('<HHIII', head)
            seq = struct.unpack_from('<I', head, 28)[0]
            if flags & ~3 or not flags & 1 or not 1 < numblocks <= maxblocks <= (blhdr-16)//16:
                raise ValueError('Unknown block list format')
            if not blhdr <= used <= remaining or used % jhdr:
                raise ValueError('Transaction bounds')
            if previous is not None and seq not in (previous, (previous+1)&0xffffffff):
                raise ValueError('Transaction sequence gap')
            if not txns and not flags & 2:
                raise ValueError('Journal starts inside a transaction')
            txn=ring(pos,used); at=blhdr
            txn_writes=[]
            for i in range(1,numblocks):
                block,n,crc=struct.unpack_from('<QII',txn,16+i*16)
                if not n or n%jhdr or at+n>used:
                    raise ValueError('Block data bounds')
                payload=txn[at:at+n];at+=n
                if block==0xffffffffffffffff:continue
                dest=block*jhdr
                if dest+n>volume_bytes or (dest<journal_offset+size and dest+n>journal_offset):
                    raise ValueError('Replay target out of bounds or in journal')
                if crc and checksum(payload)!=crc:
                    raise ValueError('Journal data checksum mismatch')
                txn_writes.append((dest,payload))
            if at!=used:
                raise ValueError('Unaccounted transaction data')
        except ValueError:
            if not tail:
                raise
            writes[:], txns[:], previous = saved[0], saved[1], saved[2]
            break
        writes.extend(txn_writes)
        txns.append({'position':pos,'bytes':used,'sequence':seq,'blocks':numblocks-1,'past_end':tail})
        previous=seq;remaining-=used
        pos=jhdr+(pos-jhdr+used)%(size-jhdr)
    if tail:
        end_for_check = end_pos if end_pos is not None else pos
    else:
        end_for_check = pos
    if end_for_check!=end or (txns and previous not in (sequence,(sequence-1)&0xffffffff)):
        raise ValueError('Journal end sequence mismatch')
    # Empty journal = start == end. xnu replay_journal ends with start = end = the position after the LAST replayed
    # transaction (including the ones past the old jhdr->end: 'resetting end') and keeps sequence_num. Writing only
    # start (as before the past-end support) left end at the old value when a past-end transaction was replayed:
    # start != end, i.e. a header claiming an almost full, unreplayed ring (fsck -n: 'Journal need to be replayed';
    # xnu would try to replay from a stale/invalid block list). cc-txn-wall3, 2026-11-14.
    header=bytearray(raw[:jhdr])
    struct.pack_into('<QQ',header,8,pos,pos)
    struct.pack_into('<I',header,36,0)
    struct.pack_into('<I',header,36,checksum(header[:44]))
    h_start,h_end=struct.unpack_from('<QQ',header,8)
    if h_start!=h_end or h_start!=pos or not jhdr<=pos<size or header[24:36]!=raw[24:36] or header[40:jhdr]!=raw[40:jhdr]:
        raise ValueError('Internal error: replayed journal header is not empty')
    checked_sum(bytes(header[:44]),36)
    return writes,bytes(header),txns


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('clone',type=Path)
    ap.add_argument('--oracle',required=True,type=Path)
    ap.add_argument('--receipt',required=True,type=Path)
    ap.add_argument('--apply',action='store_true')
    a=ap.parse_args()
    if a.clone.is_symlink() or not stat.S_ISREG(a.clone.stat().st_mode):
        raise ValueError('Only a regular offline clone is accepted')
    with a.clone.open('r+b' if a.apply else 'rb') as f:
        fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
        size=os.fstat(f.fileno()).st_size
        f.seek(1024);vh=f.read(512)
        if vh[:2]!=b'HX':raise ValueError('Expected HFSX')
        attrs,_,jib=struct.unpack_from('>III',vh,4);bs,blocks=struct.unpack_from('>II',vh,40)
        if not attrs&0x2000 or bs!=4096 or blocks*bs!=size:raise ValueError('Unexpected volume geometry')
        f.seek(jib*bs);ji=f.read(180)
        off,n=struct.unpack_from('>QQ',ji,36)
        if struct.unpack_from('>I',ji)[0]!=1 or n>32<<20 or off+n>size:raise ValueError('Unsupported journal location')
        f.seek(off);raw=f.read(n);assert len(raw)==n
        writes,header,txns=decode(raw,size,off)
        expected=b''.join(struct.pack('<QQ',at,len(data))+data for at,data in writes)
        oracle_output=a.receipt.with_suffix('.oracle.bin')
        subprocess.run([str(a.oracle.resolve()),str(a.clone.resolve()),str(oracle_output)],check=True,timeout=60)
        if oracle_output.read_bytes()!=expected:
            raise ValueError('Independent Apple replay disagrees (including transactions past journal end)')
        report={'image':str(a.clone),'journal_offset':off,'journal_bytes':n,
                'journal_sha256':hashlib.sha256(raw).hexdigest(),'transactions':txns,
                'writes':len(writes),'write_bytes':sum(len(b) for _,b in writes),
                'independent_apple_oracle_matches':True,'applied':False}
        if a.apply:
            for at,data in writes:f.seek(at);f.write(data)
            f.flush();os.fsync(f.fileno())
            # Publish an empty journal only after all validated transactions
            # have reached the clone. Never clear an unreplayed journal.
            f.seek(off);f.write(header);f.flush();os.fsync(f.fileno())
            f.seek(off);assert f.read(len(header))==header
            report['applied']=True
        a.receipt.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:v for k,v in report.items() if k!='transactions'}),flush=True)


if __name__=='__main__':main()
