#!/usr/bin/env python3
"""Host-only tests: the real device shell text runs locally against a regular file standing in for the block device."""
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import types
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fastroot as fr
import snapshot_current_root_fast as sf
import commit_root_delta_fast as cm

MIB = fr.MIB
SIZE = 64 * MIB


class Base(unittest.TestCase):
    def setUp(self):
        self.t = Path(tempfile.mkdtemp(prefix='fastroot-'))
        self.dev = self.t / 'dev.img'
        data = bytearray(os.urandom(SIZE))
        data[8 * MIB:20 * MIB] = bytes(12 * MIB)        # compressible region
        self.dev.write_bytes(bytes(data))
        self.statf = self.t / 'stat'
        self.setstat(0)
        self.p = dict(dev=str(self.dev), work=str(self.t / 'work'), stat=str(self.statf), guard='true')
        self.shim = self.t / 'shim'
        self.shim.mkdir()
        self.adb = fr.ShellAdb()
        self.mirror = self.t / 'mirror.img'
        self.pre_calls = 0

    def tearDown(self):
        shutil.rmtree(self.t, ignore_errors=True)

    def setstat(self, writes):
        f = ['0'] * 17
        f[4] = str(writes)
        self.statf.write_text(' '.join(f) + '\n')

    def poke(self, path, off, n=16):
        with open(path, 'r+b') as f:
            f.seek(off); b = bytearray(f.read(n)); b[0] ^= 0xff; f.seek(off); f.write(bytes(b))

    def snap(self, preflight=None):
        shutil.copy(self.dev, self.mirror)
        self.mirror.chmod(0o600)
        self.poke(self.mirror, 3 * MIB + 7)          # mirror stale in chunk 0 (piece 3)
        self.poke(self.mirror, 40 * MIB + 5)         # and chunk 2 (piece 40)
        return sf.snapshot(self.adb, self.p, preflight or (lambda: {}), self.mirror, self.t / 'snap', SIZE,
                           log=lambda *_: None)


class SnapshotTests(Base):
    def test_refresh_only_differing_pieces(self):
        r = self.snap()
        self.assertEqual(self.mirror.read_bytes(), self.dev.read_bytes())
        self.assertEqual(r['changed_big_chunks'], [0, 2])
        self.assertEqual(r['downloaded_bytes'], 2 * MIB)
        self.assertEqual(r['phone_partition_writes'], 0)
        self.assertTrue(r['compressed'])

    def test_corrupt_transfer_rejected(self):
        (self.shim / 'gzip').write_text('#!/bin/bash\nexec /usr/bin/gzip "$@" | head -c 100\n')
        (self.shim / 'gzip').chmod(0o755)
        self.adb = fr.ShellAdb({'PATH': f'{self.shim}:{os.environ["PATH"]}'})
        with self.assertRaises(fr.DeviceError):
            self.snap()

    def test_write_during_snapshot_detected(self):
        def pre():
            self.pre_calls += 1
            if self.pre_calls == 2:
                self.setstat(1)
            return {}
        with self.assertRaises(fr.DeviceError):
            self.snap(pre)

    def test_hash_matches_python(self):
        got = fr.device_hashes(self.adb, self.p, range(4), 16, 3)
        want = fr.hash_file_chunks(self.dev, fr.BIG, range(4))
        self.assertEqual(got, want)


