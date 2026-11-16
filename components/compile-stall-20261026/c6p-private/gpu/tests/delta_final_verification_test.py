#!/usr/bin/env python3
"""Exercise real commit control flow with a fake ADB block device, no hardware."""
from pathlib import Path
import contextlib
import io
import json
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import commit_root_delta as delta


def run(skip, corrupt=False):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        (root / 'tools').mkdir()
        (root / 'tools/nord2-install-root.py').write_text('def preflight(adb, serial):\n assert serial == "fixture"\n')
        before = root / 'before/root.img'
        after = root / 'after/root.img'
        before.parent.mkdir(); after.parent.mkdir()
        a = b'A' * delta.CHUNK
        b = b'B' * delta.CHUNK
        before.write_bytes(a); after.write_bytes(b)
        manifest = dict(before='a' * 64, after='b' * 64, bytes=16 << 30,
                        changed=[0], chunks=[[delta.sha(a), delta.sha(b)]])
        (before.parent / 'verified.json').write_text(json.dumps(dict(sha256=manifest['before'], independent_full_hash_matches=True)))
        (after.parent / 'verified.json').write_text(json.dumps(dict(before=manifest['before'], after=manifest['after'], addon_sha256='c'*64, offline_verified=True, rehearsal=False)))
        state = dict(data=a, hashes=0, writes=0, reads=0)

        def adb(argv, **kw):
            if argv[-1] == 'devices': return 'List of devices attached\nfixture\tdevice\n'
            assert argv[1:5] == ['-s', 'fixture', 'shell', '-T']
            command = argv[-1]
            if '| sha256sum' in command:
                state['hashes'] += 1
                return (manifest['before' if state['data'] == a else 'after'] + '  -\n').encode()
            if command.startswith('dd if='):
                state['reads'] += 1
                return a if corrupt and state['writes'] else state['data']
            if 'dd of=' in command:
                assert 'seek=0' in command and 'conv=notrunc,fsync' in command
                assert kw['input'] == b
                state['data'] = kw['input']; state['writes'] += 1
                return b''
            if command == 'sync': return b''
            raise AssertionError('Unexpected device operation: ' + command)

        output = root / 'output'
        argv = ['commit_root_delta.py', '--main', str(root), '--before', str(before),
                '--after', str(after), '--output', str(output), '--commit',
                '--expected-addon-sha256', 'c'*64] + (['--skip-final-full-hash'] if skip else [])
        with patch.object(sys, 'argv', argv), patch.object(delta, 'build_manifest', return_value=manifest), patch.object(delta.subprocess, 'check_output', side_effect=adb), contextlib.redirect_stdout(io.StringIO()):
            try: delta.main()
            except AssertionError as e:
                assert corrupt and 'Written range readback mismatch' in str(e), e
                assert not (output / 'verified.json').exists()
            else:
                assert not corrupt
                receipt = json.loads((output / 'verified.json').read_text())
                assert receipt['full_device_readback_verified'] is (not skip)
                assert receipt['final_full_hash_skipped'] is skip
                assert receipt['all_device_preimages_verified'] and receipt['changed_ranges_readback_verified']
                assert state['hashes'] == (1 if skip else 2)
        assert state['writes'] == 1 and state['reads'] == 2


for skip in (False, True):
    run(skip)
    run(skip, corrupt=True)
print('PASS full/changed-range verification modes; preimage hash retained, corrupt readback refused in both modes')
