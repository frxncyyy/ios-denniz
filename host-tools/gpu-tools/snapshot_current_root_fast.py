#!/usr/bin/env python3
"""Fast read-only RAM-rescue snapshot into a persistent, warm mirror image.

Never writes a phone partition (only dd/sha256sum/gzip read the block device;
partition write counters are compared before and after to prove it). Equivalent
to snapshot_current_root.py, but:
  * device hashes 16 MiB chunks in parallel shells (--jobs);
  * local mirror hashing runs concurrently with the device pass (6 threads);
  * changed 16 MiB chunks are refined to 1 MiB pieces; only differing pieces
    are fetched, gzip-compressed on the device, 3 streams in parallel;
  * the independent whole-device hash is replaced by the per-chunk proof below.

Equivalence argument: every 16 MiB chunk i of the mirror is hashed locally AFTER
the refresh and compared with the digest the device computed for chunk i in
the same session; all 1024 chunks are covered, so mirror == device chunk-wise
(SHA-256 collision resistance). The only thing a whole-device hash adds is the
absence of concurrent modification, which is proven here instead by the
userdata write/discard counters and boot id being identical before the pass
and after the last read.

storage-20261114 (grown root volume, hfs_grow.py): the volume size comes from the
device volume header (16 GiB or grown) and the mirror is a SPARSE file of that
size (resized automatically). The first 16 GiB are verified exactly as above;
past them only the 16 MiB chunks that hold an allocated block (allocation bitmap
after replaying the journal, plus the alternate VH chunk and every journal
target) are compared/fetched. The other chunks are HFS free space: listed as
'unverified_chunks' in the receipt and the sidecar, never claimed equal to the
device (the device keeps old Android data there), and commit_root_delta_fast.py
treats their device preimage as "any content, unchanged since the snapshot".
"""

import argparse
import concurrent.futures as cf
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fastroot as fr
import replay_hfs_journal as jr


