#!/usr/bin/env python3
"""Build the per-device profile of THE PHONE BEING INSTALLED from its own read-only dump (n2_read_device.sh output).

    n2_device_profile.py DUMP_DIR --out DUMP_DIR/device-profile.json [--expect release-manifest.json]

Reads only files of DUMP_DIR (never the network, never a phone).  Produces (all values come from the dump, nothing invented):
  wifi    : the MTK WIFI NVRAM file checked exactly as the kernel's nfa_prepare() does (size 6146, header 00 03 00 01 at 256,
            TLV layout tag7 @3972 .. tag10 @4197, no tag 21), its sha256 (the kernel pin), the factory MAC at [4:10] (unicast,
            non-zero).  Invalid -> REFUSED (Wi-Fi would refuse it too; we never substitute another phone's file).
  sensors : OPLUS factory calibration from the oplus_custom partition (sensor_config_info_t: 80-byte header + 256-byte
            Sensor block, v1 or v2 layout).  Implausible/blank -> defaults (acc/gyro 0 = uncalibrated, ps 0, als 1000) and a
            'sensor_cal_default' warning.  Never copied from another phone.
  storage : LUN capacity, primary GPT header + entry array (sha256 of LBA1..5 = the two-lane pin, entry-array sha256 and CRC
            = the vendor pins, userdata/boot/super extents), LP geometry/metadata sha256 and the vendor partition's linear
            extent inside super.
  modem   : presence/count of NVD_IMEI and CALIBRAT files in the dumped nvdata/protect trees (never decoded or printed).
  firmware: sha256 of the stock boot/recovery partitions (must match the release's supported firmware).
With --expect, compares the storage/firmware facts with the constants compiled into the release kernel and sets
verdict READY / REBUILD_NEEDED / REFUSED.  The profile is PRIVATE to the phone's owner (it holds its MAC and pins).
"""
import argparse, binascii, hashlib, json, os, struct, sys

WIFI_BYTES = 6146


def sha(b):
    return hashlib.sha256(b).hexdigest()


def le16(b, o):
    return b[o] | b[o + 1] << 8


def check_wifi(w):
    """Python port of nfa_prepare()'s structural checks (bsd/dev/wifi_main_phy_force.c) minus the owner digest."""
    if len(w) != WIFI_BYTES:
        return 'size %d != 6146' % len(w)
    if w[256] != 0 or w[257] != 3 or w[258] != 0 or w[259] != 1 or w[243] != 0:
        return 'header bytes at 243/256..259 differ'
    total = 256 + le16(w, 260)
    if total != 4298:
        return 'TLV total %d != 4298' % total
    off, prior, seen, start, stop, tag21 = 256, 0, 0, 0, 0, False
    while off < total:
        if not w[off] and not any(w[off:total]):
            break
        if off + 3 > total:
            return 'TLV overrun'
        tag, n = w[off], le16(w, off + 1)
        end = off + 3 + n
        if not n or end > total or tag >= 32 or seen & (1 << tag) or (tag and tag <= prior):
            return 'TLV order/size invalid at %d' % off
        seen |= 1 << tag
        prior = tag
        if tag == 7:
            start = off
        if tag == 10:
            stop = off
        if tag == 21:
            tag21 = True
        off = end
    need = (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10)
    if start != 3972 or stop != 4197 or tag21 or seen & need != need:
        return 'tag7/10 layout differs (start %d stop %d)' % (start, stop)
    mac = w[4:10]
    if not any(mac) or mac[0] & 1 or mac == b'\xff' * 6:
        return 'factory MAC invalid (zero/multicast)'
    return None


def parse_sensor(b):
    """oplus_custom: u32 magic1, u32 magic2, u32 other1[2], u8 other2[64], u8 Sensor[256] (sensor_devinfo.c)."""
    if len(b) < 336:
        return None, 'oplus_custom shorter than 336 bytes'
    s = struct.unpack_from('<64i', b, 80)
    v1 = {'acc': list(s[6:9]), 'gyro': list(s[12:15]), 'ps': list(s[15:21]), 'als': s[11]}
    v2 = {'acc': list(s[20:23]), 'gyro': list(s[30:33]), 'ps': list(s[0:6]), 'als': s[10]}

    def plausible(d):
        return (all(abs(x) <= 2000 for x in d['acc']) and all(abs(x) <= 40000 for x in d['gyro'])
                and all(0 <= x <= 65535 for x in d['ps']) and 100 <= d['als'] <= 100000 and any(d['acc'] + d['gyro'] + d['ps']))
    c = [(n, d) for n, d in (('v1', v1), ('v2', v2)) if plausible(d)]
    if len(c) == 1:
        return dict(c[0][1], layout=c[0][0]), None
    if len(c) == 2:
        return dict(v1, layout='v1'), 'both layouts plausible: v1 taken (the layout the Nord 2 stock kernel logged)'
    return None, 'no plausible calibration in oplus_custom'


def crc32(b):
    return binascii.crc32(b) & 0xffffffff


