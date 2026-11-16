#!/usr/bin/env python3
"""Offline Metal update on a qcow2 overlay of the warm mirror (no 16 GiB copy).

Same VM script, same fsck gates and same addon logic as update_metal_offline_vm.py,
except:
  * the VM writes into a qcow2 overlay (cluster 1 MiB) whose backing file is the
    read-only mirror; nothing is copied, nothing is hashed over the full image;
  * the Apple-oracle-checked HFS journal replay is applied inside the VM to the
    overlay (host side validates journal + oracle on the mirror, read-only);
  * the post-journal "fsck -n" only runs if the repairing "fsck -y" returned
    non-zero (an immediate second identical check of a clean volume adds nothing);
    the final fsck -n after unmount is kept;
  * result = delta of changed 1 MiB chunks (from the overlay allocation map,
    each candidate compared by SHA-256 against the mirror), not a second image.
Never mounts/contacts/writes a phone.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tarfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fastroot as fr
import update_metal_offline_vm as old          # read-only import: VM_SCRIPT text only
import replay_hfs_journal as jr                # read-only import: decode()/checksum()

OLD_FSCK = '''set +e
fsck.hfsplus -f -y /dev/vda
rc=$?
set -e
echo METAL_UPDATE_FSCK_RESULT=$rc
fsck.hfsplus -f -n /dev/vda
'''
NEW_FSCK = '''python3 - <<'JOURNAL'
import os, struct
d = open('/src/fastroot-journal.bin', 'rb').read()
magic, joff, hlen, n = struct.unpack_from('<8sQII', d)
assert magic == b'FRJRNL01'
pos = 24
header = d[pos:pos+hlen]; pos += hlen
writes = []
for _ in range(n):
    at, ln = struct.unpack_from('<QQ', d, pos); pos += 16
    writes.append((at, d[pos:pos+ln])); pos += ln
assert pos == len(d)
fd = os.open('/dev/vda', os.O_RDWR)
for at, data in writes:
    assert os.pwrite(fd, data, at) == len(data)
os.fsync(fd)
# Empty the journal only after every validated transaction reached the clone.
assert os.pwrite(fd, header, joff) == len(header)
os.fsync(fd)
assert os.pread(fd, len(header), joff) == header
os.close(fd)
print('METAL_JOURNAL_APPLIED', n)
JOURNAL
set +e
fsck.hfsplus -f -y /dev/vda
rc=$?
set -e
echo METAL_UPDATE_FSCK_RESULT=$rc
if [ "$rc" -ne 0 ]; then fsck.hfsplus -f -n /dev/vda; fi
'''


def vm_script():
    assert OLD_FSCK in old.VM_SCRIPT, 'original VM script changed; review before using the fast tool'
    # The fast tool runs the same guest script, so it also creates the shared pipeline-cache directory.
    assert '# METAL_CACHE_DIR_BEGIN' in old.VM_SCRIPT and 'METAL_CACHE_DIR_READY' in old.VM_SCRIPT
    return old.VM_SCRIPT.replace(OLD_FSCK, NEW_FSCK).replace('REHEARSE_VALUE', 'False')


def journal_payload(mirror, oracle, receipt):
    """Validate the mirror's journal exactly as replay_hfs_journal.py does; return the bytes the guest must apply."""
    with mirror.open('rb') as f:
        size = os.fstat(f.fileno()).st_size
        f.seek(1024); vh = f.read(512)
        if vh[:2] != b'HX':
            raise ValueError('Expected HFSX')
        attrs, _, jib = struct.unpack_from('>III', vh, 4); bs, blocks = struct.unpack_from('>II', vh, 40)
        if not attrs & 0x2000 or bs != 4096 or blocks * bs != size:
            raise ValueError('Unexpected volume geometry')
        f.seek(jib * bs); ji = f.read(180)
        off, n = struct.unpack_from('>QQ', ji, 36)
        if struct.unpack_from('>I', ji)[0] != 1 or n > 32 << 20 or off + n > size:
            raise ValueError('Unsupported journal location')
        f.seek(off); raw = f.read(n); assert len(raw) == n
    writes, header, txns = jr.decode(raw, size, off)
    expected = b''.join(struct.pack('<QQ', at, len(d)) + d for at, d in writes)
    out = receipt.with_suffix('.oracle.bin')
    subprocess.run([str(oracle.resolve()), str(mirror.resolve()), str(out)], check=True, timeout=60)
    if out.read_bytes() != expected:
        raise ValueError('Independent Apple replay disagrees (including transactions past journal end)')
    receipt.write_text(json.dumps({'image': str(mirror), 'journal_offset': off, 'journal_bytes': n,
                                   'journal_sha256': hashlib.sha256(raw).hexdigest(), 'transactions': txns,
                                   'writes': len(writes), 'write_bytes': sum(len(d) for _, d in writes),
                                   'independent_apple_oracle_matches': True, 'applied': True,
                                   'applied_in': 'vm-overlay'}, indent=2) + '\n')
    return struct.pack('<8sQII', b'FRJRNL01', off, len(header), len(writes)) + header + expected


