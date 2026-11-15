#!/usr/bin/env python3
"""Synthetic Nord 2 'device dumps' for host tests (no real phone data): mk_fixtures.py OUTDIR SEED
Writes OUTDIR/dump (same layout as n2_read_device.sh), OUTDIR/vendor/etc/md (fake model files), OUTDIR/expect.json
(release-manifest subset with the kernel constants this synthetic layout satisfies) and OUTDIR/truth.json (what the
profile must find)."""
import binascii, hashlib, json, os, random, struct, sys, uuid

O, SEED = sys.argv[1], int(sys.argv[2])
rnd = random.Random(SEED)
rb = lambda n: bytes(rnd.getrandbits(8) for _ in range(n))
D = os.path.join(O, 'dump')
for d in ('tree', 'wifi', 'gpt'):
    os.makedirs(os.path.join(D, d), exist_ok=True)

# WIFI: 6146 bytes, structure accepted by nfa_prepare
w = bytearray(6146)
mac = bytearray(rb(6)); mac[0] = (mac[0] & 0xfc) | 0x02  # locally administered unicast: never a real vendor MAC
w[4:10] = mac
w[256:262] = bytes([0, 3, 0, 1]) + struct.pack('<H', 4298 - 256)
def tlv(off, tag, end):
    n = end - off - 3
    w[off] = tag; w[off + 1:off + 3] = struct.pack('<H', n); w[off + 3:end] = bytes(rnd.randrange(1, 256) for _ in range(n))
tlv(262, 1, 3972); tlv(3972, 7, 4025); tlv(4025, 8, 4078); tlv(4078, 9, 4197); tlv(4197, 10, 4298)
open(os.path.join(D, 'wifi/WIFI'), 'wb').write(w)

# oplus_custom v1: header 80 B + Sensor[256]
acc = [rnd.randrange(-300, 300) for _ in range(3)]; gyro = [rnd.randrange(-12000, 12000) for _ in range(3)]
ps = [rnd.randrange(1, 500) for _ in range(3)] + [0, 0, 0]; als = rnd.randrange(800, 1400)
s = [0] * 64
s[6:9] = acc; s[11] = als; s[12:15] = gyro; s[15:21] = ps
oc = struct.pack('<II', 0x5a5a1234, 0x12345a5a) + b'\0' * 72 + struct.pack('<64i', *s)
open(os.path.join(D, 'oplus_custom.bin'), 'wb').write(oc + b'\0' * (4096 - len(oc)))

# GPT on a 4K LUN with the layout the release kernel was built for (super at index 54) and random partition GUIDs
LUN, FIRST, LAST = 31240192, 34, 31240183
SUPER_FIRST, SUPER_BYTES = 344064, 11039408128
names = ['part%02d' % i for i in range(58)]
names[49], names[54], names[57], names[14], names[17], names[18], names[7] = 'boot', 'super', 'userdata', 'nvdata', 'protect1', 'protect2', 'oplus_custom'
ents = bytearray(128 * 128)
pos = 6
for i, nm in enumerate(names):
    if nm == 'super':
        f, l = SUPER_FIRST, SUPER_FIRST + SUPER_BYTES // 4096 - 1
    elif nm == 'userdata':
        f, l = 3153920, 31236087
    else:
        f, l = pos, pos + 7
        pos += 8
    e = rb(16) + uuid.uuid4().bytes_le
    e = bytes(uuid.UUID(int=rnd.getrandbits(128)).bytes_le) + bytes(uuid.UUID(int=rnd.getrandbits(128)).bytes_le)
    ents[i * 128:(i + 1) * 128] = e + struct.pack('<QQQ', f, l, 0) + nm.encode('utf-16-le').ljust(72, b'\0')
ecrc = binascii.crc32(ents) & 0xffffffff
h = bytearray(92)
h[0:8] = b'EFI PART'; h[8:12] = b'\0\0\1\0'
struct.pack_into('<IIIQQQQ', h, 12, 92, 0, 0, 1, LUN - 1, FIRST, LAST)
struct.pack_into('<QIII', h, 72, 2, 128, 128, ecrc)
struct.pack_into('<I', h, 16, binascii.crc32(bytes(h)) & 0xffffffff)
lba = bytes(4096) + bytes(h) + bytes(4096 - 92) + bytes(ents)
open(os.path.join(D, 'gpt/lun-lba0-5.bin'), 'wb').write(lba)
open(os.path.join(D, 'gpt/lun-blocks.txt'), 'w').write('%d 4096-byte blocks on sdc\n' % LUN)

