#!/usr/bin/env python3
"""Per-device values compiled into the Nord 2 XNU kernel: turn them into placeholders (release) and back into THIS phone's
own values (install).  The kernel binary of the port carries, as constants:
  wifi_cal_sha256  32 B  sha256 of the board WIFI NVRAM file (wifi_main_phy_force.c + wifi_normal_config.c, one merged const)
  gpt5_sha256      32 B  sha256 of LUN2 LBA1..5 (GPT header + entries; ufs_two_lane_adapter.inc) - live two-lane UFS HALTS the
                         boot when it differs (nord2ufs2live=1): partition GUIDs are random per unit, so this is per device
  vendor_gpt_sha256/vendor_geometry_sha256/vendor_lp_sha256  32 B each (sweet_ufs.c vendor read: Wi-Fi/BT firmware source)
  gpt_entries_crc  u32   immediate (movz/movk pair) in sweet_ufs.c, CRC of the same per-unit GPT entry array
  sensor_cal       76 B  acc[6], gyr[6], ps[6], als[1] int32 (nord2_scp.inc static consts, OPLUS factory calibration)

  owner:     n2_kernel_pins.py template KERNEL --stage-dev STAGE/bsd/dev --out KERNEL.tpl --map pins-map.json [--scrub OLD=NEW ...]
             finds each owner value EXACTLY ONCE (else refuses), overwrites it with a fixed placeholder, records the offsets.
             --scrub replaces same-length owner strings (build paths) inside the binary.
  recipient: n2_kernel_pins.py apply KERNEL.tpl --map pins-map.json --profile device-profile.json --out KERNEL.dev
             checks every placeholder is still there, writes this phone's values (from its own dump), refuses unless READY.
  any:       n2_kernel_pins.py check KERNEL --map pins-map.json   -> exit 1 if a placeholder is left (never flash that)
"""
import argparse, hashlib, json, os, re, struct, sys

PIN32 = [('wifi_cal_sha256', 'wifi_main_phy_force.c', 'expected'), ('gpt5_sha256', 'ufs_two_lane_adapter.inc', 'expected'),
         ('vendor_gpt_sha256', 'sweet_ufs.c', 'nord2_vendor_gpt_sha'), ('vendor_geometry_sha256', 'sweet_ufs.c', 'nord2_vendor_geometry_sha'),
         ('vendor_lp_sha256', 'sweet_ufs.c', 'nord2_vendor_lp_sha')]


def placeholder(kind, n):
    out = b''
    i = 0
    while len(out) < n:
        out += hashlib.sha256(b'N2PIN-PLACEHOLDER:%s:%d' % (kind.encode(), i)).digest()
        i += 1
    return out[:n]


def src_array(stage, f, name):
    s = open(os.path.join(stage, f)).read()
    m = re.search(name + r'\[32\]\s*=\s*\{([^}]*)\}', s)
    if not m:
        raise SystemExit('cannot find %s in %s' % (name, f))
    b = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1)))
    assert len(b) == 32
    return b


def sensor_block(stage):
    s = open(os.path.join(stage, 'nord2_scp.inc')).read()
    m = re.search(r'acc\[6\]\s*=\s*\{([^}]*)\}\s*,\s*gyr\[6\]\s*=\s*\{([^}]*)\}\s*,\s*ps\[6\]\s*=\s*\{([^}]*)\}\s*,\s*als\[1\]\s*=\s*\{([^}]*)\}', s)
    if not m:
        raise SystemExit('cannot find the sensor calibration constants in nord2_scp.inc')
    vals = []
    for grp, n in zip(m.groups(), (6, 6, 6, 1)):
        xs = [int(x) for x in grp.replace(' ', '').split(',') if x]
        vals += xs + [0] * (n - len(xs))
    return struct.pack('<19i', *vals)


def find_crc_imm(k, val):
    lo, hi = val & 0xffff, val >> 16
    res = []
    for o in range(0, len(k) - 32, 4):
        w = struct.unpack_from('<I', k, o)[0]
        if (w & 0x7fe00000) == 0x52800000 and ((w >> 5) & 0xffff) == lo:
            rd = w & 31
            for d in range(1, 8):
                w2 = struct.unpack_from('<I', k, o + 4 * d)[0]
                if (w2 & 0x7fe00000) == 0x72a00000 and ((w2 >> 5) & 0xffff) == hi and (w2 & 31) == rd:
                    res.append((o, o + 4 * d))
    return res


def set_imm(k, off, imm16):
    w = struct.unpack_from('<I', k, off)[0]
    struct.pack_into('<I', k, off, (w & ~(0xffff << 5)) | (imm16 << 5))


def get_imm(k, off):
    return (struct.unpack_from('<I', k, off)[0] >> 5) & 0xffff


