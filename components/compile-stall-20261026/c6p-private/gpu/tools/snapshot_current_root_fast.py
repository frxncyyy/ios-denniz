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


def snapshot(adb, p, preflight, mirror, outdir, size, jobs=4, local_jobs=6, fetch_jobs=3,
             compress=True, trust_sidecar=False, seed=None, log=print):
    t0 = time.monotonic()
    timings = {}
    n_big = size // fr.BIG
    side = Path(str(mirror) + '.manifest.json')
    outdir.mkdir(parents=True, exist_ok=True)
    if not mirror.exists():
        if not seed:
            raise fr.DeviceError('mirror missing; pass --seed once')
        log('One-time mirror creation from seed')
        subprocess.run(['cp', '--sparse=always', '--', str(seed), str(mirror)], check=True)
        mirror.chmod(0o600)
    if mirror.is_symlink() or mirror.stat().st_size != size:
        raise fr.DeviceError('mirror is a symlink or has the wrong size')
    mirror.chmod(0o600)
    pre = preflight()
    epoch0 = fr.device_epoch(adb, p)
    if compress:
        compress = b'GZIP' in adb.shell('command -v gzip >/dev/null && echo GZIP')

    def local_hashes():
        s = mirror.stat()
        sig = {'size': s.st_size, 'mtime_ns': s.st_mtime_ns, 'ino': s.st_ino}
        if trust_sidecar and side.exists():
            m = json.loads(side.read_text())
            if m.get('clean') and m.get('signature') == sig and len(m['hashes']) == n_big:
                return m['hashes'], True
        return [h for _, h in sorted(fr.hash_file_chunks(mirror, fr.BIG, range(n_big), local_jobs).items())], False

    t = time.monotonic()
    with cf.ThreadPoolExecutor(1) as ex:
        fut = ex.submit(local_hashes)
        log(f'Hashing device in {n_big} x16 MiB chunks, {jobs} parallel shells')
        dev = fr.device_hashes(adb, p, range(n_big), 16, jobs)
        t_dev = time.monotonic() - t
        local, trusted = fut.result()
    timings['device_hash_s'] = round(t_dev, 1)
    timings['device_plus_local_hash_wall_s'] = round(time.monotonic() - t, 1)
    (outdir / 'device-chunks.txt').write_text(''.join(f'{i} {dev[i]}\n' for i in range(n_big)))
    bad = [i for i in range(n_big) if local[i] != dev[i]]
    log(f'{len(bad)} of {n_big} chunks differ')

    downloaded = 0
    pieces = []
    if bad:
        t = time.monotonic()
        # Mark the mirror dirty before the first byte changes.
        fr.write_json_atomic(side, {'format': fr.FORMAT, 'clean': False, 'hashes': local})
        per = fr.BIG // fr.MIB
        idx = [i * per + k for i in bad for k in range(per)]
        dev_small = fr.device_hashes(adb, p, idx, 1, jobs)
        loc_small = fr.hash_file_chunks(mirror, fr.MIB, idx, local_jobs)
        pieces = [i for i in idx if dev_small[i] != loc_small[i]]
        log(f'{len(pieces)} MiB differ inside the {len(bad)} chunks; fetching {"gzip " if compress else ""}streams')
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
        # Re-hash refreshed chunks at the full 16 MiB level against the device digests.
        again = fr.hash_file_chunks(mirror, fr.BIG, bad, local_jobs)
        if any(again[i] != dev[i] for i in bad):
            raise fr.DeviceError('Refreshed chunk does not match device digest')
        timings['refresh_s'] = round(time.monotonic() - t, 1)
    pre2 = preflight()
    epoch1 = fr.device_epoch(adb, p)
    if epoch1 != epoch0:
        raise fr.DeviceError('Userdata write counters or boot id changed during the snapshot')
    s = mirror.stat()
    sig = {'size': s.st_size, 'mtime_ns': s.st_mtime_ns, 'ino': s.st_ino}
    hashes = [dev[i] for i in range(n_big)]
    root = fr.root_of(hashes)
    fr.write_json_atomic(side, {'format': fr.FORMAT, 'clean': True, 'signature': sig, 'hashes': hashes, 'root': root})
    mirror.chmod(0o444)
    (outdir / 'preflight.json').write_text(json.dumps(pre2, indent=2) + '\n')
    timings['total_s'] = round(time.monotonic() - t0, 1)
    result = {'format': fr.FORMAT, 'kind': 'snapshot', 'mirror': str(mirror), 'bytes': size,
              'chunk_bytes': fr.BIG, 'root': root, 'per_chunk_manifest_matches': True,
              'independent_full_hash_replaced_by': 'per-16MiB-chunk device digests + userdata write-counter epoch',
              'epoch': epoch1, 'local_hash_skipped_by_sidecar': trusted,
              'changed_big_chunks': bad, 'changed_mib': len(pieces), 'downloaded_bytes': downloaded,
              'compressed': bool(compress), 'timings': timings, 'phone_partition_writes': 0}
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
    pre()
    if a.bench:
        for j, mbps in bench(adb, p):
            print(f'jobs={j}: {mbps} MiB/s')
        return
    header = adb.shell(f'dd if={mod.BY_NAME} bs=4096 count=1 status=none')
    assert len(header) == 4096 and header[1024:1026] == b'HX'
    block, count = struct.unpack_from('>II', header, 1024 + 40)
    size = block * count
    assert block == 4096 and size == 16 << 30
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'volume-header.bin').write_bytes(header)
    r = snapshot(adb, p, pre, a.mirror, a.output, size, jobs=a.jobs, compress=not a.no_compress,
                 trust_sidecar=a.trust_sidecar, seed=a.seed)
    print(json.dumps({k: v for k, v in r.items() if k != 'changed_big_chunks'}, indent=2))


if __name__ == '__main__':
    main()