def parse_gpt(lba0_5, blk=4096):
    h = lba0_5[blk:2 * blk]
    if h[:8] != b'EFI PART':
        return None, 'no GPT header at LBA1'
    hs, hcrc = struct.unpack_from('<II', h, 12)
    hh = bytearray(h[:hs]); hh[16:20] = b'\0' * 4
    if crc32(bytes(hh)) != hcrc:
        return None, 'GPT header CRC mismatch'
    cur, bak, first, last = struct.unpack_from('<QQQQ', h, 24)
    elba, n, esz, ecrc = struct.unpack_from('<QIII', h, 72)
    ents = lba0_5[elba * blk: elba * blk + n * esz]
    if len(ents) != n * esz or crc32(ents) != ecrc:
        return None, 'GPT entry array CRC mismatch / truncated dump'
    parts = {}
    for i in range(n):
        e = ents[i * esz:(i + 1) * esz]
        if e[:16] == b'\0' * 16:
            continue
        nm = e[56:128].decode('utf-16-le', errors='ignore').split('\0')[0]
        f, l = struct.unpack_from('<QQ', e, 32)
        parts[nm] = {'index': i, 'first': f, 'last': l, 'blocks': l - f + 1}
    return {'gpt5_sha256': sha(lba0_5[blk:6 * blk]), 'entries_sha256': sha(ents), 'entries_crc': ecrc, 'header_crc': hcrc,
            'backup_lba': bak, 'first_usable': first, 'last_usable': last, 'entry_lba': elba, 'entries': n, 'entry_size': esz,
            'partitions': {k: v for k, v in parts.items() if k in ('boot', 'userdata', 'super', 'vendor_boot', 'nvdata', 'protect1',
                                                                  'protect2', 'proinfo', 'oplus_custom', 'persist', 'md1img')}}, None


def parse_lp(sup):
    """Android liblp: geometry at 4096 (and 8192), metadata slots from 12288; find 'vendor' (or vendor_a) linear extent."""
    if len(sup) < 12288 + 4096:
        return None, 'super dump too short'
    g = sup[4096:8192]
    magic, gsize = struct.unpack_from('<II', g, 0)
    if magic != 0x616c4467:
        return None, 'no LP geometry'
    mmax, slots, lbs = struct.unpack_from('<III', g, 40)
    out = {'geometry_sha256': sha(g), 'geometry_copies_equal': sup[4096:8192] == sup[8192:12288], 'metadata_max': mmax,
           'slots': slots, 'logical_block_size': lbs, 'backup_metadata_offset': 12288 + slots * mmax}
    md = sup[12288:12288 + mmax]
    out['metadata_sha256'] = sha(md)
    bo = out['backup_metadata_offset']
    out['backup_metadata_equal'] = sup[bo:bo + mmax] == md if len(sup) >= bo + mmax else None
    if struct.unpack_from('<I', md, 0)[0] != 0x414C5030:
        return out, 'no LP metadata header'
    hsize = struct.unpack_from('<I', md, 8)[0]
    tables = md[hsize:]
    # header: magic, major, minor, header_size, header_checksum[32], tables_size, tables_checksum[32], partitions desc, extents desc,...
    tdesc = struct.unpack_from('<IIIIIIIIIIII', md, 80)
    (p_off, p_n, p_es, e_off, e_n, e_es) = tdesc[0:6]
    ext = []
    for i in range(e_n):
        nsec, ttype, tdata, tsrc = struct.unpack_from('<QIQI', tables, e_off + i * e_es)
        ext.append((nsec, ttype, tdata, tsrc))
    for i in range(p_n):
        p = tables[p_off + i * p_es: p_off + (i + 1) * p_es]
        nm = p[:36].split(b'\0')[0].decode()
        attrs, first_ext, n_ext, gidx = struct.unpack_from('<IIII', p, 36)
        if nm in ('vendor', 'vendor_a'):
            e = ext[first_ext:first_ext + n_ext]
            if len(e) == 1 and e[0][1] == 0:
                out['vendor_offset'] = e[0][2] * 512
                out['vendor_bytes'] = e[0][0] * 512
            else:
                out['vendor_extents'] = len(e)
    return out, None