def template(a):
    k = bytearray(open(a.kernel, 'rb').read())
    pins = []
    for kind, f, name in PIN32:
        v = src_array(a.stage_dev, f, name)
        n = k.count(v)
        if n != 1:
            raise SystemExit('REFUSED: %s found %d times (want exactly 1)' % (kind, n))
        o = k.find(v)
        k[o:o + 32] = placeholder(kind, 32)
        pins.append({'kind': kind, 'offset': o, 'bytes': 32})
    sb = sensor_block(a.stage_dev)
    if k.count(sb) != 1:
        raise SystemExit('REFUSED: sensor calibration block found %d times' % k.count(sb))
    o = k.find(sb)
    k[o:o + len(sb)] = placeholder('sensor_cal', len(sb))
    pins.append({'kind': 'sensor_cal', 'offset': o, 'bytes': len(sb)})
    crc = int(a.owner_crc, 0)
    loc = find_crc_imm(k, crc)
    if len(loc) != 1:
        raise SystemExit('REFUSED: GPT entries CRC immediate found %d times' % len(loc))
    ph = struct.unpack('<I', placeholder('gpt_entries_crc', 4))[0]
    set_imm(k, loc[0][0], ph & 0xffff); set_imm(k, loc[0][1], ph >> 16)
    pins.append({'kind': 'gpt_entries_crc', 'movz': loc[0][0], 'movk': loc[0][1], 'placeholder': ph})
    scrubbed = 0
    for sc in a.scrub:
        old, _, new = sc.partition('=')
        if len(old) != len(new):
            raise SystemExit('--scrub needs same-length strings')
        scrubbed += k.count(old.encode())
        k[:] = k.replace(old.encode(), new.encode())
    open(a.out, 'wb').write(k)
    m = {'format': 'nord2-kernel-pins-1', 'kernel_template_sha256': hashlib.sha256(k).hexdigest(), 'kernel_bytes': len(k), 'pins': pins}
    json.dump(m, open(a.map, 'w'), indent=1)
    print('kernel template %s: %d pins -> placeholders, %d owner strings scrubbed, sha256 %s' % (a.out, len(pins), scrubbed, m['kernel_template_sha256']))


def check(k, m):
    left = []
    for p in m['pins']:
        if p['kind'] == 'gpt_entries_crc':
            if get_imm(k, p['movz']) | get_imm(k, p['movk']) << 16 == p['placeholder']:
                left.append(p['kind'])
        elif k[p['offset']:p['offset'] + p['bytes']] == placeholder(p['kind'], p['bytes']):
            left.append(p['kind'])
    return left


def apply(a):
    k = bytearray(open(a.kernel, 'rb').read())
    m = json.load(open(a.map))
    if hashlib.sha256(k).hexdigest() != m['kernel_template_sha256']:
        raise SystemExit('REFUSED: kernel is not the release template named by the pins map')
    prof = json.load(open(a.profile))
    if prof.get('verdict') != 'READY':
        raise SystemExit('REFUSED: device profile verdict is %s (need READY: run n2_device_profile.py --expect)' % prof.get('verdict'))
    sc = prof['sensors']
    vals = {'wifi_cal_sha256': bytes.fromhex(prof['wifi']['sha256']), 'gpt5_sha256': bytes.fromhex(prof['gpt']['gpt5_sha256']),
            'vendor_gpt_sha256': bytes.fromhex(prof['gpt']['entries_sha256']),
            'vendor_geometry_sha256': bytes.fromhex(prof['lp']['geometry_sha256']),
            'vendor_lp_sha256': bytes.fromhex(prof['lp']['metadata_sha256']),
            'sensor_cal': struct.pack('<19i', *(sc['acc'][:3] + [0, 0, 0] + sc['gyro'][:3] + [0, 0, 0] + (sc['ps'] + [0] * 6)[:6] + [sc['als']]))}
    if len(check(k, m)) != len(m['pins']):
        raise SystemExit('REFUSED: not every placeholder is present (already applied or wrong file)')
    for p in m['pins']:
        if p['kind'] == 'gpt_entries_crc':
            c = prof['gpt']['entries_crc']
            set_imm(k, p['movz'], c & 0xffff); set_imm(k, p['movk'], c >> 16)
        else:
            v = vals[p['kind']]
            assert len(v) == p['bytes']
            k[p['offset']:p['offset'] + p['bytes']] = v
    assert not check(k, m)
    fd = os.open(a.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as f:
        f.write(k)
    print('device kernel %s: %d per-device values written from this phone\'s profile, sha256 %s' % (a.out, len(m['pins']), hashlib.sha256(k).hexdigest()))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest='cmd', required=True)
    t = sp.add_parser('template'); t.add_argument('kernel'); t.add_argument('--stage-dev', required=True); t.add_argument('--out', required=True)
    t.add_argument('--map', required=True); t.add_argument('--owner-crc', required=True, help='owner GPT entries CRC (private, from the owner dump)'); t.add_argument('--scrub', action='append', default=[])
    p = sp.add_parser('apply'); p.add_argument('kernel'); p.add_argument('--map', required=True); p.add_argument('--profile', required=True); p.add_argument('--out', required=True)
    c = sp.add_parser('check'); c.add_argument('kernel'); c.add_argument('--map', required=True)
    a = ap.parse_args()
    if a.cmd == 'template':
        template(a)
    elif a.cmd == 'apply':
        apply(a)
    else:
        left = check(open(a.kernel, 'rb').read(), json.load(open(a.map)))
        print('PINS_LEFT %s' % (', '.join(left) if left else 'none'))
        sys.exit(1 if left else 0)


if __name__ == '__main__':
    main()
