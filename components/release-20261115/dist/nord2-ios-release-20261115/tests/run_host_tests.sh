#!/bin/bash
# Host tests of the per-device install tooling (no phone).  Synthetic fixtures only, except the OWNER_* steps, which run on the
# owner's machine when OWNER_ENV points at a private env file (paths of the owner's kernel/ADT/WIFI/GPT/base and ids.json).
#   run_host_tests.sh [WORKDIR]      (WORKDIR default: a new temp dir; nothing outside it is written)
set -uo pipefail
H=$(cd "$(dirname "$0")" && pwd); TL=$H/../tools; N=$TL/n2dev
WD=${1:-$(mktemp -d)}; mkdir -p "$WD"; PASS=0; FAIL=0
ok() { echo "PASS $*"; PASS=$((PASS+1)); }; ko() { echo "FAIL $*"; FAIL=$((FAIL+1)); }
t() { local name=$1; shift; if "$@" > "$WD/$name.log" 2>&1; then ok "$name"; else ko "$name (see $WD/$name.log)"; fi; }
tn() { local name=$1; shift; if "$@" > "$WD/$name.log" 2>&1; then ko "$name (should have been refused)"; else ok "$name (refused as expected)"; fi; }
py() { python3 -I "$@"; }

# 1. fixtures: two different synthetic phones
py $H/mk_fixtures.py $WD/a 11 >/dev/null; py $H/mk_fixtures.py $WD/b 22 >/dev/null
t profile_a py $N/n2_device_profile.py $WD/a/dump --out $WD/a/dump/device-profile.json --expect $WD/a/expect.json
t profile_values py -c "
import json;p=json.load(open('$WD/a/dump/device-profile.json'));t=json.load(open('$WD/a/truth.json'))
assert p['verdict']=='READY' and p['wifi']['mac']==t['mac'] and p['wifi']['sha256']==t['wifi_sha256']
assert p['sensors']['acc']==t['acc'] and p['sensors']['gyro']==t['gyro'] and p['sensors']['ps']==t['ps'] and p['sensors']['als']==t['als']
assert p['gpt']['gpt5_sha256']==t['gpt5_sha256'] and p['gpt']['entries_crc']==t['entries_crc'] and p['lp']['vendor_offset']==7708082176"
# broken WIFI -> refused; missing oplus_custom -> defaults + warning; foreign firmware -> refused; other layout -> REBUILD_NEEDED
cp -a $WD/a $WD/a_badwifi; py -c "
p='$WD/a_badwifi/dump/wifi/WIFI';b=bytearray(open(p,'rb').read());b[3972]=99;open(p,'wb').write(b)"
tn profile_bad_wifi py $N/n2_device_profile.py $WD/a_badwifi/dump --out $WD/x1.json --expect $WD/a/expect.json
cp -a $WD/a $WD/a_nosens; rm $WD/a_nosens/dump/oplus_custom.bin
t profile_sensor_default bash -c "python3 -I $N/n2_device_profile.py $WD/a_nosens/dump --out $WD/x2.json --expect $WD/a/expect.json | grep -q sensor_cal_default"
tn profile_foreign_firmware py $N/n2_device_profile.py $WD/b/dump --out $WD/x3.json --expect $WD/a/expect.json
py -c "
import json;e=json.load(open('$WD/a/expect.json'));e['kernel_constants']['lun_blocks']+=1;json.dump(e,open('$WD/expect_other.json','w'))"
t profile_rebuild_needed bash -c "python3 -I $N/n2_device_profile.py $WD/a/dump --out $WD/x4.json --expect $WD/expect_other.json; [ \$? = 3 ]"
cp -a $WD/a $WD/a_noimei; rm -rf $WD/a_noimei/dump/tree/*/md/NVD_IMEI $WD/a_noimei/dump/tree/nvdata/md/NVRAM/NVD_IMEI
tn profile_no_imei py $N/n2_device_profile.py $WD/a_noimei/dump --out $WD/x5.json --expect $WD/a/expect.json

# 2. modem NVRAM blob only from the phone's own dump
for f in a b; do mkdir -p $WD/$f/nvtree/vendor/etc; cp -a $WD/$f/dump/tree/* $WD/$f/nvtree/; cp -a $WD/$f/vendor/etc/md $WD/$f/nvtree/vendor/etc/
  py $N/mk_nvdisk_dev.py --tree $WD/$f/nvtree --out $WD/$f/n2nvdisk.bin --check > $WD/mk_$f.log; done
t nvdisk_own py $N/n2_check_nvdisk.py $WD/a/n2nvdisk.bin --dump $WD/a/dump
tn nvdisk_other_phone py $N/n2_check_nvdisk.py $WD/b/n2nvdisk.bin --dump $WD/a/dump
cp $WD/a/n2nvdisk.bin $WD/tampered.bin; py -c "
import struct,zlib;p='$WD/tampered.bin';b=bytearray(open(p,'rb').read())
for i in range(struct.unpack_from('<I',b,12)[0]):
  o=4096+128*i;n=b[o:o+96].split(b'\0')[0]
  if b'/NVD_IMEI/' in n:
    size,clen,off,crc,flags,rcrc,_=struct.unpack_from('<IIQIIII',b,o+96)
    raw=bytearray(zlib.decompress(bytes(b[off:off+clen]),-15) if flags&1 else b[off:off+clen]); raw[0]^=1
    b[off:off+size]=raw; struct.pack_into('<IIQIIII',b,o+96,size,size,off,zlib.crc32(raw),0,zlib.crc32(raw),0); break
t2=bytes(b[4096:4096+128*struct.unpack_from('<I',b,12)[0]]); struct.pack_into('<I',b,20,zlib.crc32(t2))
h=bytearray(b[:64]);struct.pack_into('<I',h,32,0);struct.pack_into('<I',b,32,zlib.crc32(bytes(h)));open(p,'wb').write(b)"
tn nvdisk_identity_tampered py $N/n2_check_nvdisk.py $WD/tampered.bin --dump $WD/a/dump
t nvfile_payload py $N/mk_nvfile_payload.py --blob $WD/a/n2nvdisk.bin --dump $WD/a/dump --out $WD/a/nvpayload
tn nvfile_payload_foreign py $N/mk_nvfile_payload.py --blob $WD/b/n2nvdisk.bin --dump $WD/a/dump --out $WD/a/nvpayload2

# 3. sanitizer self-test with a SYNTHETIC ids list (planted needles in several encodings + nesting)
py - "$WD" <<'EOF'
import json, os, sys, gzip, io, tarfile, struct
W = sys.argv[1]; S = W + '/san'; os.makedirs(S + '/clean', exist_ok=True); os.makedirs(S + '/dirty', exist_ok=True)
ids = {'format': 'nord2-private-ids-1', 'classes': {
  'imei': [{'value': '490154203237518', 'sources': ['test']}], 'iccid': [{'value': '8939012345678901234', 'sources': ['test']}],
  'mac_wifi': [{'value': '02a1b2c3d4e5', 'sources': ['test']}], 'serial': [{'value': 'TESTSERIAL42', 'sources': ['test']}],
  'sensor_cal': [{'value': 'acc:-111,22,-33', 'sources': ['test']}], 'owner_string': [{'value': '/home/testowner', 'sources': ['test']}],
  'wifi_cal_sha256': [{'value': 'ab' * 32, 'sources': ['test']}]}}
json.dump(ids, open(S + '/ids.json', 'w'))
open(S + '/clean/readme.txt', 'w').write('nothing private here 0123456789\n')
d = S + '/dirty'
open(d + '/a.txt', 'w').write('imei 490154203237518\n')
open(d + '/b.bin', 'wb').write(b'\x00' + bytes.fromhex('02a1b2c3d4e5')[::-1] + b'\x00')          # BT-order raw MAC
open(d + '/c.bin', 'wb').write(b'xx' + bytes([0x98, 0x93, 0x10, 0x32, 0x54, 0x76, 0x98, 0x10, 0x32, 0xf4]))   # ICCID BCD swapped
open(d + '/d.gz', 'wb').write(gzip.compress(b'serial=TESTSERIAL42'.encode('utf-16-le') if False else 'serial=TESTSERIAL42'.encode('utf-16-le')))
bio = io.BytesIO()
with tarfile.open(fileobj=bio, mode='w') as t:
    data = struct.pack('<3i', -111, 22, -33); ti = tarfile.TarInfo('inner/cal.bin'); ti.size = len(data); t.addfile(ti, io.BytesIO(data))
    data = b'path /home/testowner/x'; ti = tarfile.TarInfo('inner/p.txt'); ti.size = len(data); t.addfile(ti, io.BytesIO(data))
open(d + '/e.tar', 'wb').write(bio.getvalue())
open(d + '/f.bin', 'wb').write(b'pin' + bytes.fromhex('ab' * 32))
open(d + '/n2nvdisk.bin', 'wb').write(b'N2NVDSK1\x01\x00\x00\x00' + bytes(100))
k = b'-----BEGIN OPENSSH PRIVATE KEY-----\n' + b'b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW' + b'\n'
open(d + '/g', 'wb').write(k)
EOF
t sanitize_clean py $TL/sanitize_release.py $WD/san/clean --ids $WD/san/ids.json
py $TL/sanitize_release.py $WD/san/dirty --ids $WD/san/ids.json --report $WD/san/dirty.json > $WD/sanitize_dirty.log 2>&1
t sanitize_dirty_all_found py -c "
import json;d=json.load(open('$WD/san/dirty.json'));c={(f['path'].split('!')[0].split('@')[0],f['class']) for f in d['findings']}
need={('a.txt','imei'),('b.bin','mac_wifi'),('c.bin','iccid'),('d.gz','serial'),('e.tar','sensor_cal'),('e.tar','owner_string'),('f.bin','wifi_cal_sha256'),('n2nvdisk.bin','modem_nvram_blob'),('g','private_key')}
miss=need-c; assert not miss, miss; assert d['verdict']=='FAIL'
assert not any(v in open('$WD/san/dirty.json').read() for v in ('490154203237518','TESTSERIAL42','02a1b2c3d4e5')), 'report leaks values'"

# 4. ADT + kernel pins + pack (needs the owner/release artifacts; OWNER_ENV or RELEASE_DIR)
if [ -n "${OWNER_ENV:-}" ] && [ -f "$OWNER_ENV" ]; then . "$OWNER_ENV"
  t adt_template py $N/mk_device_adt.py --make-template "$OWNER_ADT" $WD/template.adt
  t adt_device py $N/mk_device_adt.py $WD/template.adt $WD/a/device.adt --profile $WD/a/dump/device-profile.json --wifi $WD/a/dump/wifi/WIFI
  tn adt_wrong_wifi py $N/mk_device_adt.py $WD/template.adt $WD/a/device2.adt --profile $WD/a/dump/device-profile.json --wifi $WD/b/dump/wifi/WIFI
  t kernel_template py $N/n2_kernel_pins.py template "$OWNER_KERNEL_STRIP" --stage-dev "$OWNER_STAGE_DEV" --out $WD/kernel.tpl --map $WD/pins-map.json --owner-crc "$OWNER_GPT_CRC" --scrub "$OWNER_SCRUB"
  tn kernel_tpl_has_placeholders py $N/n2_kernel_pins.py check $WD/kernel.tpl --map $WD/pins-map.json
  t kernel_apply py $N/n2_kernel_pins.py apply $WD/kernel.tpl --map $WD/pins-map.json --profile $WD/a/dump/device-profile.json --out $WD/a/kernel.dev
  t kernel_dev_no_placeholders py $N/n2_kernel_pins.py check $WD/a/kernel.dev --map $WD/pins-map.json
  tn kernel_apply_twice py $N/n2_kernel_pins.py apply $WD/a/kernel.dev --map $WD/pins-map.json --profile $WD/a/dump/device-profile.json --out $WD/a/kernel.dev2
  t kernel_dev_values py - <<EOF
import json, struct, hashlib
k = open('$WD/a/kernel.dev', 'rb').read(); m = json.load(open('$WD/pins-map.json')); p = json.load(open('$WD/a/dump/device-profile.json'))
by = {x['kind']: x for x in m['pins']}
o = by['wifi_cal_sha256']['offset']; assert k[o:o+32].hex() == p['wifi']['sha256']
o = by['gpt5_sha256']['offset']; assert k[o:o+32].hex() == p['gpt']['gpt5_sha256']
o = by['sensor_cal']['offset']; v = struct.unpack_from('<19i', k, o); assert list(v[0:3]) == p['sensors']['acc'] and v[18] == p['sensors']['als']
c = by['gpt_entries_crc']; g = lambda off: (struct.unpack_from('<I', k, off)[0] >> 5) & 0xffff
assert g(c['movz']) | g(c['movk']) << 16 == p['gpt']['entries_crc']
EOF
  if [ -n "${OWNER_IDS:-}" ]; then
    t kernel_tpl_owner_clean py $TL/sanitize_release.py $WD/kernel.tpl --ids "$OWNER_IDS"
    t adt_tpl_owner_clean py $TL/sanitize_release.py $WD/template.adt --ids "$OWNER_IDS"
  fi
  if [ -n "${TEST_BASE:-}" ] && [ -n "${HT:-}" ]; then
    py -c "
import json;json.dump({'commandline':'$OWNER_CMDLINE','phys_base':'0x100000000','mem_size':'0x13e000000'},open('$WD/manifest.json','w'))"
    t pack_device env MANIFEST=$WD/manifest.json PINS_MAP=$WD/pins-map.json bash $N/n2_pack_device.sh $WD/a/boot-device.img $WD/a/kernel.dev $WD/a/device.adt "$TEST_BASE"
    tn pack_template_refused env MANIFEST=$WD/manifest.json PINS_MAP=$WD/pins-map.json bash $N/n2_pack_device.sh $WD/a/boot-tpl.img $WD/kernel.tpl $WD/a/device.adt "$TEST_BASE"
    if [ -n "${OWNER_IDS:-}" ]; then
      py $TL/sanitize_release.py $WD/a/boot-device.img --ids "$OWNER_IDS" --report $WD/pack_scan.json > /dev/null 2>&1
      t pack_device_no_owner_ids py -c "
import json;d=json.load(open('$WD/pack_scan.json'));c={f['class'] for f in d['findings']}
assert c <= {'device_adt'}, c   # the synthetic phone's own ADT is expected; nothing of the owner may appear"
    fi
  fi
else
  echo "SKIP owner-artifact steps (set OWNER_ENV)"
fi
echo "HOST_TESTS: $PASS passed, $FAIL failed (workdir $WD)"
[ $FAIL = 0 ]
