#!/usr/bin/env python3
"""storage-20261114 host tests (seconds): grown-volume snapshot (sparse mirror, bitmap-limited verification) and the
free-space preimage re-base of commit_root_delta_fast, on a small synthetic HFSX image (always_bytes scaled down).
The real device shell text runs locally through fastroot.ShellAdb, as in test_fastroot.py."""
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import types
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fastroot as fr
import snapshot_current_root_fast as sf
import commit_root_delta_fast as cm

MIB, BIG, BS = fr.MIB, fr.BIG, 4096
OLD = 64 * MIB          # stands for the 16 GiB original volume
SIZE = 256 * MIB        # grown volume: 16 chunks of 16 MiB


def make_volume(total_bytes, alloc_blocks_list):
    """Minimal non-journaled HFSX: VH + allocation bitmap at blocks 1..2 + alternate VH; only what the snapshot reads."""
    total = total_bytes // BS
    img = bytearray(total_bytes)
    vh = bytearray(512)
    vh[0:2] = b'HX'; struct.pack_into('>H', vh, 2, 5); struct.pack_into('>I', vh, 4, 0x100)
    struct.pack_into('>II', vh, 40, BS, total)
    nb = -(-total // (BS * 8))
    struct.pack_into('>QII', vh, 112, nb * BS, 0, nb); struct.pack_into('>II', vh, 128, 1, nb)
    img[1024:1536] = vh
    img[total_bytes - 1024:total_bytes - 512] = vh
    for b in list(alloc_blocks_list) + [0] + list(range(1, 1 + nb)) + [total - 1]:
        img[BS + b // 8] |= 0x80 >> (b % 8)
    return img


class Grown(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp(prefix='grown-'))
        self.dev = self.t / 'dev.img'
        # allocated: everything in the old area is "used" except it does not matter (always verified); past it:
        # one block in chunk 9 and the last block (alternate VH, chunk 15)
        img = make_volume(SIZE, [9 * 4096 + 5])
        img[9 * BIG + 5 * BS: 9 * BIG + 6 * BS] = os.urandom(BS)               # allocated data past the old end
        for c in (6, 12):                                                        # old Android garbage in free chunks
            img[c * BIG + 3 * MIB: c * BIG + 5 * MIB] = os.urandom(2 * MIB)
        img[9 * BIG + 10 * MIB: 9 * BIG + 11 * MIB] = os.urandom(MIB)           # garbage in free blocks of an allocated chunk
        self.dev.write_bytes(bytes(img) + os.urandom(8 * MIB))                   # + partition tail (SWEETLOG stand-in)
        stat = self.t / 'stat'; stat.write_text(' '.join(['0'] * 17) + '\n')
        self.p = dict(dev=str(self.dev), work=str(self.t / 'work'), stat=str(stat), guard='true')
        self.adb = fr.ShellAdb()
        self.mirror = self.t / 'mirror.img'
        # mirror = the pre-grow 64 MiB volume (stale in chunk 0)
        with open(self.mirror, 'wb') as f:
            f.write(bytes(img[:OLD]))
        with open(self.mirror, 'r+b') as f:
            f.seek(2 * MIB); f.write(b'stale')

    def tearDown(self):
        shutil.rmtree(self.t, ignore_errors=True)

    def snap(self, name='snap'):
        return sf.snapshot(self.adb, self.p, lambda: {}, self.mirror, self.t / name, SIZE, log=lambda *_: None, always_bytes=OLD)

    def test_snapshot_grown_sparse(self):
        r = self.snap()
        self.assertEqual(self.mirror.stat().st_size, SIZE)
        self.assertEqual(r['mirror_resized'], [OLD, SIZE])
        self.assertEqual(r['unverified_chunks'], [4, 5, 6, 7, 8, 10, 11, 12, 13, 14])
        dev, mir = self.dev.read_bytes(), self.mirror.read_bytes()
        for c in range(16):
            same = dev[c * BIG:(c + 1) * BIG] == mir[c * BIG:(c + 1) * BIG]
            self.assertEqual(same, c not in (6, 12), c)     # verified chunks equal; garbage never fetched
        # fetched: chunk 0 stale MiB + chunk 9 (data MiB + garbage MiB) + chunk 15 (alt VH MiB); nothing of chunks 6/12
        self.assertEqual(r['downloaded_bytes'], 4 * MIB)
        side = json.loads(Path(str(self.mirror) + '.manifest.json').read_text())
        self.assertEqual(side['unverified_chunks'], r['unverified_chunks'])
        self.assertEqual(fr.root_of(side['hashes']), r['root'])
        self.assertLess(os.stat(self.mirror).st_blocks * 512, OLD + 8 * MIB)   # still sparse
        # second snapshot: nothing differs, nothing downloaded
        r2 = self.snap('snap2')
        self.assertEqual(r2['changed_big_chunks'], [])
        self.assertEqual(r2['downloaded_bytes'], 0)

    def test_ungrown_unchanged_behaviour(self):
        dev = self.t / 'dev16.img'
        img = make_volume(OLD, [])
        dev.write_bytes(bytes(img))
        self.p['dev'] = str(dev)
        shutil.copy(dev, self.mirror)
        r = sf.snapshot(self.adb, self.p, lambda: {}, self.mirror, self.t / 's16', OLD, log=lambda *_: None, always_bytes=OLD)
        self.assertNotIn('unverified_chunks', r)
        self.assertNotIn('unverified_chunks', json.loads(Path(str(self.mirror) + '.manifest.json').read_text()))

    def test_mirror_shrinks_after_undo(self):
        self.snap()
        dev = self.t / 'dev16.img'
        dev.write_bytes(bytes(make_volume(OLD, [])))
        self.p['dev'] = str(dev)
        # the refusal floor is the real 16 GiB; scale it down for the test
        old_floor = fr.OLD_ROOT_BYTES
        fr.OLD_ROOT_BYTES = OLD
        try:
            r = sf.snapshot(self.adb, self.p, lambda: {}, self.mirror, self.t / 's16', OLD, log=lambda *_: None, always_bytes=OLD)
        finally:
            fr.OLD_ROOT_BYTES = old_floor
        self.assertEqual(self.mirror.stat().st_size, OLD)
        self.assertEqual(self.mirror.read_bytes(), dev.read_bytes())
        self.assertNotIn('unverified_chunks', r)

    def _delta(self, chunks_data):
        """Fake offline-update output: 1 MiB chunks -> new content; before = mirror content."""
        upd = self.t / 'upd'; upd.mkdir()
        table = []
        with open(upd / 'delta.bin', 'wb') as d, open(self.mirror, 'rb') as m:
            for pos, (c, data) in enumerate(sorted(chunks_data.items())):
                m.seek(c * MIB); before = m.read(MIB)
                d.write(data)
                table.append({'chunk': c, 'before': fr.sha(before), 'after': fr.sha(data), 'pos': pos})
        snap = json.loads((self.t / 'snap/verified.json').read_text())
        v = {'format': fr.FORMAT, 'kind': 'offline-update', 'before_root': snap['root'], 'mirror': str(self.mirror),
             'after_id': fr.delta_id(snap['root'], {e['chunk']: e['after'] for e in table}), 'addon_sha256': 'x',
             'bytes': SIZE, 'chunk_bytes': MIB, 'changed_chunks': len(table), 'delta': table, 'offline_verified': True, 'rehearsal': False}
        (upd / 'verified.json').write_text(json.dumps(v))
        return upd, table

    def commit(self, upd):
        a = types.SimpleNamespace(snapshot_receipt=self.t / 'snap/verified.json', update=upd, output=self.t / 'inst',
                                  expected_addon_sha256='x', commit=True, resume=False, batch_mib=64, jobs=2)
        cm.run(self.adb, self.p, lambda: {}, a, log=lambda *_: None)
        return json.loads((self.t / 'inst/verified.json').read_text())

    def test_commit_rebases_free_space_only(self):
        self.snap()
        new = {12 * 16 + 3: os.urandom(MIB),      # free chunk 12, device has garbage there -> re-based
               5 * 16 + 1: os.urandom(MIB),       # free chunk 5, device zeros == mirror -> normal
               9 * 16 + 0: os.urandom(MIB)}       # verified chunk 9 -> normal
        upd, table = self._delta(new)
        r = self.commit(upd)
        self.assertEqual(r['free_space_preimages_rebased'], 1)
        dev = self.dev.read_bytes()
        for c, data in new.items():
            self.assertEqual(dev[c * MIB:(c + 1) * MIB], data)
        side = json.loads(Path(str(self.mirror) + '.manifest.json').read_text())
        self.assertIn(12, side['unverified_chunks'])
        mir = self.mirror.read_bytes()
        for c, data in new.items():
            self.assertEqual(mir[c * MIB:(c + 1) * MIB], data)    # mirror advanced

    def test_commit_still_refuses_unknown_preimage_in_verified_chunk(self):
        self.snap()
        upd, _ = self._delta({9 * 16 + 2: os.urandom(MIB)})
        with open(self.dev, 'r+b') as f:            # device changed behind the snapshot in a VERIFIED chunk
            f.seek((9 * 16 + 2) * MIB); f.write(b'tamper')
        with self.assertRaises(fr.DeviceError):
            self.commit(upd)


if __name__ == '__main__':
    unittest.main()
