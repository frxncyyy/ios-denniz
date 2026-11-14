#!/usr/bin/env python3
"""Offline grow of the Nord2 iOS root HFS+/HFSX volume to (almost) the whole userdata partition.

What Apple's hfs_extendfs() does online, done offline on a CLEAN, unmounted volume:
  * the allocation bitmap gets a second extent placed at the first block past the old end
    (those blocks are marked used in the new bitmap part),
  * a new alternate volume header is written 1024 bytes before the new end (its block marked used),
  * the old alternate volume header block (old last block) is freed and zeroed,
  * the primary volume header gets totalBlocks/freeBlocks/allocationFile updated (commit point).
Nothing else moves; no catalog/extents/attributes/journal change. The journal must be empty.

The tail of the partition is NOT given to HFS: sweet_ufs.c writes the persistent console log
(SWEETLOG, 4 MiB + 4 KiB) to the last 1025 blocks of userdata. New size =
floor((partition_blocks - reserve) / 32768) * 32768 blocks, i.e. a whole number of bitmap blocks
(no partial last bitmap block) and >= reserve (default 16384 blocks = 64 MiB) left at the end.

Pure planner: plan(read) -> writes. Used by
  * apply-image IMAGE   (rehearsal on a file / VM block device; also 'undo-image' and 'check-image'),
  * commit_hfs_grow.py  (the RAM-rescue device step, through the fastroot verified-chunk writer).
"""
import argparse
import hashlib
import json
import os
import struct
import sys

BS = 4096
BITS_PER_BLOCK = BS * 8
ATTR_UNMOUNTED = 0x100
ATTR_INCONSISTENT = 0x800
ATTR_JOURNALED = 0x2000
JMAGIC = 0x4a4e4c78
JENDIAN = 0x12345678


class Refusal(Exception):
    pass


def u16(b, o): return struct.unpack_from('>H', b, o)[0]
def u32(b, o): return struct.unpack_from('>I', b, o)[0]
def u64(b, o): return struct.unpack_from('>Q', b, o)[0]


def sha(b):
    return hashlib.sha256(b).hexdigest()


def parse_vh(vh):
    if vh[:2] not in (b'H+', b'HX') or u16(vh, 2) not in (4, 5):
        raise Refusal('not an HFS+/HFSX volume header')
    f = dict(sig=vh[:2].decode(), attrs=u32(vh, 4), jib=u32(vh, 12), block=u32(vh, 40), total=u32(vh, 44),
             free=u32(vh, 48), write_count=u32(vh, 68))
    o = 112
    f['alloc_logical'] = u64(vh, o)
    f['alloc_blocks'] = u32(vh, o + 12)
    f['alloc_ext'] = [(u32(vh, o + 16 + 8 * i), u32(vh, o + 20 + 8 * i)) for i in range(8)]
    return f


def journal_state(read, f):
    """Return a description; raise unless the journal is empty (or the volume is not journaled)."""
    if not (f['attrs'] & ATTR_JOURNALED):
        return {'journaled': False}
    jib = read(f['jib'] * BS, 52)
    joff, jsize = u64(jib, 36), u64(jib, 44)
    if u32(jib, 0) & 0x2:
        raise Refusal('external journal unsupported')
    h = read(joff, 64)
    for fmt in ('<', '>'):
        magic, endian, start, end, size = struct.unpack_from(fmt + 'IIQQQ', h, 0)
        if magic == JMAGIC and endian == JENDIAN:
            break
    else:
        raise Refusal('journal header magic not found')
    if size != jsize:
        raise Refusal('journal size mismatch (info block %d, header %d)' % (jsize, size))
    if start != end:
        raise Refusal('journal is NOT empty (start %d end %d): replay/clean unmount first' % (start, end))
    return {'journaled': True, 'offset': joff, 'size': jsize, 'start': start, 'end': end}


