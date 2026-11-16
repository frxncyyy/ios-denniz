#!/usr/bin/env python3
"""Catalog preservation check on mirror(before, journal replayed) vs mirror+delta(after).

Same logic as the per-run check_update_catalog.py scripts, but the 'after' side
is read from the offline-update delta instead of a second 16 GiB image.
Fails (exit 1) if any catalog record outside --owned/--owned-prefix changed.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fastroot as fr
import replay_hfs_journal as j

OWNED = ['/private/var/mobile/Library/Preferences/com.apple.backboardd.plist',
         '/private/var/mobile/Library/Preferences/.GlobalPreferences.plist',
         '/private/var/root/Library/Preferences/com.apple.backboardd.plist',
         '/private/var/root/Library/Preferences/.GlobalPreferences.plist',
         '/usr/local/libexec/nord2-metal-probe',
         '/System/Library/Extensions/Nord2Metal.bundle/Nord2Metal',
         '/System/Library/Extensions/Nord2Metal.bundle/Info.plist',
         # Files an update may add inside the bundle, and the bundle directory
         # record itself (its valence/dates change when one is added).
         '/System/Library/Extensions/Nord2Metal.bundle/PipelinePack.tpcpack',
         '/System/Library/Extensions/Nord2Metal.bundle/ABSchedule.conf',
         '/System/Library/Extensions/Nord2Metal.bundle',
         '/usr/local/share/nord2-metal/varying.bc']
# The shared pipeline-cache directory (chown/chmod of the directory, removal of
# root-owned stale entries) and anything the frontend later writes into it.
CACHE_DIR = '/private/var/mobile/Library/Caches/com.tegu.metal.pipelines'
OWNED.append(CACHE_DIR)
PREFIXES = ['/private/var/db/nord2-metal/', CACHE_DIR + '/']


def catalog(h, fetch, size):
    class Reader(h.Reader):
        def get(self, off, n):
            assert off >= 0 and n >= 0 and off + n <= self.size
            return self.callback(off, n)
    r = Reader(fetch, size, allow_dirty=True)
    head = r.fread(r.catalog, 0, 512)
    leaf, last, seen, out = h.U32(head, 24), 0, set(), {}
    while leaf:
        assert leaf not in seen and len(seen) < 50000
        seen.add(leaf)
        node = r.fread(r.catalog, leaf * r.node_size, 14)
        assert h.U32(node, 4) == last
        for key, rec in r.node(leaf, 1):
            typ = h.U16(rec, 0)
            if typ not in (1, 2):
                continue
            cnid = h.U32(rec, 8)
            assert cnid not in out
            name = bytes().join(struct.pack('>H', c) for c in key[1]).decode('utf-16-be')
            out[cnid] = {'parent': key[0], 'name': name, 'type': typ, 'permissions': rec[32:44].hex(),
                         'forks': rec[88:248].hex() if typ == 2 else None,
                         'finder_type': rec[48:56].hex() if typ == 2 else None}
        last, leaf = leaf, h.U32(node, 0)
    return out


def paths(records):
    def path(k):
        if k == 2:
            return ''
        v = records[k]
        return path(v['parent']) + '/' + v['name']
    return {path(k): v for k, v in records.items()}


def check(hfsx_reader, mirror, update_dir, owned=OWNED, prefixes=PREFIXES, journal_offset=532480, journal_bytes=8388608):
    upd = json.loads((update_dir / 'verified.json').read_text())
    size = upd['bytes']
    mfd, dfd = os.open(mirror, os.O_RDONLY), os.open(update_dir / 'delta.bin', os.O_RDONLY)
    pos = {e['chunk']: e['pos'] for e in upd['delta']}

    def raw(off, n):
        return os.pread(mfd, n, off)

    def after(off, n):
        buf = bytearray(os.pread(mfd, n, off))
        for c in range(off // fr.CHUNK, (off + n - 1) // fr.CHUNK + 1):
            if c in pos:
                a, z = max(off, c * fr.CHUNK), min(off + n, (c + 1) * fr.CHUNK)
                buf[a - off:z - off] = os.pread(dfd, z - a, pos[c] * fr.CHUNK + (a - c * fr.CHUNK))
        return bytes(buf)
    writes, _, _ = j.decode(raw(journal_offset, journal_bytes), size, journal_offset)
    pages = {}
    for offset, data in writes:
        for start in range(offset // 4096 * 4096, (offset + len(data) + 4095) // 4096 * 4096, 4096):
            if start not in pages:
                pages[start] = bytearray(raw(start, 4096))
            a, z = max(start, offset), min(start + 4096, offset + len(data))
            pages[start][a - start:z - start] = data[a - offset:z - offset]

    def before(off, n):
        v = bytearray(raw(off, n))
        for start in range(off // 4096 * 4096, (off + n + 4095) // 4096 * 4096, 4096):
            if start in pages:
                a, z = max(start, off), min(start + 4096, off + n)
                v[a - off:z - off] = pages[start][a - start:z - start]
        return bytes(v)
    b, a = catalog(hfsx_reader, before, size), catalog(hfsx_reader, after, size)
    bp, ap = paths(b), paths(a)
    changed = [k for k in sorted(set(bp) | set(ap)) if bp.get(k) != ap.get(k)]
    unknown = [k for k in changed if k not in owned and not any(k.startswith(x) for x in prefixes)]
    return {'before_records': len(b), 'after_records': len(a), 'changed_paths': changed, 'outside_owned_paths': unknown}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--hfsx-read', type=Path, required=True)
    ap.add_argument('--snapshot-receipt', type=Path, required=True)
    ap.add_argument('--update', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--owned-json', type=Path, help='JSON list replacing the default owned paths')
    a = ap.parse_args()
    s = importlib.util.spec_from_file_location('h', a.hfsx_read)
    h = importlib.util.module_from_spec(s); s.loader.exec_module(h)
    owned = json.loads(a.owned_json.read_text()) if a.owned_json else OWNED
    snap = json.loads(a.snapshot_receipt.read_text())
    out = check(h, Path(snap['mirror']), a.update, owned)
    a.output.write_text(json.dumps(out, indent=2) + '\n')
    print(json.dumps({'before': out['before_records'], 'after': out['after_records'],
                      'changed': len(out['changed_paths']), 'outside_owned_paths': out['outside_owned_paths']}))
    sys.exit(1 if out['outside_owned_paths'] else 0)


if __name__ == '__main__':
    main()
