#!/usr/bin/env python3
"""RAM-rescue step: grow the installed iOS HFS root to the whole userdata partition (minus a tail reserve).

NOT EXECUTED. Run only in the tested ADB RAM rescue, with userdata unmounted, right after a successful
commit_root_delta_fast.py of the same transaction (the offline VM left the volume clean, journal empty).

  plan   (default): reads ~6 MiB from the device, prints the plan, writes plan.json + undo preimages. No writes.
  --commit        : writes through fastroot.script_apply (per 1 MiB chunk: device preimage hash re-checked
                    right before the write, staged payload hash checked, fsync, flush, full readback).
                    Three batches in this order: [bitmap extension + new alternate VH] (past the old end,
                    invisible to the old volume) -> [chunk 0: primary VH + old bitmap tail] (commit point)
                    -> [chunk with the old alternate VH, zeroed].
  --undo DIR      : restores the saved 1 MiB preimages of DIR (reverse order) ONLY if the device still holds
                    exactly what the grow wrote (i.e. iOS has not allocated anything in the new space yet).

Every device chunk touched is recorded (before/after SHA-256 and the before bytes) in OUT/undo/.
The host mirror is NOT advanced here: the next snapshot_current_root_fast.py (storage-20261114 version) sees the
grown volume header, resizes the sparse mirror and re-verifies (txn_grow.sh runs it right after the grow).
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
W = HERE.parent.parent.parent
sys.path.insert(0, str(W / 'gpu/tools'))
sys.path.insert(0, str(HERE))
import fastroot as fr  # noqa: E402
import hfs_grow as g   # noqa: E402

PART_SECTORS = 224657344          # userdata sdc57, verified by nord2-install-root.py preflight too
MIB = fr.MIB


def load_preflight(main):
    spec = importlib.util.spec_from_file_location('preflight', main / 'tools/nord2-install-root.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def chunk_reader(adb, p, part=None):
    """storage-20261114: userdata (224657344 sectors) is NOT a multiple of 1 MiB (last 1 MiB chunk is 992 KiB); reads in
    that partial tail chunk go through 4 KiB dd blocks (the old code died with 'short read chunk 109695')."""
    cache = {}
    full = part // MIB if part else None

    def tail_read(off, n):
        b0, b1 = off // BS4, -(-(off + n) // BS4)
        b = adb.shell(f'{fr.guard(p)} && dd if={p["dev"]} bs=4096 skip={b0} count={b1 - b0} status=none', timeout=120)
        if len(b) != (b1 - b0) * BS4:
            raise fr.DeviceError(f'short read at block {b0}')
        return b[off - b0 * BS4:off - b0 * BS4 + n]

    def chunk(i):
        if i not in cache:
            b = adb.shell(f'{fr.guard(p)} && dd if={p["dev"]} bs=1048576 skip={i} count=1 status=none', timeout=120)
            if len(b) != MIB:
                raise fr.DeviceError(f'short read chunk {i}')
            cache[i] = b
        return cache[i]

    def read(off, n):
        if full is not None and off + n > full * MIB:
            if off < full * MIB:
                k = full * MIB - off
                return read(off, k) + tail_read(off + k, n - k)
            if off + n > part:
                raise fr.DeviceError('read past the partition end')
            return tail_read(off, n)
        out = b''
        while n:
            i, at = divmod(off, MIB)
            k = min(n, MIB - at)
            out += chunk(i)[at:at + k]
            off += k
            n -= k
        return out
    return read, chunk


def to_chunks(writes, chunk):
    """Block writes -> {chunk_index: (before bytes, after bytes)}."""
    out = {}
    for w in writes:
        off, data = w['off'], w['new']
        while data:
            i, at = divmod(off, MIB)
            k = min(len(data), MIB - at)
            before, after = out.get(i, (chunk(i), bytearray(chunk(i))))
            after[at:at + k] = data[:k]
            out[i] = (before, after)
            off += k
            data = data[k:]
    return {i: (b, bytes(a)) for i, (b, a) in out.items()}


BS4 = 4096


def apply_block(adb, p, blk, before_sha, after, log, what):
    """One verified 4 KiB write (the partition alternate VH lives in the partial last MiB of userdata, which the 1 MiB
    chunk writer cannot address): preimage re-check, staged payload check, write+fsync, flush, readback."""
    assert len(after) == BS4
    w, dev, g_ = p['work'], p['dev'], fr.guard(p)
    adb.shell(f'mkdir -p {w} && cat > {w}/block.bin', data=after, timeout=120)
    a_sha = fr.sha(after)
    script = '\n'.join([
        'H4() { set -- $(dd if="$1" bs=4096 skip="$2" count=1 status=none | sha256sum); echo "$1"; }',
        f'{g_} || {{ echo FAIL guard; exit 2; }}',
        f'p=$(H4 {w}/block.bin 0); [ "$p" = {a_sha} ] || {{ echo "FAIL payload $p"; exit 3; }}',
        f'd=$(H4 {dev} {blk})',
        f'if [ "$d" = {before_sha} ]; then',
        f'  {g_} || {{ echo FAIL guard; exit 2; }}',
        f'  dd if={w}/block.bin of={dev} bs=4096 seek={blk} count=1 conv=notrunc,fsync status=none || {{ echo "FAIL write"; exit 5; }}',
        '  echo W',
        f'elif [ "$d" = {a_sha} ]; then echo D',
        'else echo "FAIL preimage $d"; exit 4; fi',
        'sync', f'blockdev --flushbufs {dev} 2>/dev/null', 'echo 3 > /proc/sys/vm/drop_caches 2>/dev/null',
        f'r=$(H4 {dev} {blk}); [ "$r" = {a_sha} ] || {{ echo "FAIL readback $r"; exit 6; }}',
        f'rm -f {w}/block.bin', 'echo BLOCK_OK'])
    out = adb.shell(script, timeout=300).decode()
    if 'BLOCK_OK' not in out.splitlines() or any(l.startswith('FAIL') for l in out.splitlines()):
        raise fr.DeviceError(f'{what}: block write failed: {out[-300:]!r}')
    log(f'block {what}: 4 KiB at block {blk} written/verified ({out.split()[0]})')


def apply_batches(adb, p, batches, log):
    for name, ents in batches:
        payload = b''.join(a for _, _, a in ents)
        adb.shell(f'mkdir -p {p["work"]} && cat > {p["work"]}/batch.bin', data=payload, timeout=600)
        spec = [(i, fr.sha(b), fr.sha(a)) for i, b, a in ents]
        out = adb.shell(fr.script_apply(p, spec, list(range(len(ents)))), timeout=900)
        fr.parse_apply(out, spec)
        log(f'batch {name}: {len(ents)} chunk(s) written and read back')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--main', type=Path, required=True)
    ap.add_argument('--adb', default='$HOME/Android/Sdk/platform-tools/adb')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--reserve-blocks', type=int, default=16384)
    ap.add_argument('--commit', action='store_true')
    ap.add_argument('--undo', type=Path, help='output dir of a previous --commit')
    a = ap.parse_args()
    log = lambda m: print(m, flush=True)
    mod = load_preflight(a.main)
    adb = fr.Adb.single(a.adb)
    p = dict(fr.REAL_PROFILE, dev=mod.BY_NAME)
    mod.preflight(a.adb, adb.serial)
    sectors = int(adb.shell('cat /sys/class/block/sdc57/size').strip())
    if sectors != PART_SECTORS or mod.PART_SECTORS != PART_SECTORS:
        raise fr.DeviceError(f'userdata has {sectors} sectors, expected {PART_SECTORS}')
    run(adb, p, sectors * 512, lambda: mod.preflight(a.adb, adb.serial), a, log)


def run(adb, p, part, preflight, a, log=print):
    """storage-20261114: split out of main() so the very same device steps run in host tests (fastroot.ShellAdb
    against a partition-sized image file); behaviour unchanged."""
    read, chunk = chunk_reader(adb, p, part)
    if a.undo:
        rec = json.loads((a.undo / 'plan.json').read_text())
        chunks = [(c['chunk'], (a.undo / 'undo' / f'{c["chunk"]}.before').read_bytes(), c['after']) for c in rec['chunks']]
        for i, b, after in chunks:
            if fr.sha(b) != [c for c in rec['chunks'] if c['chunk'] == i][0]['before']:
                raise fr.DeviceError('saved preimage corrupt')
            if fr.sha(chunk(i)) != after:
                raise fr.DeviceError(f'chunk {i} is no longer what the grow wrote: refuse undo (iOS used the new space?)')
        ext_start, ext = rec['summary']['bitmap_extension']
        writes = [dict(what=w['what'], off=w['off'], new=bytes.fromhex(w['new_hex']), old=b'') for w in rec['writes'] if w['what'] == 'bitmap extension']
        g.undo_check(read, rec['summary'], writes)
        pav = rec.get('partition_avh')
        if pav:
            pb = (a.undo / 'undo' / 'partition-avh.before').read_bytes()
            if fr.sha(pb) != pav['before']:
                raise fr.DeviceError('saved partition-AVH preimage corrupt')
            cur = fr.sha(read(pav['off'], BS4))
            if cur not in (pav['after'], pav['before']):
                raise fr.DeviceError('partition-AVH block changed since the grow (SWEETLOG/iOS wrote it): refuse undo')
            if cur == pav['after']:   # it was written last: undo it first
                apply_block(adb, p, pav['off'] // BS4, pav['after'], pb, log, 'partition alternate VH (undo)')
        # reverse order: cleanup chunk, commit chunk, then the past-the-end chunks
        order = list(reversed(rec['batches']))
        batches = [(n, [(i, chunk(i), b) for i, b, _ in chunks if i in idx]) for n, idx in order]
        apply_batches(adb, p, batches, log)
        adb.shell('sync')
        log('HFSGROW_UNDO_DONE: volume back to %d blocks' % rec['summary']['old_blocks'])
        return
    summary, writes = g.plan(read, part, a.reserve_blocks, expect_total=4194304)
    g.merge_same_block(writes)
    # storage-20261114: the partition alternate VH sits in the partial last MiB: separate verified 4 KiB write, last.
    pavw = [w for w in writes if w['what'] == 'partition alternate VH']
    assert len(pavw) == 1 and pavw[0]['off'] % BS4 == 0 and len(pavw[0]['new']) == BS4 and pavw[0]['off'] + BS4 == part
    pavw = pavw[0]
    writes = [w for w in writes if w is not pavw]
    ch = to_chunks(writes, chunk)
    first = {w['what']: w['off'] // MIB for w in writes}
    last = {w['what']: (w['off'] + len(w['new']) - 1) // MIB for w in writes}
    span = lambda k: list(range(first[k], last[k] + 1))
    past = sorted(set(span('bitmap extension') + span('new alternate VH')))
    commit = [first['primary VH (commit point)']]
    assert first['old bitmap block: free old altVH block'] == commit[0], 'bitmap tail not in chunk 0: review'
    cleanup = [first['zero old alternate VH']]
    assert all(i * MIB >= summary['old_bytes'] for i in past) and not set(past) & set(cleanup)
    assert sorted(past + commit + cleanup) == sorted(ch), 'unexpected chunk set'
    batches = [('past-the-end', past), ('commit', commit), ('cleanup', cleanup)]
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'undo').mkdir(exist_ok=True)
    for i, (b, _) in ch.items():
        (a.output / 'undo' / f'{i}.before').write_bytes(b)
    (a.output / 'undo' / 'partition-avh.before').write_bytes(pavw['old'])
    rec = {'summary': summary, 'partition_bytes': part, 'batches': batches,
           'partition_avh': {'off': pavw['off'], 'before': fr.sha(pavw['old']), 'after': fr.sha(pavw['new'])},
           'chunks': [{'chunk': i, 'before': fr.sha(b), 'after': fr.sha(x)} for i, (b, x) in sorted(ch.items())],
           'writes': [dict(what=w['what'], off=w['off'], len=len(w['new']), old_sha=fr.sha(w['old']), new_sha=fr.sha(w['new']),
                           new_hex=w['new'].hex() if w['what'] == 'bitmap extension' else '') for w in writes + [pavw]]}
    fr.write_json_atomic(a.output / 'plan.json', rec)
    log('HFSGROW_PLAN ' + json.dumps(summary))
    if not a.commit:
        log('plan only (no --commit): nothing written')
        return
    if (a.output / 'started.json').exists():
        raise RuntimeError('grow already started in this output dir; inspect, then --undo or finish manually')
    (a.output / 'started.json').write_text('{}\n')
    preflight()
    apply_batches(adb, p, [(n, [(i, ch[i][0], ch[i][1]) for i in idx]) for n, idx in batches], log)
    apply_block(adb, p, pavw['off'] // BS4, fr.sha(pavw['old']), pavw['new'], log, 'partition alternate VH')
    adb.shell('sync')
    fr.write_json_atomic(a.output / 'verified.json', {'grown': True, 'summary': summary, 'all_chunks_readback_verified': True})
    log('HFSGROW_COMMITTED new_blocks=%d (%.1f GiB)' % (summary['new_blocks'], summary['new_bytes'] / 2**30))


if __name__ == '__main__':
    main()