def build_initrd(root, script, path):
    init = (root / 'init').read_bytes()
    ino = offset = 0
    with gzip.open(path, 'wb', compresslevel=1) as stream:
        def add(name, mode, data=b'', major=0, minor=0):
            nonlocal ino, offset
            ino += 1
            name = name.encode() + b'\0'
            fields = [ino, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, 0, len(data), 0, 0, major, minor, len(name), 0]
            head = b'070701' + b''.join(f'{x:08x}'.encode() for x in fields) + name
            stream.write(head); offset += len(head)
            pad = (-offset) % 4; stream.write(bytes(pad)); offset += pad
            stream.write(data); offset += len(data)
            pad = (-offset) % 4; stream.write(bytes(pad)); offset += pad
        for p in sorted(root.rglob('*')):
            rel = str(p.relative_to(root))
            data = (script if rel == 'build/run.sh' else init if rel == 'init' else
                    os.readlink(p).encode() if p.is_symlink() else p.read_bytes() if p.is_file() else b'')
            add(rel, p.lstat().st_mode, data)
        if not (root / 'dev').exists():
            add('dev', stat.S_IFDIR | 0o755)
        add('dev/console', stat.S_IFCHR | 0o600, major=5, minor=1)
        add('TRAILER!!!', 0)


def extract_delta(overlay, mirror, out, size):
    """Chunks that differ from the mirror, written to out/delta.bin; returns the delta table."""
    cand = fr.overlay_chunks(overlay, size)
    ofd, mfd = os.open(overlay, os.O_RDONLY), os.open(mirror, os.O_RDONLY)
    table, pos = [], 0
    try:
        with (out / 'delta.bin').open('wb') as df:
            for c in sorted(cand):
                after = bytes(fr.CHUNK) if cand[c] is None else os.pread(ofd, fr.CHUNK, cand[c])
                before = os.pread(mfd, fr.CHUNK, c * fr.CHUNK)
                assert len(after) == fr.CHUNK and len(before) == fr.CHUNK
                if after != before:
                    df.write(after)
                    table.append({'chunk': c, 'before': fr.sha(before), 'after': fr.sha(after), 'pos': pos})
                    pos += 1
            df.flush(); os.fsync(df.fileno())
    finally:
        os.close(ofd); os.close(mfd)
    return table, len(cand)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--main', type=Path, required=True)
    ap.add_argument('--snapshot-receipt', type=Path, required=True, help='verified.json written by snapshot_current_root_fast.py')
    ap.add_argument('--addon', type=Path, required=True)
    ap.add_argument('--addon-sha256', required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--journal-oracle', type=Path, required=True)
    ap.add_argument('--vm-kernel', type=Path, default=Path('/usr/lib/modules/7.2.9-arch1-1/vmlinuz'))
    ap.add_argument('--audit-compare', action='store_true',
                    help='extra: qemu-img compare mirror vs overlay (reads both fully) to audit the allocation-map argument')
    ap.add_argument('--verify-mirror', action='store_true', help='re-hash the whole mirror (parallel) against the receipt first')
    a = ap.parse_args()
    t0 = time.monotonic()
    if not a.vm_kernel.is_file():
        ap.error('matching VM kernel is missing; use --vm-kernel')
    snap = json.loads(a.snapshot_receipt.read_text())
    assert snap['format'] == fr.FORMAT and snap['per_chunk_manifest_matches'] and snap['phone_partition_writes'] == 0
    mirror = Path(snap['mirror'])
    size = snap['bytes']
    side = json.loads(Path(str(mirror) + '.manifest.json').read_text())
    s = mirror.stat()
    assert side['clean'] and side['root'] == snap['root'] and fr.root_of(side['hashes']) == snap['root']
    assert side['signature'] == {'size': s.st_size, 'mtime_ns': s.st_mtime_ns, 'ino': s.st_ino}, 'mirror changed since snapshot'
    if a.verify_mirror:
        got = fr.hash_file_chunks(mirror, fr.BIG, range(size // fr.BIG))
        assert [got[i] for i in range(size // fr.BIG)] == side['hashes'], 'mirror content differs from manifest'
    assert hashlib.file_digest(a.addon.open('rb'), 'sha256').hexdigest() == a.addon_sha256, 'addon hash mismatch'
    a.output.mkdir(exist_ok=False)
    payload = (a.output / 'inputs').resolve(); payload.mkdir()
    with tarfile.open(a.addon) as tf:
        tf.extractall(payload, filter='data')
    assert not (payload / 'fastroot-journal.bin').exists()
    (payload / 'fastroot-journal.bin').write_bytes(journal_payload(mirror, a.journal_oracle, a.output / 'journal-replay.json'))
    overlay = (a.output / 'overlay.qcow2').resolve()
    subprocess.run(['qemu-img', 'create', '-q', '-f', 'qcow2', '-b', str(mirror.resolve()), '-F', 'raw',
                    '-o', 'cluster_size=1M,lazy_refcounts=on', str(overlay)], check=True)
    initrd = a.output / 'metal-initrd.gz'
    build_initrd(a.main / 'research/nord2/rootfs-vm/initramfs', vm_script().encode(), initrd)
    command = ['qemu-system-x86_64', '-enable-kvm', '-cpu', 'host', '-m', '2048', '-smp', '2',
               '-nodefaults', '-no-reboot', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
               '-kernel', str(a.vm_kernel.resolve()), '-initrd', str(initrd),
               '-append', 'console=ttyS0 rdinit=/init panic=-1 loglevel=4',
               '-drive', f'file={overlay},format=qcow2,if=virtio,cache=unsafe',
               '-virtfs', f'local,path={payload},mount_tag=inputs,security_model=none,readonly=on']
    (a.output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    t = time.monotonic()
    with (a.output / 'vm.log').open('wb') as out:
        subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, check=True, timeout=1200)
    vm_s = time.monotonic() - t
    log = (a.output / 'vm.log').read_text(); print(log[-3000:])
    assert 'ROOTFS_VM_COMPLETE' in log and 'ROOTFS_VM_FAILED' not in log and 'METAL_UPDATE_READY' in log
    assert 'METAL_JOURNAL_APPLIED' in log and 'METAL_UPDATE_VERIFIED' in log and 'METAL_CACHE_DIR_READY' in log
    assert 'appears to be OK' in log.rsplit('METAL_UPDATE_VERIFIED', 1)[1], 'final fsck is not clean'
    s2 = mirror.stat()
    assert side['signature'] == {'size': s2.st_size, 'mtime_ns': s2.st_mtime_ns, 'ino': s2.st_ino}, 'mirror mutated'
    table, allocated = extract_delta(overlay, mirror, a.output, size)
    if a.audit_compare:
        subprocess.run(['qemu-img', 'compare', '-f', 'raw', '-F', 'qcow2', str(mirror), str(overlay)],
                       check=False, stdout=subprocess.DEVNULL)  # exit 1 expected: images differ; audit lives in the tests
    delta = {t_['chunk']: t_['after'] for t_ in table}
    result = {'format': fr.FORMAT, 'kind': 'offline-update', 'before_root': snap['root'], 'mirror': str(mirror),
              'after_id': fr.delta_id(snap['root'], delta), 'addon_sha256': a.addon_sha256,
              'bytes': size, 'chunk_bytes': fr.CHUNK, 'overlay_allocated_chunks': allocated,
              'changed_chunks': len(table), 'delta': table, 'offline_verified': True, 'rehearsal': False,
              'fsck_clean_after': True, 'timings': {'vm_s': round(vm_s, 1), 'total_s': round(time.monotonic() - t0, 1)}}
    fr.write_json_atomic(a.output / 'verified.json', result)
    print(json.dumps({k: v for k, v in result.items() if k != 'delta'}, indent=2))


if __name__ == '__main__':
    main()