def load_main_preflight(main):
    spec = importlib.util.spec_from_file_location('preflight', main / 'tools/nord2-install-root.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def runs_of(indices, max_len):
    """Merge sorted piece indices into (start, count) runs of at most max_len."""
    out = []
    for i in sorted(indices):
        if out and out[-1][0] + out[-1][1] == i and out[-1][1] < max_len:
            out[-1][1] += 1
        else:
            out.append([i, 1])
    return [tuple(r) for r in out]


def fetch_run(adb, p, start, count, compress):
    cmd = f'dd if={p["dev"]} bs=1048576 skip={start} count={count} status=none' + (' | gzip -1' if compress else '')
    proc = adb.stream(cmd)
    raw, err = proc.communicate()
    if proc.returncode:
        raise fr.DeviceError(f'fetch failed rc={proc.returncode} {err[-200:]!r}')
    if compress:
        d = zlib.decompressobj(31)
        raw = d.decompress(raw) + d.flush()
        if not d.eof:
            raise fr.DeviceError('truncated gzip stream')
    if len(raw) != count * fr.MIB:
        raise fr.DeviceError('short fetch')
    return start, raw


def hfs_layout(read, size):
    """Volume header facts the partial (grown-volume) snapshot needs; read(off, n) reads the VERIFIED mirror."""
    vh = read(1024, 512)
    if vh[:2] not in (b'H+', b'HX'):
        raise fr.DeviceError('not an HFS+/HFSX volume header')
    attrs, jib = struct.unpack_from('>I', vh, 4)[0], struct.unpack_from('>I', vh, 12)[0]
    bs, total = struct.unpack_from('>II', vh, 40)
    if bs != 4096 or bs * total != size:
        raise fr.DeviceError(f'volume header geometry {bs}x{total} != snapshot size {size}')
    alloc_blocks = struct.unpack_from('>I', vh, 112 + 12)[0]
    ext = [struct.unpack_from('>II', vh, 112 + 16 + 8 * k) for k in range(8)]
    if sum(c for _, c in ext) != alloc_blocks:
        raise fr.DeviceError('allocation file has extents-overflow records: partial snapshot unsupported (review)')
    if alloc_blocks * bs * 8 < total:
        raise fr.DeviceError('allocation file does not cover the volume')
    return dict(attrs=attrs, jib=jib, total=total, alloc_ext=[e for e in ext if e[1]])


def allocated_big_chunks(read, size, lay):
    """16 MiB chunk indices holding >= 1 allocated block, per the bitmap AFTER replaying the journal
    (a dirty journal may hold bitmap updates not yet on disk), plus every chunk a journal transaction
    targets. read() reads the verified mirror; the bitmap and the journal must already be verified."""
    bs = 4096
    bitmap = bytearray()
    for start, count in lay['alloc_ext']:
        bitmap += read(start * bs, count * bs)
    targets = []
    if lay['attrs'] & 0x2000:
        ji = read(lay['jib'] * bs, 52)
        joff, jlen = struct.unpack_from('>QQ', ji, 36)
        if struct.unpack_from('>I', ji)[0] & 0x2 or joff + jlen > size:
            raise fr.DeviceError('unsupported journal location')
        writes, _, _ = jr.decode(read(joff, jlen), size, joff)
        for dest, data in writes:
            targets.append((dest, len(data)))
        # apply journal writes that land in bitmap blocks
        pos = 0
        for st, cnt in lay['alloc_ext']:
            a0, a1 = st * bs, (st + cnt) * bs
            for dest, data in writes:
                lo, hi = max(a0, dest), min(a1, dest + len(data))
                if lo < hi:
                    bitmap[pos + lo - a0:pos + hi - a0] = data[lo - dest:hi - dest]
            pos += cnt * bs
    per = fr.BIG // bs // 8                      # bitmap bytes per 16 MiB chunk (512)
    n_big = size // fr.BIG
    out = {c for c in range(n_big) if any(bitmap[c * per:(c + 1) * per])}
    for dest, n in targets:
        out.update(range(dest // fr.BIG, (dest + n - 1) // fr.BIG + 1))
    return out


def snapshot(adb, p, preflight, mirror, outdir, size, jobs=4, local_jobs=6, fetch_jobs=3,
             compress=True, trust_sidecar=False, seed=None, log=print, always_bytes=fr.OLD_ROOT_BYTES):
    """size = HFS volume bytes. Up to always_bytes every 16 MiB chunk is device-verified (the whole volume when it
    is not grown: identical to the pre-grow tool). A grown volume (storage-20261114) is verified in rounds:
    1) chunks [0, always_bytes) + the last chunk (alternate VH); 2) the chunks holding the allocation file;
    3) every chunk with an allocated block (bitmap after journal replay) or a journal target. The remaining
    chunks are FS free space: reported in 'unverified_chunks' (receipt + sidecar), never compared or fetched."""
    t0 = time.monotonic()
    timings = {}
    n_big = size // fr.BIG
    if size % fr.BIG:
        raise fr.DeviceError('volume size is not a multiple of 16 MiB')
    side = Path(str(mirror) + '.manifest.json')
    outdir.mkdir(parents=True, exist_ok=True)
    if not mirror.exists():
        if not seed:
            raise fr.DeviceError('mirror missing; pass --seed once')
        log('One-time mirror creation from seed')
        subprocess.run(['cp', '--sparse=always', '--', str(seed), str(mirror)], check=True)
        mirror.chmod(0o600)
    if mirror.is_symlink():
        raise fr.DeviceError('mirror is a symlink')
    resized = None
    if mirror.stat().st_size != size:
        # Grown (or un-grown) volume: resize the SPARSE mirror to the volume size. Nothing is trusted from the
        # old sidecar: it is marked dirty first and every needed chunk is re-verified below.
        old_size = mirror.stat().st_size
        if size < always_bytes or old_size < always_bytes:
            raise fr.DeviceError(f'mirror {old_size} / volume {size}: refusing to resize below {always_bytes} bytes')
        if side.exists():
            fr.write_json_atomic(side, dict(json.loads(side.read_text()), clean=False))
        mirror.chmod(0o600)
        os.truncate(mirror, size)
        resized = [old_size, size]
        log(f'Mirror resized (sparse) {old_size} -> {size} bytes')
    mirror.chmod(0o600)
    pre = preflight()
    epoch0 = fr.device_epoch(adb, p)
    if compress:
        compress = b'GZIP' in adb.shell('command -v gzip >/dev/null && echo GZIP')
    n_always = min(n_big, always_bytes // fr.BIG)

    def local_hashes():
        s = mirror.stat()
        sig = {'size': s.st_size, 'mtime_ns': s.st_mtime_ns, 'ino': s.st_ino}
        if trust_sidecar and side.exists():
            m = json.loads(side.read_text())
            if m.get('clean') and m.get('signature') == sig and len(m['hashes']) == n_big:
                return m['hashes'], True
        return [h for _, h in sorted(fr.hash_file_chunks(mirror, fr.BIG, range(n_big), local_jobs).items())], False

    dev = {}
    bad_all, pieces_all = [], []
    downloaded = 0

    def refresh(chunks, local, known=None):
        """Device-hash `chunks` (unless `known`), fetch the differing 1 MiB pieces into the mirror, re-verify."""
        nonlocal downloaded
        chunks = sorted(set(chunks) - set(dev))
        if not chunks:
            return
        got = dict(known) if known is not None else fr.device_hashes(adb, p, chunks, 16, jobs)
        dev.update(got)
        bad = [i for i in chunks if local[i] != got[i]]
        if not bad:
            return
        bad_all.extend(bad)
        fr.write_json_atomic(side, {'format': fr.FORMAT, 'clean': False, 'hashes': local})
        per = fr.BIG // fr.MIB
        idx = [i * per + k for i in bad for k in range(per)]
        dev_small = fr.device_hashes(adb, p, idx, 1, jobs)
        loc_small = fr.hash_file_chunks(mirror, fr.MIB, idx, local_jobs)
        pieces = [i for i in idx if dev_small[i] != loc_small[i]]
        pieces_all.extend(pieces)
        log(f'{len(pieces)} MiB differ inside {len(bad)} chunks; fetching {"gzip " if compress else ""}streams')
        fd = os.open(mirror, os.O_RDWR)
        try:
            runs = runs_of(pieces, 16)
            with cf.ThreadPoolExecutor(fetch_jobs) as ex:
                for start, raw in ex.map(lambda r: fetch_run(adb, p, r[0], r[1], compress), runs):
                    for k in range(len(raw) // fr.MIB):
                        part = raw[k * fr.MIB:(k + 1) * fr.MIB]
                        if fr.sha(part) != dev_small[start + k]:
                            raise fr.DeviceError('Device changed or corrupt transfer; mirror piece rejected')
                    os.pwrite(fd, raw, start * fr.MIB)
                    downloaded += len(raw)
                    (outdir / 'snapshot-progress.json').write_text(json.dumps({'downloaded_bytes': downloaded}) + '\n')
            os.fsync(fd)
        finally:
            os.close(fd)
        again = fr.hash_file_chunks(mirror, fr.BIG, bad, local_jobs)
        if any(again[i] != got[i] for i in bad):
            raise fr.DeviceError('Refreshed chunk does not match device digest')
        for i in bad:
            local[i] = got[i]

    t = time.monotonic()
    first = list(range(n_always)) + ([n_big - 1] if n_always < n_big else [])
    with cf.ThreadPoolExecutor(1) as ex:
        fut = ex.submit(local_hashes)
        log(f'Hashing device in {len(first)} x16 MiB chunks, {jobs} parallel shells (volume {n_big} chunks)')
        got = fr.device_hashes(adb, p, first, 16, jobs)
        t_dev = time.monotonic() - t
        local, trusted = fut.result()
    local = list(local)
    timings['device_hash_s'] = round(t_dev, 1)
    timings['device_plus_local_hash_wall_s'] = round(time.monotonic() - t, 1)
    # Round 1 (the whole volume when not grown): same algorithm as the pre-grow tool.
    dev_first = got
    bad1 = [i for i in first if local[i] != dev_first[i]]
    log(f'{len(bad1)} of {len(first)} chunks differ')
    t = time.monotonic()
    refresh(first, local, known=dev_first)
    rounds = {'round1_chunks': len(first)}
    if n_always < n_big:
        def mread(off, n):
            with mirror.open('rb') as f:
                f.seek(off)
                b = f.read(n)
            if len(b) != n:
                raise fr.DeviceError('short mirror read')
            return b
        lay = hfs_layout(mread, size)
        r2 = {c for st, cnt in lay['alloc_ext'] for c in range(st * 4096 // fr.BIG, ((st + cnt) * 4096 - 1) // fr.BIG + 1)}
        rounds['round2_chunks'] = len(r2 - set(dev))
        refresh(r2, local)
        alloc = allocated_big_chunks(mread, size, lay)
        r3 = alloc - set(dev)
        rounds['round3_chunks'] = len(r3)
        refresh(r3, local)
        log(f'Grown volume: {len(dev)} of {n_big} chunks verified, {n_big - len(dev)} free chunks not compared')
    if bad_all:
        timings['refresh_s'] = round(time.monotonic() - t, 1)
    unverified = [i for i in range(n_big) if i not in dev]
    (outdir / 'device-chunks.txt').write_text(''.join(f'{i} {dev[i]}\n' for i in sorted(dev)))
    pre2 = preflight()
    epoch1 = fr.device_epoch(adb, p)
    if epoch1 != epoch0:
        raise fr.DeviceError('Userdata write counters or boot id changed during the snapshot')
    s = mirror.stat()
    sig = {'size': s.st_size, 'mtime_ns': s.st_mtime_ns, 'ino': s.st_ino}
    # verified chunks: the device digest; unverified (free) chunks: the local mirror digest (sidecar stays
    # consistent with the mirror file; those chunks are never claimed equal to the device).
    hashes = [dev[i] if i in dev else local[i] for i in range(n_big)]
    root = fr.root_of(hashes)
    sc = {'format': fr.FORMAT, 'clean': True, 'signature': sig, 'hashes': hashes, 'root': root}
    if unverified:
        sc['unverified_chunks'] = unverified
    fr.write_json_atomic(side, sc)
    mirror.chmod(0o444)
    (outdir / 'preflight.json').write_text(json.dumps(pre2, indent=2) + '\n')
    timings['total_s'] = round(time.monotonic() - t0, 1)
    result = {'format': fr.FORMAT, 'kind': 'snapshot', 'mirror': str(mirror), 'bytes': size,
              'chunk_bytes': fr.BIG, 'root': root, 'per_chunk_manifest_matches': True,
              'independent_full_hash_replaced_by': 'per-16MiB-chunk device digests + userdata write-counter epoch',
              'epoch': epoch1, 'local_hash_skipped_by_sidecar': trusted,
              'changed_big_chunks': sorted(bad_all), 'changed_mib': len(pieces_all), 'downloaded_bytes': downloaded,
              'compressed': bool(compress), 'timings': timings, 'phone_partition_writes': 0}
    if n_always < n_big:
        result.update(volume_grown=True, verified_chunks=len(dev), unverified_chunks=unverified,
                      unverified_reason='HFS free space (no allocated block in the bitmap after journal replay, no journal target)',
                      rounds=rounds)
    if resized:
        result['mirror_resized'] = resized
    fr.write_json_atomic(outdir / 'verified.json', result)
    return result


def bench(adb, p, jobs_list=(1, 2, 4, 8), chunks=16):
    """Read-only on-device hash throughput probe on disjoint regions (picks --jobs)."""
    pos, out = 0, []
    for j in jobs_list:
        n = chunks * j
        t = time.monotonic()
        fr.device_hashes(adb, p, range(pos, pos + n), 16, j)
        dt = time.monotonic() - t
        out.append((j, round(n * 16 / dt, 1)))
        pos += n
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--main', type=Path, required=True)
    ap.add_argument('--mirror', type=Path, required=True, help='persistent warm raw image (sidecar: <mirror>.manifest.json)')
    ap.add_argument('--seed', type=Path, help='only needed once, to create the mirror')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--adb', default='$HOME/Android/Sdk/platform-tools/adb')
    ap.add_argument('--jobs', type=int, default=4, help='parallel device hash shells (see --bench)')
    ap.add_argument('--no-compress', action='store_true')
    ap.add_argument('--trust-sidecar', action='store_true',
                    help='skip local re-hash when the mirror stat signature matches a clean sidecar')
    ap.add_argument('--bench', action='store_true', help='print on-device hash MiB/s for 1/2/4/8 jobs and exit (read-only)')
    a = ap.parse_args()
    mod = load_main_preflight(a.main)
    adb = fr.Adb.single(a.adb)
    p = dict(fr.REAL_PROFILE, dev=mod.BY_NAME)
    pre = lambda: mod.preflight(a.adb, adb.serial)
    pre_info = pre()
    if a.bench:
        for j, mbps in bench(adb, p):
            print(f'jobs={j}: {mbps} MiB/s')
        return
    header = adb.shell(f'dd if={mod.BY_NAME} bs=4096 count=1 status=none')
    assert len(header) == 4096 and header[1024:1026] == b'HX'
    block, count = struct.unpack_from('>II', header, 1024 + 40)
    size = block * count
    # storage-20261114: 16 GiB (original) or a grown volume (hfs_grow.py: multiple of 128 MiB, ends before the
    # SWEETLOG tail reserve of the partition).
    part = pre_info['partition_bytes']
    assert block == 4096 and (size == 16 << 30 or (size > 16 << 30 and size % (128 << 20) == 0
                                                   and size <= part - (1025 + 1) * 4096)), (block, count, part)
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'volume-header.bin').write_bytes(header)
    r = snapshot(adb, p, pre, a.mirror, a.output, size, jobs=a.jobs, compress=not a.no_compress,
                 trust_sidecar=a.trust_sidecar, seed=a.seed)
    print(json.dumps({k: v for k, v in r.items() if k != 'changed_big_chunks'}, indent=2))


if __name__ == '__main__':
    main()
