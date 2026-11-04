#!/usr/bin/env python3
"""Shared pieces of the fast root-transaction tools (new code; nothing here
touches snapshot_current_root.py, update_metal_offline_vm.py or
commit_root_delta.py).

Model
  * A "mirror" is a raw image of the device userdata (16 GiB) plus a sidecar
    manifest of SHA-256 per 16 MiB chunk, every one of which was checked against a
    hash computed *on the device* in the same rescue session.
  * The offline update runs on a qcow2 overlay whose backing file is the mirror,
    so no 16 GiB copy exists and the overlay allocation map is the exact list of
    chunks the update could have changed.
  * The install writes only the 1 MiB chunks whose content really differs.
"""
import concurrent.futures as cf
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

MIB = 1 << 20
CHUNK = MIB                 # install granularity (unchanged from commit_root_delta.py)
BIG = 16 * MIB              # snapshot hash granularity (unchanged from snapshot_current_root.py)
FORMAT = 'nord2-fastroot-1'
OLD_ROOT_BYTES = 16 << 30   # size of the original iOS root volume: below it every chunk is always device-verified
# storage-20261114: a grown root volume (hfs_grow.py) is mirrored as a SPARSE file of the volume size.  Above
# OLD_ROOT_BYTES only the 16 MiB chunks that hold allocated HFS blocks (bitmap after journal replay) are compared
# with the device; the others are listed as 'unverified_chunks' in the receipt and the sidecar (FS free space:
# their content means nothing to HFS, the mirror usually holds zeros/holes there, the device old Android data).
HEX = re.compile('[0-9a-f]{64}')

REAL_PROFILE = dict(
    dev='/dev/block/by-name/userdata',
    real='/dev/block/sdc57',
    stat='/sys/class/block/sdc57/stat',
    work='/tmp/nord2fast',
    marker='/nord2-rescue',
    marker_text='ADB RAM recovery; userdata is not mounted')


class DeviceError(RuntimeError):
    pass


def sha(data):
    return hashlib.sha256(data).hexdigest()


def root_of(hexes):
    """Identity of a whole image = SHA-256 over its ordered chunk digests."""
    return hashlib.sha256(b''.join(bytes.fromhex(h) for h in hexes)).hexdigest()


def delta_id(before_root, delta):
    """Identity of mirror+delta: SHA-256 over before_root and the sorted (index, after-digest) pairs."""
    h = hashlib.sha256(before_root.encode())
    for i, d in sorted(delta.items()):
        h.update(b'|%d:%s' % (i, d.encode()))
    return h.hexdigest()


def write_json_atomic(path, obj):
    tmp = Path(str(path) + '.tmp')
    tmp.write_text(json.dumps(obj, indent=2) + '\n')
    with tmp.open('rb') as f:
        os.fsync(f.fileno())
    os.replace(tmp, path)


# ---------------------------------------------------------------- local hashing
_ZERO_DIGEST = {}


def zero_digest(unit):
    if unit not in _ZERO_DIGEST:
        _ZERO_DIGEST[unit] = hashlib.sha256(bytes(unit)).hexdigest()
    return _ZERO_DIGEST[unit]


def hash_file_chunks(path, unit, indices, jobs=6, total=None):
    """SHA-256 of each unit-sized chunk, threaded (hashlib and pread drop the GIL)."""
    fd = os.open(path, os.O_RDONLY)
    try:
        zero = bytes(4 * MIB)
        fsize = os.fstat(fd).st_size

        def one(i):
            h = hashlib.sha256()
            off, end = i * unit, (i + 1) * unit
            if end > fsize:
                raise DeviceError(f'short local read at chunk {i}')
            while off < end:
                # storage-20261114: holes of a sparse mirror read as zeros; hash them without reading (same digest).
                try:
                    data_at = os.lseek(fd, off, os.SEEK_DATA)
                except OSError:          # ENXIO: only a hole up to EOF
                    data_at = end
                if data_at >= end and off == i * unit:
                    return i, zero_digest(unit)
                if data_at > off:
                    k = min(data_at, end) - off
                    while k:
                        n = min(k, len(zero))
                        h.update(zero[:n]); off += n; k -= n
                    continue
                b = os.pread(fd, min(4 * MIB, end - off), off)
                if not b:
                    raise DeviceError(f'short local read at chunk {i}')
                h.update(b)
                off += len(b)
            return i, h.hexdigest()
        with cf.ThreadPoolExecutor(jobs) as ex:
            return dict(ex.map(one, indices))
    finally:
        os.close(fd)


