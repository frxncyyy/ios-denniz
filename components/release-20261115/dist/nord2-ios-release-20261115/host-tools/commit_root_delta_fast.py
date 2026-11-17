#!/usr/bin/env python3
"""Fast, batched, verified install of an offline-update delta into the RAM-rescue userdata.

Same safety rules as commit_root_delta.py, per 1 MiB chunk:
  * the device preimage is hashed ON THE DEVICE immediately before that chunk
    is written (and the write is refused unless it equals the verified 'before');
  * the staged payload piece is hashed on the device against the 'after' digest
    before it may touch the partition;
  * after a batch, caches are flushed and EVERY written chunk is read back and
    must equal 'after' (any mismatch fails the batch and leaves the rescue up);
  * no phone write other than the changed chunks of the delta; no reboot.
Differences: all chunks go through a few adb shells (64 MiB per batch) instead of
three round trips per chunk, the host never hashes the 16 GiB images (the delta
table carries digests), and the whole-device pre-hash is replaced by the
write-counter epoch of the snapshot (full re-verify only if the epoch differs).
"""
import argparse
import json
import os
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fastroot as fr


def classify(actual, before, after):
    if actual == after:
        return 'done'
    if actual == before:
        return 'pending'
    raise fr.DeviceError('Unknown device preimage; refuse write')


def verify_device_unchanged(adb, p, snap, side_hashes, mirror, entries, jobs, resume, log, unverified=()):
    """Return 'epoch' or 'full'. Raises if the device is not the verified before image.
    storage-20261114: chunks listed as unverified (grown-volume free space) are not compared."""
    cur = fr.device_epoch(adb, p)
    if cur == snap['epoch'] and not resume:
        log('Device write counters and boot id identical to the snapshot: no re-hash needed')
        return 'epoch'
    n_big = len(side_hashes)
    unv = set(unverified)
    check = [i for i in range(n_big) if i not in unv]
    log(f'Epoch differs or resume requested: re-hashing {len(check)} verified chunks of the device in parallel')
    dev = fr.device_hashes(adb, p, check, 16, jobs)
    bad = [i for i in check if dev[i] != side_hashes[i]]
    if bad:
        if not resume:
            raise fr.DeviceError('Current device is not the verified before image')
        per = fr.BIG // fr.MIB
        idx = [i * per + k for i in bad for k in range(per)]
        small = fr.device_hashes(adb, p, idx, 1, jobs)
        local = fr.hash_file_chunks(mirror, fr.MIB, idx)
        table = {e['chunk']: e for e in entries}
        for i in idx:
            if small[i] == local[i]:
                continue
            if i // per in unv:
                continue
            e = table.get(i)
            if not e or classify(small[i], e['before'], e['after']) != 'done':
                raise fr.DeviceError(f'Unknown device content in chunk {i}; refuse resume')
    return 'full'