def count_identity(tree):
    imei = cal = files = 0
    for dp, _, fs in os.walk(tree):
        for f in fs:
            files += 1
            p = os.path.join(dp, f)
            imei += '/NVD_IMEI/' in p + '/'
            cal += '/CALIBRAT/' in p + '/'
    return {'files': files, 'nvd_imei_files': imei, 'calibrat_files': cal}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dump')
    ap.add_argument('--out', required=True)
    ap.add_argument('--allow-firmware-mismatch', action='store_true', help='only warn when a stock partition hash differs')
    ap.add_argument('--expect', help='release-manifest.json (kernel constants + supported firmware)')
    a = ap.parse_args()
    D = a.dump
    rd = lambda n: open(os.path.join(D, n), 'rb').read() if os.path.exists(os.path.join(D, n)) else None
    prof = {'format': 'nord2-device-profile-1', 'private': True, 'errors': [], 'warnings': []}
    w = rd('wifi/WIFI')
    if w is None:
        prof['errors'].append('wifi/WIFI missing in the dump')
    else:
        e = check_wifi(w)
        if e:
            prof['errors'].append('WIFI calibration refused: ' + e)
        else:
            prof['wifi'] = {'sha256': sha(w), 'mac': w[4:10].hex(), 'bytes': len(w)}
    oc = rd('oplus_custom.bin')
    sc, why = parse_sensor(oc) if oc else (None, 'oplus_custom.bin missing')
    if sc is None:
        prof['warnings'].append('sensor_cal_default: ' + why)
        sc = {'acc': [0, 0, 0], 'gyro': [0, 0, 0], 'ps': [0, 0, 0, 0, 0, 0], 'als': 1000, 'layout': 'default'}
    elif why:
        prof['warnings'].append(why)
    prof['sensors'] = sc
    g = rd('gpt/lun-lba0-5.bin')
    if g is None:
        prof['errors'].append('gpt/lun-lba0-5.bin missing')
    else:
        gp, why = parse_gpt(g)
        if why:
            prof['errors'].append(why)
        else:
            prof['gpt'] = gp
    cap = rd('gpt/lun-blocks.txt')
    if cap:
        prof.setdefault('gpt', {})['lun_blocks'] = int(cap.split()[0])
    s = rd('super-head.bin')
    if s is None:
        prof['errors'].append('super-head.bin missing')
    else:
        lp, why = parse_lp(s)
        if lp:
            prof['lp'] = lp
        if why:
            prof['errors'].append('LP: ' + why)
    trees = {k: count_identity(os.path.join(D, 'tree', k)) for k in ('nvdata', 'protect_f', 'protect_s') if os.path.isdir(os.path.join(D, 'tree', k))}
    prof['modem'] = trees
    if not trees or not sum(t['nvd_imei_files'] for t in trees.values()) or not sum(t['calibrat_files'] for t in trees.values()):
        prof['errors'].append('the dump has no NVD_IMEI/CALIBRAT files: the modem blob cannot be built from THIS phone (refusing; never borrow one)')
    fw = rd('partitions.sha256')
    if fw:
        prof['firmware'] = dict(l.split()[:2] for l in fw.decode().splitlines() if len(l.split()) >= 2)
    if a.expect:
        m = json.load(open(a.expect))
        kc = m.get('kernel_constants', {})
        diff = []
        gp = prof.get('gpt', {})
        lp = prof.get('lp', {})
        sup = gp.get('partitions', {}).get('super', {})
        facts = {'lun_blocks': gp.get('lun_blocks'), 'gpt_last_usable': gp.get('last_usable'), 'gpt_first_usable': gp.get('first_usable'),
                 'gpt_entries': gp.get('entries'), 'gpt_entry_lba': gp.get('entry_lba'), 'super_index': sup.get('index'),
                 'super_first': sup.get('first'), 'super_bytes': sup.get('blocks', 0) * 4096 if sup else None,
                 'vendor_offset': lp.get('vendor_offset'), 'vendor_bytes': lp.get('vendor_bytes'),
                 'lp_backup_offset': lp.get('backup_metadata_offset'), 'lp_metadata_max': lp.get('metadata_max')}
        for k, v in kc.items():
            if facts.get(k) is not None and facts[k] != v:
                diff.append(k)
            if facts.get(k) is None:
                prof['warnings'].append('could not verify kernel constant %s from the dump' % k)
        prof['kernel_constant_mismatch'] = diff
        fwok = m.get('supported_firmware', {})
        for part, want in fwok.items():
            got = prof.get('firmware', {}).get(part)
            if got and got != want:
                (prof['warnings'] if a.allow_firmware_mismatch else prof['errors']).append('firmware: %s sha256 differs from the supported build (flash the supported OxygenOS build first)' % part)
            if not got:
                prof['warnings'].append('firmware: %s not hashed in the dump' % part)
        prof['verdict'] = 'REFUSED' if prof['errors'] else ('REBUILD_NEEDED' if diff else 'READY')
    else:
        prof['verdict'] = 'REFUSED' if prof['errors'] else 'PROFILED'
    fd = os.open(a.out + '.tmp', os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w') as f:
        json.dump(prof, f, indent=1)
    os.replace(a.out + '.tmp', a.out)
    print('PROFILE %s: errors %d, warnings %d, sensors %s, wifi %s, kernel constant mismatches %s' % (
        prof['verdict'], len(prof['errors']), len(prof['warnings']), sc.get('layout'), 'ok' if 'wifi' in prof else 'NO',
        prof.get('kernel_constant_mismatch', '-')))
    for e in prof['errors']:
        print('  ERROR', e)
    for e in prof['warnings']:
        print('  WARN ', e)
    sys.exit(0 if prof['verdict'] in ('READY', 'PROFILED') else (3 if prof['verdict'] == 'REBUILD_NEEDED' else 2))


if __name__ == '__main__':
    main()