# ---------------------------------------------------------------- adb
class Adb:
    def __init__(self, adb, serial):
        self.adb, self.serial = adb, serial

    @classmethod
    def single(cls, adb):
        devices = subprocess.check_output([adb, 'devices'], text=True).splitlines()[1:]
        live = [x.split()[0] for x in devices if len(x.split()) == 2 and x.split()[1] in ('device', 'recovery')]
        if len(live) != 1:
            raise DeviceError('Exactly one authorized rescue device required')
        return cls(adb, live[0])

    def shell(self, command, data=None, timeout=600):
        p = subprocess.run([self.adb, '-s', self.serial, 'shell', '-T', command], input=data,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        if p.returncode:
            raise DeviceError(f'device command failed rc={p.returncode}: {p.stderr[-300:]!r} {p.stdout[-300:]!r}')
        return p.stdout

    def stream(self, command):
        return subprocess.Popen([self.adb, '-s', self.serial, 'shell', '-T', command],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)


class ShellAdb:
    """Test double: runs the very same shell text locally (see test_fastroot.py)."""
    def __init__(self, env=None, cwd=None):
        self.env = dict(os.environ, **(env or {}))
        self.cwd = cwd

    def shell(self, command, data=None, timeout=600):
        p = subprocess.run(['/bin/bash', '-c', command], input=data, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=timeout, env=self.env, cwd=self.cwd)
        if p.returncode:
            raise DeviceError(f'device command failed rc={p.returncode}: {p.stderr[-300:]!r} {p.stdout[-300:]!r}')
        return p.stdout

    def stream(self, command):
        return subprocess.Popen(['/bin/bash', '-c', command], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=self.env, cwd=self.cwd)


# ---------------------------------------------------------------- device scripts
def guard(p):
    if 'guard' in p:
        return p['guard']
    return (f'test -b {p["dev"]} && test "$(readlink -f {p["dev"]})" = {p["real"]} && '
            f'test "$(cat {p["marker"]})" = "{p["marker_text"]}"')


def script_hash(p, indices, unit_mib, jobs):
    """Print 'index sha256' for every unit-sized chunk, `jobs` shells in parallel.

    Only dd/sha256sum/wait of the rescue toybox+mksh are used. Indices are dealt
    out in contiguous runs so each job reads its region sequentially.
    """
    indices = list(indices)
    per = max(1, -(-len(indices) // jobs))
    parts = [indices[k:k + per] for k in range(0, len(indices), per)]
    w = p['work']
    lines = [f'mkdir -p {w} && rm -f {w}/h.*']
    for n, part in enumerate(parts):
        lines.append(f'( for i in {" ".join(map(str, part))}; do '
                     f'set -- $(dd if={p["dev"]} bs=1048576 skip=$((i*{unit_mib})) count={unit_mib} status=none | sha256sum); '
                     f'echo "$i $1"; done > {w}/h.{n} ) &')
    lines += ['wait', f'cat {w}/h.*', f'rm -f {w}/h.*']
    return '\n'.join(lines)


def parse_hashes(out, indices):
    got = {}
    for line in out.decode().splitlines():
        f = line.split()
        if len(f) == 2 and f[0].isdigit() and HEX.fullmatch(f[1]):
            got[int(f[0])] = f[1]
    missing = [i for i in indices if i not in got]
    if missing:
        raise DeviceError(f'device returned no/invalid hash for {len(missing)} chunks, first {missing[:5]}')
    return {i: got[i] for i in indices}


def device_hashes(adb, p, indices, unit_mib, jobs, timeout=1800):
    indices = list(indices)
    if not indices:
        return {}
    # The index list is pasted into the adb shell command line; the rescue adbd closes the connection
    # ("error: closed") for very long command lines (3000+ indices ~15 KB), so ask in batches.
    batch = 300
    result = {}
    for k in range(0, len(indices), batch):
        part = indices[k:k + batch]
        result.update(parse_hashes(adb.shell(script_hash(p, part, unit_mib, jobs), timeout=timeout), part))
    return result


def script_epoch(p):
    return f'cat /proc/sys/kernel/random/boot_id; cat {p["stat"]}'


def device_epoch(adb, p):
    """Write/discard counters of the userdata partition plus boot id.

    Equal epochs prove no I/O that modifies the partition happened in between
    (every write path to the partition, including via the whole-disk node, is
    accounted in this block-layer counter).
    """
    lines = adb.shell(script_epoch(p)).decode().splitlines()
    boot, st = lines[0].strip(), lines[1].split()
    if len(st) < 7:
        raise DeviceError('unexpected block stat format')
    fields = [st[4], st[5], st[6]] + (st[11:14] if len(st) >= 14 else [])
    return {'boot_id': boot, 'write_discard_counters': fields}


def script_apply(p, entries, offsets):
    """One adb shell: per chunk re-verify preimage, write, fsync; then flush caches and read back.

    entries: list of (chunk_index, before_hex, after_hex). offsets[k] = position of
    entry k in the staged payload file ({work}/batch.bin), in chunks.
    """
    w, dev = p['work'], p['dev']
    g = guard(p)
    L = ['PAY=%s/batch.bin' % w,
         'H() { set -- $(dd if="$1" bs=1048576 skip="$2" count=1 status=none | sha256sum); echo "$1"; }',
         f'{g} || {{ echo FAIL guard; exit 2; }}']
    for (i, b, a), k in zip(entries, offsets):
        L += [f'p=$(H $PAY {k}); [ "$p" = {a} ] || {{ echo "FAIL payload {i} $p"; exit 3; }}',
              f'd=$(H {dev} {i})',
              f'if [ "$d" = {b} ]; then',
              f'  {g} || {{ echo FAIL guard; exit 2; }}',
              f'  dd if=$PAY of={dev} bs=1048576 skip={k} seek={i} count=1 conv=notrunc,fsync status=none || {{ echo "FAIL write {i}"; exit 5; }}',
              f'  echo "W {i}"',
              f'elif [ "$d" = {a} ]; then echo "D {i}"',
              f'else echo "FAIL preimage {i} $d"; exit 4; fi']
    L += ['sync', f'blockdev --flushbufs {dev} 2>/dev/null', 'echo 3 > /proc/sys/vm/drop_caches 2>/dev/null']
    for i, b, a in entries:
        L += [f'r=$(H {dev} {i}); [ "$r" = {a} ] || {{ echo "FAIL readback {i} $r"; exit 6; }}', f'echo "V {i}"']
    L += [f'echo BATCH_OK {len(entries)}', f'rm -f $PAY']
    return '\n'.join(L)


def parse_apply(out, entries):
    text = out.decode()
    lines = text.splitlines()
    if any(l.startswith('FAIL') for l in lines) or f'BATCH_OK {len(entries)}' not in lines:
        raise DeviceError('batch failed: ' + ' | '.join(lines[-5:]))
    verified = {int(l.split()[1]) for l in lines if l.startswith('V ')}
    if verified != {i for i, _, _ in entries}:
        raise DeviceError('batch did not read back every chunk')
    return verified


# ---------------------------------------------------------------- qcow2 overlay
def overlay_chunks(overlay, backing_size, chunk=CHUNK):
    """Return {chunk_index: file offset in overlay or None(zero)} for chunks allocated in the overlay.

    Everything not listed is, by qcow2 semantics, read from the backing mirror.
    """
    out = json.loads(subprocess.check_output(['qemu-img', 'map', '--output=json', '-U', '-f', 'qcow2', str(overlay)]))
    chunks = {}
    for e in out:
        if e.get('depth') != 0 or not (e.get('data') or e.get('zero')):
            continue
        if e.get('data') and e.get('compressed'):
            raise DeviceError('compressed overlay cluster unsupported')
        start, length = e['start'], e['length']
        if start % chunk or length % chunk:
            raise DeviceError('overlay not aligned to chunk; create it with cluster_size=1M')
        for c in range(start // chunk, (start + length) // chunk):
            chunks[c] = (e['offset'] + (c * chunk - start)) if e.get('data') and not e.get('zero') else None
    if chunks and max(chunks) * chunk >= backing_size:
        raise DeviceError('overlay chunk beyond device size')
    return chunks
