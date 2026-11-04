#!/usr/bin/env python3
"""Synthetic HFS+ journal tests for replay_hfs_journal.decode (host only, no images)."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replay_hfs_journal as jr

J = 512


def build(size, blhdr, start, txns, seq0=100, corrupt_header=False):
    """txns: list of lists of (block_number, payload_bytes). Lays them circularly from start."""
    raw = bytearray(size)
    pos, seq = start, seq0

    def put(at, data):
        for i, b in enumerate(data):
            raw[J + (at - J + i) % (size - J)] = b

    for k, blocks in enumerate(txns):
        hdr = bytearray(blhdr)
        used = blhdr + sum(len(d) for _, d in blocks)
        struct.pack_into('<HHIII', hdr, 0, (blhdr - 16) // 16, len(blocks) + 1, used, 0, 1 | (2 if k == 0 else 0))
        struct.pack_into('<I', hdr, 28, seq)
        for i, (blk, d) in enumerate(blocks, 1):
            struct.pack_into('<QII', hdr, 16 + i * 16, blk, len(d), jr.checksum(d))
        struct.pack_into('<I', hdr, 8, jr.checksum(hdr[:32]))
        put(pos, hdr + b''.join(d for _, d in blocks))
        pos = J + (pos - J + used) % (size - J)
        last = seq
        seq += 1
    struct.pack_into('<IIQQQIIII', raw, 0, 0x4a4e4c78, 0x12345678, start, pos, size, blhdr, 0, J, last)
    struct.pack_into('<I', raw, 36, jr.checksum(raw[:44]))
    if corrupt_header:
        raw[12] ^= 1
    return bytes(raw), pos


VOL, OFF = 1 << 30, 512 << 20  # journal sits past the replay targets


def blocks(tag, n):
    return [(4096 + tag * 16 + i, bytes([tag + i]) * J * 2) for i in range(n)]


class Decode(unittest.TestCase):
    def test_linear(self):
        raw, end = build(1 << 20, 4096, J, [blocks(1, 2), blocks(2, 3)])
        w, h, t = jr.decode(raw, VOL, OFF)
        self.assertEqual((len(t), len(w), struct.unpack_from('<Q', h, 8)[0]), (2, 5, end))

    def test_wrapped(self):
        size = 256 * 1024
        start = size - 3 * J  # first block list header straddles the end of the ring
        raw, end = build(size, 4096, start, [blocks(1, 2), blocks(2, 4), blocks(3, 1)])
        self.assertLess(end, start)
        w, h, t = jr.decode(raw, VOL, OFF)
        self.assertEqual([x['sequence'] for x in t], [100, 101, 102])
        self.assertEqual([d for _, d in w], [d for b in (blocks(1, 2), blocks(2, 4), blocks(3, 1)) for _, d in b])

    def test_large_blhdr_like_r5c(self):
        # xnu size_up_tbuffer with hw.memsize ~4.75 GiB -> blhdr 0x13000 (> old 64 KiB cap), wrapped
        size = 8 << 20
        raw, end = build(size, 0x13000, size - 0x13000 // 2, [blocks(1, 3), blocks(2, 2)])
        self.assertLess(end, size - 0x13000 // 2)
        _, _, t = jr.decode(raw, VOL, OFF)
        self.assertEqual(len(t), 2)

    def test_blhdr_beyond_xnu_max(self):
        raw, _ = build(8 << 20, 0x19000, J, [blocks(1, 1)])
        with self.assertRaisesRegex(ValueError, 'blhdr_size 0x19000'):
            jr.decode(raw, VOL, OFF)

    def test_corrupt_header_message(self):
        raw, _ = build(1 << 20, 4096, J, [blocks(1, 1)], corrupt_header=True)
        with self.assertRaisesRegex(ValueError, 'shut down cleanly'):
            jr.decode(raw, VOL, OFF)

    def test_start_out_of_range(self):
        raw = bytearray(build(1 << 20, 4096, J, [blocks(1, 1)])[0])
        struct.pack_into('<Q', raw, 8, 1 << 21)
        struct.pack_into('<I', raw, 36, 0)
        struct.pack_into('<I', raw, 36, jr.checksum(raw[:44]))
        with self.assertRaisesRegex(ValueError, 'Journal range: start=0x200000'):
            jr.decode(bytes(raw), VOL, OFF)

    def check_empty_header(self, raw, h, pos):
        # start == end == pos, everything else as before, checksum valid
        self.assertEqual(struct.unpack_from('<QQ', h, 8), (pos, pos))
        self.assertEqual((h[:8], h[24:36], h[40:]), (raw[:8], raw[24:36], raw[40:J]))
        jr.checked_sum(h[:44], 36)

    def test_header_empty_after_replay(self):
        raw, end = build(1 << 20, 4096, J, [blocks(1, 2), blocks(2, 3)])
        _, h, t = jr.decode(raw, VOL, OFF)
        self.assertFalse(any(x['past_end'] for x in t))
        self.check_empty_header(raw, h, end)

    def past_end(self, size, start, txns, inside):
        # header end/sequence as xnu leaves them when the last transaction(s) reached the disk but the header did not:
        # end = after the first `inside` transactions, sequence_num = sequence of the next one
        raw, pos = build(size, 4096, start, txns)
        _, end = build(size, 4096, start, txns[:inside])
        raw = bytearray(raw)
        struct.pack_into('<Q', raw, 16, end)
        struct.pack_into('<I', raw, 44, 100 + inside)
        struct.pack_into('<I', raw, 36, 0)
        struct.pack_into('<I', raw, 36, jr.checksum(raw[:44]))
        return bytes(raw), end, pos

    def test_past_end_header_is_empty(self):
        # cc-txn-wall3: one complete transaction past jhdr->end is replayed; the published header must be EMPTY
        # (start == end == position after it), not start=after / end=old end (an almost full unreplayed ring).
        txns = [blocks(1, 2), blocks(2, 3), blocks(3, 1)]
        raw, end, pos = self.past_end(1 << 20, J, txns, 2)
        self.assertNotEqual(end, pos)
        w, h, t = jr.decode(raw, VOL, OFF)
        self.assertEqual([x['past_end'] for x in t], [False, False, True])
        self.assertEqual([d for _, d in w], [d for b in txns for _, d in b])
        self.check_empty_header(raw, h, pos)
        # decoding the journal with the published header finds nothing more to replay (idempotent, like xnu start == end)
        again = h + raw[J:]
        w2, h2, t2 = jr.decode(again, VOL, OFF)
        self.assertEqual((w2, t2, h2), ([], [], h))

    def test_past_end_wrapped_header_is_empty(self):
        size = 256 * 1024
        txns = [blocks(1, 2), blocks(2, 4), blocks(3, 1)]
        raw, end, pos = self.past_end(size, size - 12 * J, txns, 2)
        self.assertLess(pos, size - 12 * J)
        _, h, t = jr.decode(raw, VOL, OFF)
        self.assertEqual([x['past_end'] for x in t], [False, False, True])
        self.check_empty_header(raw, h, pos)

    def test_two_past_end_refused(self):
        # xnu rewrites the header after every transaction, so more than one past the end is not expected:
        # the decoder keeps refusing it (nothing is applied) rather than guessing
        raw, _, _ = self.past_end(1 << 20, J, [blocks(1, 2), blocks(2, 3), blocks(3, 1)], 1)
        with self.assertRaisesRegex(ValueError, 'Journal end sequence mismatch'):
            jr.decode(raw, VOL, OFF)

    def test_past_end_invalid_tail_stops_at_end(self):
        txns = [blocks(1, 2), blocks(2, 3), blocks(3, 1)]
        raw, end, pos = self.past_end(1 << 20, J, txns, 2)
        raw = bytearray(raw); raw[end + 4096 + 5] ^= 0xff  # corrupt the payload of the past-end transaction
        w, h, t = jr.decode(bytes(raw), VOL, OFF)
        self.assertEqual((len(t), len(w)), (2, 5))
        self.check_empty_header(raw, h, end)


if __name__ == '__main__':
    unittest.main()
