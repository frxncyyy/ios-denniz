#!/usr/bin/env python3
"""sim-20261115 (item 3): the N2NVDSK1 modem NVRAM blob of scratchpad-backup-20261106/nvbuild/mk_nvdisk.py, byte-identical format,
but built from a directory tree taken from THE PHONE IT WILL BE WRITTEN TO (never from another phone's backup).

    mk_nvdisk_dev.py --tree DIR --out OUT.bin [--check]
DIR holds  nvdata/md  protect_f/md  protect_s/md  (this phone's own partitions, copied read-only)  and  vendor/etc/md, vendor/etc/mdota
(model files from the vendor image: the same on every Nord 2 with the same firmware).  Also:
    mk_nvdisk_dev.py --extract BLOB --tree DIR      unpack a blob into such a tree (round-trip test only; the tree holds identities)
Nothing is printed from file contents; only counts, sizes and the blob sha256."""
import argparse, os, struct, sys, zlib, hashlib
OFF_GIB = 64
ROOTS = [('/mnt/vendor/nvdata/md', 'nvdata/md'), ('/mnt/vendor/protect_f/md', 'protect_f/md'), ('/mnt/vendor/protect_s/md', 'protect_s/md'),
         ('/vendor/etc/md', 'vendor/etc/md'), ('/vendor/etc/mdota', 'vendor/etc/mdota')]

def build(tree, out):
    files = []
    for canon, rel in ROOTS:
        src = os.path.join(tree, rel)
        for root, ds, fs in os.walk(src):
            for f in sorted(fs):
                p = os.path.join(root, f)
                files.append((canon + p[len(src):], p))
    files.sort()
    ents = []; data = bytearray()
    for canon, p in files:
        raw = open(p, 'rb').read()
        enc = canon.encode()
        assert len(enc) < 96, canon
        co = zlib.compressobj(9, zlib.DEFLATED, -15, 9); z = co.compress(raw) + co.flush()
        stored, flags = (z, 1) if len(z) < len(raw) else (raw, 0)
        ents.append((enc, len(raw), len(stored), len(data), zlib.crc32(stored), flags, zlib.crc32(raw)))
        data += stored + b'\0' * (-len(stored) % 4096)
    tbytes = 128 * len(ents); tcap = (tbytes + 4095) & ~4095; base = 4096 + tcap
    table = bytearray()
    for enc, size, clen, rel, crc, flags, rcrc in ents:
        table += enc + b'\0' * (96 - len(enc)) + struct.pack('<IIQIIII', size, clen, base + rel, crc, flags, rcrc, 0)
    hdr = bytearray(b'N2NVDSK1' + struct.pack('<IIIIQI', 1, len(ents), tbytes, zlib.crc32(table), len(data), 0))
    hdr += b'\0' * (64 - len(hdr))
    struct.pack_into('<I', hdr, 32, zlib.crc32(bytes(hdr[:64])))
    blob = bytes(hdr) + b'\0' * (4096 - len(hdr)) + bytes(table) + b'\0' * (tcap - tbytes) + bytes(data)
    blob += b'\0' * (-len(blob) % (1 << 20))
    assert len(blob) <= 64 << 20, 'blob > 64 MiB (the reserved region)'
    open(out, 'wb').write(blob)
    names = [e[0].decode() for e in ents]
    stats = {k: sum(1 for n in names if n.startswith(k)) for k, _ in ROOTS}
    print('blob %s: %d files (%s), %d MiB, sha256 %s' % (out, len(ents), ', '.join('%s %d' % (k, v) for k, v in stats.items()),
          len(blob) >> 20, hashlib.sha256(blob).hexdigest()))
    imei = sum(1 for n in names if '/NVD_IMEI/' in n); cal = sum(1 for n in names if '/CALIBRAT/' in n)
    print('identity/calibration present: NVD_IMEI files %d, CALIBRAT files %d' % (imei, cal))
    return imei, cal, stats

def extract(blob_path, tree):
    b = open(blob_path, 'rb').read()
    assert b[:8] == b'N2NVDSK1'
    ver, n, tbytes, tcrc, dlen, _ = struct.unpack_from('<IIIIQI', b, 8)
    for i in range(n):
        e = b[4096 + 128 * i: 4096 + 128 * (i + 1)]
        name = e[:96].split(b'\0')[0].decode()
        size, clen, off, crc, flags, rcrc, _ = struct.unpack_from('<IIQIIII', e, 96)
        st = b[off:off + clen]
        raw = zlib.decompress(st, -15) if flags & 1 else st
        assert zlib.crc32(raw) == rcrc and len(raw) == size, name
        for canon, rel in ROOTS:
            if name.startswith(canon + '/'):
                p = os.path.join(tree, rel, name[len(canon) + 1:]); break
        else:
            raise SystemExit('unknown root ' + name)
        os.makedirs(os.path.dirname(p), exist_ok=True); open(p, 'wb').write(raw)
    print('extracted %d files' % n)

ap = argparse.ArgumentParser(); ap.add_argument('--tree', required=True); ap.add_argument('--out'); ap.add_argument('--extract')
ap.add_argument('--check', action='store_true', help='refuse a tree without NVD_IMEI or CALIBRAT files (incomplete copy)')
a = ap.parse_args()
if a.extract: extract(a.extract, a.tree); sys.exit(0)
imei, cal, stats = build(a.tree, a.out)
if a.check and (not imei or not cal or not stats['/mnt/vendor/protect_f/md'] or not stats['/vendor/etc/md']):
    os.unlink(a.out); raise SystemExit('REFUSED: the tree is not a complete copy of this phone\'s nvdata/protect partitions + vendor md files')