# super head: LP geometry (x2) + metadata slot 0 + backup copy with a linear 'vendor' extent
MMAX, SLOTS = 65536, 2
VOFF, VBYTES = 7708082176, 544354304
geo = bytearray(4096)
struct.pack_into('<II', geo, 0, 0x616c4467, 52); geo[8:40] = rb(32); struct.pack_into('<III', geo, 40, MMAX, SLOTS, 4096)
parts = [('system', 0, 1 << 20), ('vendor', VOFF // 512, VBYTES // 512), ('product', 1 << 22, 1 << 20)]
ptab = b''.join(nm.encode().ljust(36, b'\0') + struct.pack('<IIII', 0, i, 1, 0) for i, (nm, _, _) in enumerate(parts))
etab = b''.join(struct.pack('<QIQI', n, 0, start, 0) for _, start, n in parts)
tables = ptab + etab
hdr = bytearray(128)
struct.pack_into('<IHHI', hdr, 0, 0x414C5030, 10, 0, 128); struct.pack_into('<I', hdr, 44, len(tables))
struct.pack_into('<IIIIII', hdr, 80, 0, len(parts), 52, len(ptab), len(parts), 24)
md = (bytes(hdr) + tables).ljust(MMAX, b'\0')
sup = bytes(4096) + bytes(geo) + bytes(geo) + md * (SLOTS) + md * SLOTS
open(os.path.join(D, 'super-head.bin'), 'wb').write(sup[:262144].ljust(262144, b'\0'))

# modem trees: fake NVD_IMEI / CALIBRAT / others (random bytes = synthetic identities)
for root, sub in (('nvdata', 'md/NVRAM/NVD_IMEI'), ('nvdata', 'md/NVRAM/CALIBRAT'), ('nvdata', 'md/NVRAM/NVD_DATA'), ('protect_f', 'md/NVD_IMEI'), ('protect_s', 'md/NVD_IMEI')):
    p = os.path.join(D, 'tree', root, sub); os.makedirs(p, exist_ok=True)
    for i in range(3):
        open(os.path.join(p, 'F%03d_000' % i), 'wb').write(rb(rnd.randrange(64, 3000)))
v = os.path.join(O, 'vendor/etc/md'); os.makedirs(v, exist_ok=True)
for i in range(2):
    open(os.path.join(v, 'model_%d.EDB' % i), 'wb').write(random.Random(7 + i).randbytes(5000))   # same on every fixture = firmware
fw = {'boot': hashlib.sha256(b'synthetic stock boot %d' % (SEED % 2)).hexdigest(), 'recovery': hashlib.sha256(b'synthetic stock recovery').hexdigest()}
open(os.path.join(D, 'partitions.sha256'), 'w').write(''.join('%s %s\n' % kv for kv in fw.items()))
expect = {'kernel_constants': {'lun_blocks': LUN, 'gpt_last_usable': LAST, 'gpt_first_usable': FIRST, 'gpt_entries': 128, 'gpt_entry_lba': 2,
                               'super_index': 54, 'super_first': SUPER_FIRST, 'super_bytes': SUPER_BYTES, 'vendor_offset': VOFF,
                               'vendor_bytes': VBYTES, 'lp_backup_offset': 12288 + SLOTS * MMAX, 'lp_metadata_max': MMAX},
          'supported_firmware': fw}
json.dump(expect, open(os.path.join(O, 'expect.json'), 'w'), indent=1)
truth = {'mac': bytes(mac).hex(), 'wifi_sha256': hashlib.sha256(w).hexdigest(), 'acc': acc, 'gyro': gyro, 'ps': ps, 'als': als,
         'gpt5_sha256': hashlib.sha256(lba[4096:6 * 4096]).hexdigest(), 'entries_crc': ecrc}
json.dump(truth, open(os.path.join(O, 'truth.json'), 'w'), indent=1)
print('fixture', O, 'seed', SEED)
