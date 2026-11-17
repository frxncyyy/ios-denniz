#!/usr/bin/env python3
"""Read-only RAM-rescue snapshot, seeding unchanged chunks from an old image.

Never writes a phone partition. The seed is immutable. An independent full
device SHA-256 must match the completed local image before it is usable.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess

CHUNK = 16 << 20


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--main', type=Path, required=True)
    ap.add_argument('--seed', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--adb', default='$HOME/Android/Sdk/platform-tools/adb')
    a = ap.parse_args()
    spec = importlib.util.spec_from_file_location('preflight', a.main / 'tools/nord2-install-root.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    devices = subprocess.check_output([a.adb, 'devices'], text=True).splitlines()[1:]
    live = [x.split()[0] for x in devices if len(x.split()) == 2 and x.split()[1] in ('device', 'recovery')]
    if len(live) != 1:
        raise RuntimeError('Exactly one authorized rescue device required')
    serial = live[0]
    preflight = mod.preflight(a.adb, serial)

    def remote(command, timeout=120):
        return subprocess.check_output([a.adb, '-s', serial, 'shell', '-T', command], timeout=timeout)

    header = remote(f'dd if={mod.BY_NAME} bs=4096 count=1 status=none')
    assert len(header) == 4096 and header[1024:1026] == b'HX'
    block, count = struct.unpack_from('>II', header, 1024 + 40)
    size = block * count
    assert block == 4096 and size == 16 << 30 and size == a.seed.stat().st_size
    a.output.mkdir(exist_ok=True)
    (a.output / 'preflight.json').write_text(json.dumps(preflight, indent=2) + '\n')
    (a.output / 'volume-header.bin').write_bytes(header)
    # Recompute device chunks every invocation, so interrupted work never trusts
    # a stale phone manifest after a subsequent iOS boot.
    commands = ('set -e; set -o pipefail; i=0; while [ "$i" -lt 1024 ]; do '
                'printf "%s " "$i"; '
                f'dd if={mod.BY_NAME} bs=1048576 skip=$((i * 16)) count=16 status=none | sha256sum; '
                'i=$((i + 1)); done')
    print('Hashing current unmounted device in 16 MiB chunks', flush=True)
    with (a.output / 'device-chunks.txt').open('wb') as out, (a.output / 'hash.stderr').open('wb') as err:
        subprocess.run([a.adb, '-s', serial, 'shell', '-T', commands], stdout=out, stderr=err,
                       check=True, timeout=1800)
    lines = (a.output / 'device-chunks.txt').read_text().splitlines()
    assert len(lines) == size // CHUNK
    expected = []
    for i, line in enumerate(lines):
        fields = line.split()
        assert len(fields) == 3 and fields[0] == str(i) and re.fullmatch('[0-9a-f]{64}', fields[1])
        expected.append(fields[1])
    snapshot = a.output / 'current-rootfs.img'
    assert snapshot.resolve() != a.seed.resolve()
    if not snapshot.exists():
        subprocess.run(['cp', '--reflink=auto', '--sparse=always', '--', str(a.seed), str(snapshot)], check=True)
    assert snapshot.stat().st_size == size and not snapshot.is_symlink()
    # Verified seeds are read-only; cp preserves their mode. Only this distinct
    # refresh copy may be writable, and it becomes read-only again on success.
    snapshot.chmod(0o600)
    transferred = 0
    differences = []
    with snapshot.open('r+b') as f:
        for i, sha in enumerate(expected):
            f.seek(i * CHUNK)
            data = f.read(CHUNK)
            if hashlib.sha256(data).hexdigest() != sha:
                data = remote(f'dd if={mod.BY_NAME} bs=1048576 skip={i * 16} count=16 status=none')
                assert len(data) == CHUNK and hashlib.sha256(data).hexdigest() == sha, 'Device changed or short read'
                f.seek(i * CHUNK)
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
                transferred += CHUNK
                differences.append(i)
                print(f'Refreshed chunk {i}/1024; downloaded {transferred // (1 << 20)} MiB', flush=True)
            (a.output / 'snapshot-progress.json').write_text(json.dumps({
                'checked_chunks': i + 1, 'total_chunks': len(expected), 'downloaded_bytes': transferred}) + '\n')
    mod.preflight(a.adb, serial)
    print('Checking independent full device and local SHA-256', flush=True)
    whole = remote(f'set -o pipefail; dd if={mod.BY_NAME} bs=1048576 count={size // (1 << 20)} status=none | sha256sum', 1800).decode().split()[0]
    with snapshot.open('rb') as f:
        actual = hashlib.file_digest(f, 'sha256').hexdigest()
    assert whole == actual, 'Snapshot full device hash mismatch'
    snapshot.chmod(0o444)
    result = {'source': str(a.seed), 'snapshot': str(snapshot), 'bytes': size,
              'sha256': actual, 'device_sha256': whole, 'independent_full_hash_matches': True,
              'downloaded_bytes_this_run': transferred, 'changed_chunks_this_run': differences,
              'phone_partition_writes': 0}
    (a.output / 'verified.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'changed_chunks_this_run'}, indent=2))


if __name__ == '__main__':
    main()
