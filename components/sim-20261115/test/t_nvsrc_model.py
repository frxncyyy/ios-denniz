#!/usr/bin/env python3
# host model of 0715's blob source choice (sweet_amfi.c sweet_fs_dev_open / sweet_fs_disk_init), the header/table checks with the
# kernel's rules, run on the real N2NVDSK1 format (blob built here from a synthetic tree, no identities) and on bt18c's failure.
import struct, zlib, sys, os, tempfile, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
def header_ok(b):
    if len(b) < 4096 or b[:8] != b'N2NVDSK1': return False
    ver, count, tbytes, tcrc = struct.unpack_from('<IIII', b, 8); hcrc = struct.unpack_from('<I', b, 32)[0]
    h = bytearray(b[:64]); struct.pack_into('<I', h, 32, 0)
    if ver != 1 or not count or count > 8192 or tbytes != count * 128 or zlib.crc32(bytes(h)) != hcrc: return False
    return zlib.crc32(b[4096:4096 + tbytes]) == tcrc
def choose(file_blob, raw_blob):
    """returns (src, n2nv_src) like the kernel: file first, raw if the file is absent or invalid"""
    bad = 0
    if file_blob is not None:
        if header_ok(file_blob): return 2, 2
        bad = 0x100
    if raw_blob is not None and header_ok(raw_blob): return 1, 1 | bad
    return 0, bad
t = tempfile.mkdtemp()
for d in ('nvdata/md/NVRAM/NVD_DATA', 'protect_f/md', 'protect_s/md', 'vendor/etc/md', 'vendor/etc/mdota'):
    os.makedirs(os.path.join(t, 'tree', d)); open(os.path.join(t, 'tree', d, 'X'), 'wb').write(b'synthetic' * 50)
subprocess.run([sys.executable, '-I', os.path.join(HERE, '..', 'portable', 'mk_nvdisk_dev.py'), '--tree', t + '/tree', '--out', t + '/b.bin'],
               check=True, capture_output=True)
good = open(t + '/b.bin', 'rb').read()
over = b'\x37\x7f\x06\x82' + good[4:]                                   # bt18c: first word 377f0682 (HFS reused the region)
fails = 0
for name, f, r, want in [('file ok + raw ok', good, good, (2, 2)), ('no file + raw ok (old behaviour)', None, good, (1, 1)),
                         ('no file + raw overwritten (bt18c)', None, over, (0, 0)), ('file ok + raw overwritten (fix)', good, over, (2, 2)),
                         ('file corrupt + raw ok', over, good, (1, 0x101)), ('file corrupt + raw overwritten', over, over, (0, 0x100))]:
    got = choose(f, r); ok = got == want; fails += not ok
    print('%-36s -> src %d n2nv_src 0x%x %s' % (name, got[0], got[1], 'ok' if ok else 'FAIL want %r' % (want,)))
print('T_NVSRC_MODEL_OK' if not fails else 'T_NVSRC_MODEL FAILED'); sys.exit(fails)