class InstallTests(Base):
    def prepare(self, changes=(5, 33, 34)):
        r = self.snap()
        self.snapshot_receipt = self.t / 'snap' / 'verified.json'
        upd = self.t / 'upd'; upd.mkdir()
        entries, pos = [], 0
        mirror = self.mirror.read_bytes()
        with (upd / 'delta.bin').open('wb') as df:
            for c in changes:
                before = mirror[c * MIB:(c + 1) * MIB]
                after = bytes([c]) * MIB
                df.write(after)
                entries.append({'chunk': c, 'before': fr.sha(before), 'after': fr.sha(after), 'pos': pos}); pos += 1
        delta = {e['chunk']: e['after'] for e in entries}
        fr.write_json_atomic(upd / 'verified.json', {'format': fr.FORMAT, 'kind': 'offline-update', 'before_root': r['root'],
            'after_id': fr.delta_id(r['root'], delta), 'addon_sha256': 'a' * 64, 'bytes': SIZE, 'offline_verified': True,
            'rehearsal': False, 'delta': entries})
        self.upd, self.entries = upd, entries
        self.want = bytearray(mirror)
        for e in entries:
            self.want[e['chunk'] * MIB:(e['chunk'] + 1) * MIB] = bytes([e['chunk']]) * MIB
        return r

    def args(self, **kw):
        return types.SimpleNamespace(snapshot_receipt=self.snapshot_receipt, update=self.upd, output=self.t / 'out',
                                     expected_addon_sha256='a' * 64, commit=True, resume=False, batch_mib=2, jobs=2,
                                     advance_mirror=True, **kw)

    def go(self, **kw):
        return cm.run(self.adb, self.p, lambda: {}, self.args(**kw), log=lambda *_: None)

    def test_happy_path_epoch_skip_and_mirror_advance(self):
        self.prepare()
        self.assertEqual(self.go(), 'epoch')
        self.assertEqual(self.dev.read_bytes(), bytes(self.want))
        self.assertEqual(self.mirror.read_bytes(), bytes(self.want))     # warm mirror advanced
        side = json.loads(Path(str(self.mirror) + '.manifest.json').read_text())
        self.assertEqual(side['hashes'], [h for _, h in sorted(fr.hash_file_chunks(self.mirror, fr.BIG, range(4)).items())])
        v = json.loads((self.t / 'out' / 'verified.json').read_text())
        self.assertTrue(v['changed_ranges_readback_verified'])

    def test_preimage_mismatch_aborts_before_any_write(self):
        self.prepare()
        self.poke(self.dev, 34 * MIB + 3)            # device drifted in a chunk we would write (epoch unchanged)
        before = self.dev.read_bytes()
        with self.assertRaises(fr.DeviceError):
            self.go()
        self.assertEqual(self.dev.read_bytes(), before)

    def test_preimage_checked_on_device_right_before_each_write(self):
        self.prepare()
        ents = [(e['chunk'], e['before'], e['after']) for e in self.entries]
        self.poke(self.dev, 33 * MIB + 1)             # second chunk drifts after classification
        before = self.dev.read_bytes()
        payload = b''.join(bytes([e[0]]) * MIB for e in ents)
        self.adb.shell(f'mkdir -p {self.p["work"]} && cat > {self.p["work"]}/batch.bin', data=payload)
        with self.assertRaises(fr.DeviceError) as cm_:
            fr.parse_apply(self.adb_try(fr.script_apply(self.p, ents, [0, 1, 2])), ents)
        after = self.dev.read_bytes()
        self.assertEqual(after[33 * MIB:], before[33 * MIB:])              # not written at/after the bad chunk
        self.assertEqual(after[5 * MIB:6 * MIB], bytes([5]) * MIB)         # earlier chunk was legitimately written

    def adb_try(self, script):
        try:
            return self.adb.shell(script)
        except fr.DeviceError as e:
            raise fr.DeviceError('batch failed: ' + str(e))

    def test_payload_corruption_refused(self):
        self.prepare()
        ents = [(e['chunk'], e['before'], e['after']) for e in self.entries]
        payload = bytearray(b''.join(bytes([e[0]]) * MIB for e in ents)); payload[10] ^= 1
        self.adb.shell(f'mkdir -p {self.p["work"]} && cat > {self.p["work"]}/batch.bin', data=bytes(payload))
        before = self.dev.read_bytes()
        with self.assertRaises(fr.DeviceError):
            self.adb.shell(fr.script_apply(self.p, ents, [0, 1, 2]))
        self.assertEqual(self.dev.read_bytes(), before)

    def test_readback_mismatch_fails(self):
        self.prepare()
        shim = self.shim / 'dd'
        shim.write_text('#!/bin/bash\n/usr/bin/dd "$@"; rc=$?\nfor a in "$@"; do case $a in seek=*) s=${a#seek=};; of=*) o=${a#of=};; esac; done\n'
                        'if [ -n "$o" ] && [ "$o" = "%s" ]; then printf "\\377" | /usr/bin/dd of="$o" bs=1 seek=$((s*1048576+9)) conv=notrunc status=none; fi\nexit $rc\n' % self.dev)
        shim.chmod(0o755)
        self.adb = fr.ShellAdb({'PATH': f'{self.shim}:{os.environ["PATH"]}'})
        with self.assertRaises(fr.DeviceError) as e:
            self.go()
        self.assertIn('readback', str(e.exception))

    def test_epoch_change_forces_full_verify_and_catches_drift(self):
        self.prepare()
        self.setstat(5)                                   # something wrote since the snapshot
        self.assertEqual(self.go(), 'full')               # content identical -> proceeds
        self.assertEqual(self.dev.read_bytes(), bytes(self.want))

    def test_epoch_change_with_changed_unchanged_chunk_refused(self):
        self.prepare()
        self.setstat(5)
        self.poke(self.dev, 50 * MIB)                    # outside the delta
        before = self.dev.read_bytes()
        with self.assertRaises(fr.DeviceError):
            self.go()
        self.assertEqual(self.dev.read_bytes(), before)

    def test_resume_after_partial(self):
        self.prepare()
        # simulate: chunk 5 already written (verified), then interrupted
        with open(self.dev, 'r+b') as f:
            f.seek(5 * MIB); f.write(bytes([5]) * MIB)
        (self.t / 'out').mkdir()
        (self.t / 'out' / 'started.json').write_text('{}')
        a = self.args(); a.resume = True
        cm.run(self.adb, self.p, lambda: {}, a, log=lambda *_: None)
        self.assertEqual(self.dev.read_bytes(), bytes(self.want))