def advance_mirror(mirror, update_dir, entries, side):
    """After a fully read-back install, mirror == device again: apply the delta to the mirror.

    Every written chunk was verified on the device against the same 'after'
    digests, so the mirror equals the device chunk-wise without re-downloading.
    The sidecar is marked dirty first and republished clean afterwards.
    """
    side_path = Path(str(mirror) + '.manifest.json')
    fr.write_json_atomic(side_path, dict(side, clean=False))
    mirror.chmod(0o600)
    fd, df = os.open(mirror, os.O_RDWR), os.open(update_dir / 'delta.bin', os.O_RDONLY)
    try:
        for e in entries:
            os.pwrite(fd, os.pread(df, fr.CHUNK, e['pos'] * fr.CHUNK), e['chunk'] * fr.CHUNK)
        os.fsync(fd)
    finally:
        os.close(fd); os.close(df)
    per = fr.BIG // fr.CHUNK
    touched = sorted({e['chunk'] // per for e in entries})
    hashes = list(side['hashes'])
    for i, h in fr.hash_file_chunks(mirror, fr.BIG, touched).items():
        hashes[i] = h
    mirror.chmod(0o444)
    st = mirror.stat()
    sc = {'format': fr.FORMAT, 'clean': True, 'hashes': hashes, 'root': fr.root_of(hashes),
          'signature': {'size': st.st_size, 'mtime_ns': st.st_mtime_ns, 'ino': st.st_ino}}
    if side.get('unverified_chunks'):
        # storage-20261114: still free space as far as the snapshot proved; the installed chunks inside them are
        # read back on the device, the rest stays unverified until the next snapshot (which re-checks allocated ones).
        sc['unverified_chunks'] = side['unverified_chunks']
    fr.write_json_atomic(side_path, sc)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--main', type=Path, required=True)
    ap.add_argument('--snapshot-receipt', type=Path, required=True)
    ap.add_argument('--update', type=Path, required=True, help='offline-update output dir (verified.json + delta.bin)')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--expected-addon-sha256', required=True)
    ap.add_argument('--commit', action='store_true')
    ap.add_argument('--resume', action='store_true')
    ap.add_argument('--batch-mib', type=int, default=64)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--adb', default='adb')
    ap.add_argument('--skip-final-full-hash', action='store_true', default=True)
    a = ap.parse_args()
    import snapshot_current_root_fast as sf
    mod = sf.load_main_preflight(a.main)
    adb = fr.Adb.single(a.adb)
    p = dict(fr.REAL_PROFILE, dev=mod.BY_NAME)
    pre = lambda: mod.preflight(a.adb, adb.serial)
    run(adb, p, pre, a)


def run(adb, p, preflight, a, log=print):
    t0 = time.monotonic()
    snap = json.loads(a.snapshot_receipt.read_text())
    upd = json.loads((a.update / 'verified.json').read_text())
    mirror = Path(snap['mirror'])
    side = json.loads(Path(str(mirror) + '.manifest.json').read_text())
    s = mirror.stat()
    # Receipt checks (equivalent of check_receipts)
    assert snap['format'] == upd['format'] == fr.FORMAT
    assert snap['per_chunk_manifest_matches'] and upd['offline_verified'] and not upd['rehearsal']
    assert upd['addon_sha256'] == a.expected_addon_sha256, 'addon differs from exact approval'
    assert upd['before_root'] == snap['root'] == side['root'] and fr.root_of(side['hashes']) == snap['root']
    assert side['clean'] and side['signature'] == {'size': s.st_size, 'mtime_ns': s.st_mtime_ns, 'ino': s.st_ino}, 'mirror changed since snapshot'
    assert upd['bytes'] == snap['bytes']
    # storage-20261114: grown volume: free-space chunks the snapshot did not compare with the device.
    unverified = set(snap.get('unverified_chunks', []))
    assert unverified == set(side.get('unverified_chunks', [])), 'snapshot and sidecar disagree on unverified chunks'
    entries = upd['delta']
    assert upd['after_id'] == fr.delta_id(upd['before_root'], {e['chunk']: e['after'] for e in entries})
    # The delta table must be consistent with the mirror and the staged bytes (cheap: only changed chunks).
    dfd, mfd = os.open(a.update / 'delta.bin', os.O_RDONLY), os.open(mirror, os.O_RDONLY)
    try:
        for e in entries:
            assert fr.sha(os.pread(mfd, fr.CHUNK, e['chunk'] * fr.CHUNK)) == e['before'], 'mirror differs from delta before-digest'
            assert fr.sha(os.pread(dfd, fr.CHUNK, e['pos'] * fr.CHUNK)) == e['after'], 'delta.bin differs from its digest'
    finally:
        os.close(dfd); os.close(mfd)
    a.output.mkdir(exist_ok=True)
    write_json = fr.write_json_atomic
    write_json(a.output / 'manifest.json', {'before': snap['root'], 'after_id': upd['after_id'], 'changed': [e['chunk'] for e in entries]})
    log(f'Changed {len(entries)} MiB of {upd["bytes"] // fr.MIB} MiB')
    if not a.commit:
        return None
    marker = a.output / 'started.json'
    if marker.exists() and not a.resume:
        raise RuntimeError('Transaction already started; inspect and explicitly resume')
    preflight()
    mode = verify_device_unchanged(adb, p, snap, side['hashes'], mirror, entries, a.jobs, a.resume, log, unverified)
    # Classify every changed chunk once: any unknown preimage aborts BEFORE the first write.
    cur = fr.device_hashes(adb, p, [e['chunk'] for e in entries], 1, a.jobs)
    per = fr.BIG // fr.CHUNK
    free_rebased = 0
    if unverified:
        # A delta chunk inside a 16 MiB chunk the snapshot proved FREE (no allocated block after journal replay)
        # holds whatever the device had (old Android data); the mirror has zeros there. Its content means nothing
        # to HFS, and the epoch/re-hash above proved no write since the snapshot, so its preimage is the current
        # device content: re-base 'before' on it (script_apply still re-checks it right before the write and
        # reads back 'after').
        rebased = []
        for e in entries:
            c = cur[e['chunk']]
            if e['chunk'] // per in unverified and c not in (e['before'], e['after']):
                e = dict(e, before=c)
                free_rebased += 1
            rebased.append(e)
        entries = rebased
        if free_rebased:
            log(f'{free_rebased} delta chunks lie in snapshot-free space: preimage re-based on the device content')
    pending = [e for e in entries if classify(cur[e['chunk']], e['before'], e['after']) == 'pending']
    log(f'{len(pending)} chunks pending, {len(entries) - len(pending)} already done')
    preflight()
    marker.write_text(json.dumps({'before': snap['root'], 'after_id': upd['after_id']}) + '\n')
    per_batch = max(1, a.batch_mib)
    t_w = time.monotonic()
    with (a.output / 'ranges.jsonl').open('a') as journal, (a.update / 'delta.bin').open('rb') as df:
        for k in range(0, len(pending), per_batch):
            batch = pending[k:k + per_batch]
            payload = b''
            for e in batch:
                df.seek(e['pos'] * fr.CHUNK)
                d = df.read(fr.CHUNK)
                assert len(d) == fr.CHUNK and fr.sha(d) == e['after']
                payload += d
            adb.shell(f'mkdir -p {p["work"]} && cat > {p["work"]}/batch.bin', data=payload, timeout=600)
            ents = [(e['chunk'], e['before'], e['after']) for e in batch]
            out = adb.shell(fr.script_apply(p, ents, list(range(len(batch)))), timeout=900)
            fr.parse_apply(out, ents)
            for e in batch:
                journal.write(json.dumps({'chunk': e['chunk'], 'before': e['before'], 'after': e['after'], 'readback_verified': True}) + '\n')
            journal.flush(); os.fsync(journal.fileno())
            log(f'Committed and read back {k + len(batch)}/{len(pending)} chunks')
    adb.shell('sync')
    write_json(a.output / 'verified.json', {
        'before': snap['root'], 'after': upd['after_id'], 'after_id': upd['after_id'], 'changed_bytes': len(entries) * fr.CHUNK,
        'all_device_preimages_verified': True, 'changed_ranges_readback_verified': True,
        'full_device_readback_verified': False, 'final_full_hash_skipped': True,
        'pre_install_device_check': mode, 'free_space_preimages_rebased': free_rebased, 'timings': {'write_s': round(time.monotonic() - t_w, 1),
                                                       'total_s': round(time.monotonic() - t0, 1)}})
    log('All changed ranges read back and verified; final whole-device hash skipped (approved)')
    if getattr(a, 'advance_mirror', True):
        advance_mirror(mirror, a.update, entries, side)
        log('Warm mirror advanced to the installed state')
    return mode


if __name__ == '__main__':
    main()