def target_blocks(part_bytes, reserve_blocks):
    pb = part_bytes // BS
    return ((pb - reserve_blocks) // BITS_PER_BLOCK) * BITS_PER_BLOCK


def set_bit(buf, bit, value):
    byte, mask = bit >> 3, 0x80 >> (bit & 7)
    old = bool(buf[byte] & mask)
    buf[byte] = (buf[byte] | mask) if value else (buf[byte] & ~mask & 0xff)
    return old


def alloc_block_of_bit(f, bit):
    """Device block holding bitmap bit `bit` (old allocation file extents only)."""
    fb = bit // BITS_PER_BLOCK
    for start, count in f['alloc_ext']:
        if fb < count:
            return start + fb
        fb -= count
    raise Refusal('bit beyond the allocation file')


def plan(read, part_bytes, reserve_blocks=16384, expect_total=None):
    """read(offset, n) -> bytes. Returns the ordered block writes (offset, new bytes, old bytes)."""
    blk0 = read(0, BS)
    vh = blk0[1024:1536]
    f = parse_vh(vh)
    if f['block'] != BS:
        raise Refusal('block size %d unsupported' % f['block'])
    if not (f['attrs'] & ATTR_UNMOUNTED) or (f['attrs'] & ATTR_INCONSISTENT):
        raise Refusal('volume not cleanly unmounted (attributes 0x%x)' % f['attrs'])
    if expect_total is not None and f['total'] != expect_total:
        raise Refusal('volume has %d blocks, expected %d' % (f['total'], expect_total))
    j = journal_state(read, f)
    old, new = f['total'], target_blocks(part_bytes, reserve_blocks)
    if new <= old:
        raise Refusal('nothing to grow: volume %d blocks, target %d' % (old, new))
    if new >= 1 << 32:
        raise Refusal('target exceeds 32-bit block count')
    if part_bytes // BS - new < 1025 + 1:
        raise Refusal('tail reserve would overlap the SWEETLOG area')
    used_ext = [e for e in f['alloc_ext'] if e[1]]
    if any(not c for _, c in f['alloc_ext'][:len(used_ext)]) or len(used_ext) >= 8:
        raise Refusal('allocation file extents not compact or no free extent slot (overflow extents unsupported)')
    if sum(c for _, c in used_ext) != f['alloc_blocks'] or f['alloc_logical'] != f['alloc_blocks'] * BS:
        raise Refusal('allocation fork inconsistent')
    if f['alloc_blocks'] * BITS_PER_BLOCK < old:
        raise Refusal('allocation file does not cover the volume')
    need = -(-new // BITS_PER_BLOCK)
    ext = need - f['alloc_blocks']
    if ext <= 0:
        raise Refusal('bitmap already large enough (unexpected)')
    ext_start = old
    # old alternate VH must be where HFS puts it (proves the old last block is the altVH block)
    old_last = old - 1
    olb = read(old_last * BS, BS)
    alt = olb[BS - 1024:BS - 512]
    if alt[:2] != vh[:2] or parse_vh(alt)['total'] != old:
        raise Refusal('old alternate volume header not found at the old end')
    # old bitmap block with the old-last-block bit
    bblk = alloc_block_of_bit(f, old_last)
    bmap_old = read(bblk * BS, BS)
    bmap_new = bytearray(bmap_old)
    if not set_bit(bmap_new, old_last % BITS_PER_BLOCK, False):
        raise Refusal('old alternate VH block is not marked allocated')
    # bits past the old end inside the old bitmap (none when the bitmap covers exactly `old` bits) must be clear
    covered = f['alloc_blocks'] * BITS_PER_BLOCK
    if covered > old:
        raise Refusal('old bitmap covers more than the volume: unsupported layout, review')
    # new bitmap extension: bits [old, old+ext) set (the extension itself), bit new-1 set (new altVH)
    extbuf = bytearray(ext * BS)
    base = f['alloc_blocks'] * BITS_PER_BLOCK          # first bit stored in the extension
    for b in range(ext_start, ext_start + ext):
        set_bit(extbuf, b - base, True)
    set_bit(extbuf, (new - 1) - base, True)
    # new volume header
    nvh = bytearray(vh)
    struct.pack_into('>I', nvh, 44, new)
    struct.pack_into('>I', nvh, 48, f['free'] + (new - old) - ext)   # +old altVH freed, -new altVH used: net 0
    struct.pack_into('>I', nvh, 68, (f['write_count'] + 1) & 0xffffffff)
    struct.pack_into('>Q', nvh, 112, need * BS)
    struct.pack_into('>I', nvh, 112 + 12, need)
    k = len(used_ext)
    struct.pack_into('>II', nvh, 112 + 16 + 8 * k, ext_start, ext)
    nvh = bytes(nvh)
    nblk0 = blk0[:1024] + nvh + blk0[1536:]
    new_last = new - 1
    old_new_last = read(new_last * BS, BS)
    nlast = bytes(BS - 1024) + nvh + bytes(512)
    old_ext = read(ext_start * BS, ext * BS)
    nolb = olb[:BS - 1024] + bytes(1024)
    # Apple's HFS (hfs_MountHFSPlusVolume 'degenerate case', FS smaller than partition) also keeps an alternate VH
    # 1024 bytes before the END OF THE PARTITION (hfs_partition_avh_sector), and fsck_hfs only looks there
    # (InitializeVolumeObject: totalDeviceSectors - 2). An old-geometry copy there makes fsck_hfs 'repair' the
    # volume back to 16 GiB. Overwrite it with the new VH (only those 512 bytes; on Nord2 this block is the last
    # block of the SWEETLOG ring, which the iOS kernel already overwrites the same way).
    part_last = part_bytes // BS - 1
    if part_last <= new_last:
        raise Refusal('no room between the volume end and the partition end')
    opart = read(part_last * BS, BS)
    npart = opart[:BS - 1024] + nvh + opart[BS - 512:]
    writes = [  # order matters: everything past the old end first, then the commit point, then cleanup
        dict(what='bitmap extension', off=ext_start * BS, new=bytes(extbuf), old=old_ext),
        dict(what='new alternate VH', off=new_last * BS, new=nlast, old=old_new_last),
        dict(what='primary VH (commit point)', off=0, new=nblk0, old=blk0),
        dict(what='old bitmap block: free old altVH block', off=bblk * BS, new=bytes(bmap_new), old=bmap_old),
        dict(what='zero old alternate VH', off=old_last * BS, new=nolb, old=olb),
        dict(what='partition alternate VH', off=part_last * BS, new=npart, old=opart),
    ]
    if bblk == 0:
        raise Refusal('bitmap in block 0 (unexpected)')
    summary = dict(partition_avh_before=('valid VH, %d blocks' % parse_vh(opart[BS - 1024:BS - 512])['total']) if opart[BS - 1024:BS - 1022] in (b'H+', b'HX') else 'no VH',
                   old_blocks=old, new_blocks=new, partition_blocks=part_bytes // BS, reserve_tail_blocks=part_bytes // BS - new,
                   old_bytes=old * BS, new_bytes=new * BS, bitmap_blocks_old=f['alloc_blocks'], bitmap_blocks_new=need,
                   bitmap_extension=[ext_start, ext], old_free=f['free'], new_free=u32(nvh, 48), journal=j,
                   attrs='0x%x' % f['attrs'], signature=f['sig'])
    return summary, writes


def merge_same_block(writes):
    """VH block 0 and a bitmap block may coincide only if bblk==0 (refused), so writes are disjoint."""
    spans = sorted((w['off'], w['off'] + len(w['new'])) for w in writes)
    for (a0, a1), (b0, b1) in zip(spans, spans[1:]):
        if b0 < a1:
            raise Refusal('overlapping writes')
    return writes


def file_rw(path, size=None):
    fd = os.open(path, os.O_RDWR)
    if size is None:
        size = os.lseek(fd, 0, os.SEEK_END)

    def read(off, n):
        b = os.pread(fd, n, off)
        if len(b) != n:
            raise Refusal('short read at %d' % off)
        return b

    def write(off, data):
        if os.pwrite(fd, data, off) != len(data):
            raise Refusal('short write at %d' % off)
        os.fsync(fd)
    return fd, size, read, write


def undo_check(read, summary, writes):
    """Undo is exact only while nothing was allocated past the old end except the grow-owned bits."""
    ext_start, ext = summary['bitmap_extension']
    cur = read(ext_start * BS, ext * BS)
    if cur != [w for w in writes if w['what'] == 'bitmap extension'][0]['new']:
        raise Refusal('blocks past the old end are in use (or bitmap changed): undo would lose data; refuse')
    vh = parse_vh(read(1024, 512))
    if vh['total'] != summary['new_blocks'] or vh['free'] != summary['new_free']:
        raise Refusal('volume header changed since the grow (free %d, expected %d): refuse' % (vh['free'], summary['new_free']))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=['plan-image', 'apply-image', 'undo-image'])
    ap.add_argument('image')
    ap.add_argument('--part-bytes', type=int, required=True, help='size of the userdata partition (224657344*512 on Nord2)')
    ap.add_argument('--reserve-blocks', type=int, default=16384)
    ap.add_argument('--expect-total', type=int, default=4194304)
    ap.add_argument('--receipt', help='JSON written by apply-image (preimages, hex) and read by undo-image')
    a = ap.parse_args()
    fd, size, read, write = file_rw(a.image)
    if size < a.part_bytes:
        raise Refusal('image smaller than --part-bytes')
    if a.mode == 'undo-image':
        rec = json.load(open(a.receipt))
        writes = [dict(what=w['what'], off=w['off'], new=bytes.fromhex(w['new']), old=bytes.fromhex(w['old'])) for w in rec['writes']]
        undo_check(read, rec['summary'], writes)
        for w in reversed(writes):
            if read(w['off'], len(w['new'])) != w['new']:
                raise Refusal('device no longer holds what the grow wrote at %s' % w['what'])
        for w in reversed(writes):
            write(w['off'], w['old'])
            print('HFSGROW_UNDO wrote', w['what'], w['off'], len(w['old']))
        print('HFSGROW_UNDO_DONE')
        return
    summary, writes = plan(read, a.part_bytes, a.reserve_blocks, a.expect_total)
    merge_same_block(writes)
    print('HFSGROW_PLAN', json.dumps(summary))
    for w in writes:
        print('HFSGROW_WRITE %-42s off=%d len=%d old=%s new=%s' % (w['what'], w['off'], len(w['new']), sha(w['old'])[:12], sha(w['new'])[:12]))
    if a.mode == 'plan-image':
        return
    if a.receipt:
        with open(a.receipt, 'w') as o:
            json.dump({'summary': summary, 'writes': [dict(what=w['what'], off=w['off'], new=w['new'].hex(), old=w['old'].hex()) for w in writes]}, o)
    for w in writes:
        write(w['off'], w['new'])
        if read(w['off'], len(w['new'])) != w['new']:
            raise Refusal('readback mismatch at ' + w['what'])
    print('HFSGROW_DONE new_blocks=%d new_bytes=%d' % (summary['new_blocks'], summary['new_bytes']))


if __name__ == '__main__':
    try:
        main()
    except Refusal as e:
        print('HFSGROW_REFUSED', e)
        sys.exit(2)
