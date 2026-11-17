#!/usr/bin/env python3
"""Refuse a modem NVRAM blob (N2NVDSK1) that is not made from THIS phone's own partitions.

    n2_check_nvdisk.py BLOB --dump DUMP_DIR [--foreign-ids ids.json]

PASS only if: the header/table CRCs are valid (same checks as the kernel 0715 / guest-nvfile1), every per-device file in the
blob (/mnt/vendor/nvdata/md, protect_f/md, protect_s/md) is byte-identical to the same file in DUMP_DIR/tree (the read-only
dump of the phone being installed) and every dumped file is in the blob, and the blob holds NVD_IMEI and CALIBRAT files.
So the IMEI and RF calibration the modem gets are the phone's own; nothing is invented, nothing comes from another unit.
--foreign-ids (the release owner's private list) additionally refuses a blob carrying the owner's identity files.
Prints counts only."""
import argparse, hashlib, json, os, struct, sys, zlib

ROOTS = {'/mnt/vendor/nvdata/md': 'nvdata/md', '/mnt/vendor/protect_f/md': 'protect_f/md', '/mnt/vendor/protect_s/md': 'protect_s/md'}


def entries(b):
    if b[:8] != b'N2NVDSK1':
        raise SystemExit('REFUSED: not an N2NVDSK1 blob')
    ver, n, tbytes, tcrc = struct.unpack_from('<IIII', b, 8)
    h = bytearray(b[:64]); hc = struct.unpack_from('<I', b, 32)[0]; struct.pack_into('<I', h, 32, 0)
    if ver != 1 or not 0 < n <= 8192 or tbytes != 128 * n or zlib.crc32(bytes(h)) != hc or zlib.crc32(b[4096:4096 + tbytes]) != tcrc:
        raise SystemExit('REFUSED: blob header/table invalid')
    for i in range(n):
        e = b[4096 + 128 * i: 4096 + 128 * (i + 1)]
        name = e[:96].split(b'\0')[0].decode()
        size, clen, off, crc, flags, rcrc, _ = struct.unpack_from('<IIQIIII', e, 96)
        st = b[off:off + clen]
        if zlib.crc32(st) != crc:
            raise SystemExit('REFUSED: stored CRC mismatch in an entry')
        raw = zlib.decompress(st, -15) if flags & 1 else st
        if len(raw) != size or zlib.crc32(raw) != rcrc:
            raise SystemExit('REFUSED: entry content CRC mismatch')
        yield name, raw


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('blob'); ap.add_argument('--dump', required=True); ap.add_argument('--foreign-ids')
    a = ap.parse_args()
    b = open(a.blob, 'rb').read()
    tree = os.path.join(a.dump, 'tree')
    want = {}
    for canon, rel in ROOTS.items():
        base = os.path.join(tree, rel)
        for dp, _, fs in os.walk(base):
            for f in fs:
                p = os.path.join(dp, f)
                want[canon + p[len(base):]] = p
    seen, same, differ, extra, imei, cal = set(), 0, 0, 0, 0, 0
    foreign = set()
    if a.foreign_ids:
        d = json.load(open(a.foreign_ids))['classes']
        foreign = {e['value'] for e in d.get('nv_identity_sha256', [])}
    hit_foreign = 0
    for name, raw in entries(b):
        if hashlib.sha256(raw).hexdigest() in foreign:
            hit_foreign += 1
        if not any(name.startswith(c + '/') for c in ROOTS):
            continue                                   # /vendor/etc/md*: firmware model files (same on every unit of a build)
        seen.add(name)
        imei += '/NVD_IMEI/' in name
        cal += '/CALIBRAT/' in name
        p = want.get(name)
        if p is None:
            extra += 1
        elif open(p, 'rb').read() == raw:
            same += 1
        else:
            differ += 1
    missing = len(set(want) - seen)
    if a.foreign_ids and hashlib.sha256(b).hexdigest() in foreign:
        hit_foreign += 1
    print('blob: %d per-device files identical to this phone\'s dump, %d differ, %d not in the dump, %d dump files missing; '
          'NVD_IMEI %d, CALIBRAT %d%s' % (same, differ, extra, missing, imei, cal, (', foreign identity files %d' % hit_foreign) if a.foreign_ids else ''))
    bad = []
    if differ or extra or missing:
        bad.append('the blob is not made from this phone\'s partitions')
    if not imei or not cal:
        bad.append('no NVD_IMEI/CALIBRAT files (incomplete dump)')
    if hit_foreign:
        bad.append('the blob carries the release owner\'s identity files')
    if bad:
        print('NVDISK_REFUSED: ' + '; '.join(bad)); sys.exit(1)
    print('NVDISK_OWN_OK')


if __name__ == '__main__':
    main()