class CacheDirInstall(unittest.TestCase):
    """The shared pipeline-cache directory the offline guest script creates, and its catalog allowlist."""

    @staticmethod
    def block():
        import update_metal_offline_vm as old
        text = old.VM_SCRIPT
        a, b = text.index('# METAL_CACHE_DIR_BEGIN'), text.index('# METAL_CACHE_DIR_END')
        return text[a:b]

    def run_block(self, root, chowns):
        import json as _json
        env = {'os': types.SimpleNamespace(chown=lambda p, u, g: chowns.append((str(p), u, g)), chmod=os.chmod,
                                           stat=os.stat), 'json': _json, 'r': root}
        exec(self.block(), env)

    def test_guest_creates_mobile_owned_shared_dir_and_drops_root_leftovers(self):
        t = Path(tempfile.mkdtemp(prefix='cachedir-'))
        self.addCleanup(shutil.rmtree, t, True)
        leaf = t / 'private/var/mobile/Library/Caches/com.tegu.metal.pipelines'
        leaf.mkdir(parents=True)
        os.chmod(leaf, 0o700)
        for name in ('k1-00001.tpc', 'k2-65535.tpc', 'misses.txt', '.tmp-4-1', '.probe-9'):
            (leaf / name).write_bytes(b'x')
        (leaf / 'keep.txt').write_bytes(b'y')            # not ours: untouched
        (leaf / 'k3-00001.tpc').write_bytes(b'z')        # unknown kind: untouched
        # os.stat in the block must see uid 501: stand in with the real mode and a fixed owner.
        real = os.stat
        calls = []
        class St:
            def __init__(self, st): self.st_mode, self.st_uid, self.st_gid = st.st_mode, 501, 501
        import json as _json
        env = {'os': types.SimpleNamespace(chown=lambda p, u, g: calls.append((Path(p).name, u, g)), chmod=os.chmod,
                                           stat=lambda p: St(real(p))), 'json': _json, 'r': t}
        exec(self.block(), env)
        self.assertEqual(sorted(x.name for x in leaf.iterdir()), ['k3-00001.tpc', 'keep.txt'])
        self.assertEqual(calls, [('com.tegu.metal.pipelines', 501, 501)])
        self.assertEqual(os.stat(leaf).st_mode & 0o7777, 0o755)
        exec(self.block(), env)                          # idempotent
        self.assertEqual(sorted(x.name for x in leaf.iterdir()), ['k3-00001.tpc', 'keep.txt'])

    def test_guest_refuses_non_directory_and_creates_missing_caches(self):
        t = Path(tempfile.mkdtemp(prefix='cachedir-'))
        self.addCleanup(shutil.rmtree, t, True)
        caches = t / 'private/var/mobile/Library/Caches'
        caches.mkdir(parents=True)
        (caches / 'com.tegu.metal.pipelines').symlink_to('/tmp')
        with self.assertRaises(SystemExit):
            self.run_block(t, [])
        (caches / 'com.tegu.metal.pipelines').unlink()
        shutil.rmtree(caches)
        # Caches missing: created 501:501 0700 and the leaf made (stat is the real one -> uid assert fails
        # only because we are not 501; the creation calls are what matter here).
        chowns = []
        try:
            self.run_block(t, chowns)
        except AssertionError:
            pass
        self.assertTrue((caches / 'com.tegu.metal.pipelines').is_dir())
        self.assertEqual(chowns[0], (str(caches), 501, 501))

    def test_fast_and_old_installer_share_the_guest_script(self):
        import update_metal_offline_fast as fast
        script = fast.vm_script()
        self.assertIn('METAL_CACHE_DIR_READY', script)
        self.assertIn('/src/fastroot-journal.bin', script)

    def test_catalog_allowlist_covers_only_the_cache_dir(self):
        import check_update_catalog_fast as cc
        d = '/private/var/mobile/Library/Caches/com.tegu.metal.pipelines'
        def unknown(k):
            return k not in cc.OWNED and not any(k.startswith(x) for x in cc.PREFIXES)
        self.assertFalse(unknown(d))
        self.assertFalse(unknown(d + '/k1-00001.tpc'))
        self.assertTrue(unknown('/private/var/mobile/Library/Caches'))
        self.assertTrue(unknown('/private/var/mobile/Library/Caches/com.tegu.metal.pipelinesX'))
        self.assertTrue(unknown('/private/var/mobile/Library/Caches/other'))

    def test_frontend_tries_the_shared_dir_first_and_logs_why_others_fail(self):
        text = (Path(__file__).resolve().parents[1] / 'metal/tegu/pipeline_cache_frontend.inc').read_text()
        order = text.index('{"/private/var/mobile","/Library/Caches"}')
        self.assertLess(order, text.index('getenv("HOME"),"/Library/Caches"'))
        self.assertLess(text.index('getenv("HOME"),"/Library/Caches"'), text.index('{"/private/var/tmp",""}'))
        self.assertIn('TEGU_PIPELINE_CACHE_DIRS', text)
        self.assertIn('TEGU_PERF_DISK', text)


if __name__ == '__main__':
    unittest.main(verbosity=2)
