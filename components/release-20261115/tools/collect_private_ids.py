#!/usr/bin/env python3
"""OWNER-SIDE ONLY (never shipped, never run on a recipient's data for publishing).

Collects the identifiers of the OWNER's Nord 2 (and of the owner's host account) from the owner's own private sources and
writes them to a private JSON file (mode 0600) that sanitize_release.py uses to prove a release tree contains none of them.
Nothing is printed except classes and counts.

    collect_private_ids.py --out private/ids.json [--mirror rootfs-mirror.img --hfsx hfsx.py] [--wifi WIFI] [--adt gpu.adt]
        [--android-cmdline cmdline.txt] [--ssh-dir DIR ...] [--scp-source nord2_scp.inc] [--nvdisk n2nvdisk.bin]
        [--literal CLASS=VALUE ...] [--owner-string STR ...]

Classes: imei, iccid, imsi, msisdn, serial, mac_wifi, mac_bt, udid, ssid, ssh_pub, ssh_secret, wifi_cal_sha256, sensor_cal,
owner_string, nv_identity_sha256 (sha256 of each NVD_IMEI/... file of the owner's modem blob: catches a copied blob or file).
"""
import argparse, base64, hashlib, json, os, plistlib, re, shutil, struct, subprocess, sys, tempfile, zlib

KEYMAP = [  # (regex on the plist key path, class)
    (r'IMEI|InternationalMobileEquipmentIdentity|kCTMobileEquipmentInfoIMEI', 'imei'),
    (r'MEID', 'imei'),
    (r'ICCID|IntegratedCircuitCardIdentity', 'iccid'),
    (r'IMSI|Imsi|InternationalMobileSubscriberIdentity', 'imsi'),
    (r'PhoneNumber|MSISDN|MobileSubscriberNumber', 'msisdn'),
    (r'SerialNumber', 'serial'),
    (r'UniqueDeviceID|UDID', 'udid'),
    (r'BluetoothAddress', 'mac_bt'),
    (r'WiFiAddress|WifiAddress|MACAddress|EthernetMacAddress', 'mac_wifi'),
    (r'SSID_STR|SSIDString|\bSSID\b|NetworkName', 'ssid'),
]
MAC_RE = re.compile(r'^([0-9a-fA-F]{2})[:-]?([0-9a-fA-F]{2})[:-]?([0-9a-fA-F]{2})[:-]?([0-9a-fA-F]{2})[:-]?([0-9a-fA-F]{2})[:-]?([0-9a-fA-F]{2})$')


def luhn_ok(s):
    if not s.isdigit():
        return False
    t = 0
    for i, ch in enumerate(reversed(s)):
        d = int(ch)
        if i % 2:
            d *= 2
            d -= 9 if d > 9 else 0
        t += d
    return t % 10 == 0


def norm(cls, v):
    if isinstance(v, bytes):
        if cls.startswith('mac') and len(v) == 6:
            return v.hex()
        try:
            v = v.decode()
        except UnicodeDecodeError:
            return None
    if not isinstance(v, (str, int)):
        return None
    v = str(v).strip()
    if cls in ('imei', 'iccid', 'imsi'):
        v = re.sub(r'[\s-]', '', v)
        if cls == 'imei' and not (len(v) in (14, 15, 16) and v.isdigit() and v.strip('0')):
            return None
        if cls == 'iccid' and not (18 <= len(v.rstrip('Ff')) <= 20 and v.rstrip('Ff').isdigit() and v.startswith('89')):
            return None
        if cls == 'imsi' and not (14 <= len(v) <= 15 and v.isdigit()):
            return None
        return v.rstrip('Ff')
    if cls == 'msisdn':
        d = re.sub(r'[^\d]', '', v)
        return d if len(d) >= 8 else None
    if cls.startswith('mac'):
        m = MAC_RE.match(v)
        if not m:
            return None
        h = ''.join(m.groups()).lower()
        return None if h in ('000000000000', 'ffffffffffff') else h
    if cls in ('serial', 'udid', 'ssid'):
        return v if len(v) >= 4 else None
    return v


class Ids:
    def __init__(self):
        self.d = {}

    def add(self, cls, v, src):
        v = norm(cls, v) if cls not in ('wifi_cal_sha256', 'sensor_cal', 'ssh_pub', 'ssh_secret', 'owner_string', 'nv_identity_sha256', 'gpt_fingerprint') else v
        if not v:
            return
        e = self.d.setdefault(cls, {})
        e.setdefault(v, set()).add(src)


def walk_plist(obj, path, ids, src):
    if isinstance(obj, dict):
        for k, v in obj.items():
            walk_plist(v, path + '/' + str(k), ids, src)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            walk_plist(v, path + '[%d]' % i, ids, src)
    else:
        last = path.rsplit('/', 1)[-1]
        for rx, cls in KEYMAP:
            if re.search(rx, last) or re.search(rx, path, re.I) and cls in ('imei', 'iccid', 'imsi', 'msisdn'):
                ids.add(cls, obj, src)
        if isinstance(obj, str):
            for m in re.finditer(r'(?<!\d)(89\d{16,18})(?!\d)', obj):
                ids.add('iccid', m.group(1), src)
        # ICCID-keyed dictionaries (CommCenter PersonalWallet / per-ICCID maps)
    if isinstance(obj, dict):
        for k in obj.keys():
            ks = str(k)
            if re.fullmatch(r'89\d{16,18}[Ff]?', ks):
                ids.add('iccid', ks, src + ' (key)')


def from_mirror(mirror, hfsx, ids, tmp):
    wanted = {
        '/private/var/wireless/Library/Preferences': None,
        '/private/var/root/Library/Lockdown': None,
        '/private/var/preferences/SystemConfiguration': None,
        '/private/var/mobile/Library/Preferences': ['com.apple.commcenter', 'com.apple.preferences', 'com.apple.Preferences', 'com.apple.MobileBluetooth', 'com.apple.wifi'],
        '/private/var/db/nord2-bt': ['bdaddr'],
        '/private/var/jb/etc/ssh': ['ssh_host_'],
        '/private/var/Managed Preferences/mobile': None,
    }
    got = 0
    for d, names in wanted.items():
        out = os.path.join(tmp, d.strip('/').replace('/', '_'))
        cmd = [sys.executable, '-I', hfsx, mirror, 'get', d, out] + (names or [])
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if r.returncode or not os.path.isdir(out):
            continue
        for f in sorted(os.listdir(out)):
            p = os.path.join(out, f)
            got += 1
            raw = open(p, 'rb').read()
            src = 'mirror:%s/%s' % (d, f)
            if f == 'bdaddr':
                ids.add('mac_bt', raw.decode(errors='ignore').strip(), src)
                continue
            if f.startswith('ssh_host_'):
                take_ssh(p, ids, src)
                continue
            try:
                pl = plistlib.loads(raw)
            except Exception:
                continue
            walk_plist(pl, '', ids, src)
    # MobileGestalt cache (systemgroup.com.apple.mobilegestaltcache): find it among the SystemGroup containers
    sg = '/private/var/containers/Shared/SystemGroup'
    r = subprocess.run([sys.executable, '-I', hfsx, mirror, 'ls', sg], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    for line in r.stdout.splitlines():
        g = line.split()[-1].rstrip('/') if line.strip() else ''
        if not re.fullmatch(r'[0-9A-F-]{36}', g):
            continue
        out = os.path.join(tmp, 'mg_' + g)
        rr = subprocess.run([sys.executable, '-I', hfsx, mirror, 'get', sg + '/' + g + '/Library/Caches', out, 'com.apple.MobileGestalt.plist'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        p = os.path.join(out, 'com.apple.MobileGestalt.plist')
        if rr.returncode == 0 and os.path.exists(p):
            got += 1
            try:
                walk_plist(plistlib.loads(open(p, 'rb').read()), '', ids, 'mirror:MobileGestalt.plist')
            except Exception:
                pass
    return got


def take_ssh(p, ids, src):
    raw = open(p, 'rb').read()
    if p.endswith('.pub'):
        parts = raw.split()
        if len(parts) >= 2:
            ids.add('ssh_pub', parts[1].decode(), src)
    else:
        ids.add('ssh_secret', hashlib.sha256(raw).hexdigest(), src + ' (sha256 of private key file)')
        body = b''.join(l for l in raw.splitlines() if l and not l.startswith(b'-----'))
        if len(body) > 64:
            ids.add('ssh_secret', body[16:80].decode(errors='ignore'), src + ' (64-char slice of key body)')


def from_wifi(path, ids):
    w = open(path, 'rb').read()
    ids.add('wifi_cal_sha256', hashlib.sha256(w).hexdigest(), 'wifi:' + os.path.basename(path))
    if len(w) >= 10:
        ids.add('mac_wifi', w[4:10].hex(), 'wifi:' + os.path.basename(path) + '[4:10]')


def from_adt(path, ids):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + '/n2dev')
    import n2adt
    root = n2adt.parse_all(open(path, 'rb').read())
    ch = n2adt.child(root, 'chosen')
    for k, _, v in ch.props:
        if k == 'mac-address-wifi0':
            ids.add('mac_wifi', v[:6].hex(), 'adt:mac-address-wifi0')
        if k == 'nord2-wifi-calibration':
            ids.add('wifi_cal_sha256', hashlib.sha256(v).hexdigest(), 'adt:nord2-wifi-calibration')


def from_cmdline(path, ids):
    s = open(path, 'rb').read().decode(errors='ignore')
    for m in re.finditer(r'androidboot\.serialno=(\S+)', s):
        ids.add('serial', m.group(1), 'android-cmdline')


def from_scp(path, ids):
    s = open(path).read()
    m = re.search(r'acc\[6\]\s*=\s*\{([^}]*)\}\s*,\s*gyr\[6\]\s*=\s*\{([^}]*)\}\s*,\s*ps\[6\]\s*=\s*\{([^}]*)\}', s)
    if not m:
        return
    for name, grp in zip(('acc', 'gyro', 'ps'), m.groups()):
        vals = [int(x) for x in grp.replace(' ', '').split(',') if x]
        ids.add('sensor_cal', '%s:%s' % (name, ','.join(map(str, vals[:3]))), 'kernel source nord2_scp.inc')


def from_nvdisk(path, ids):
    b = open(path, 'rb').read()
    if b[:8] != b'N2NVDSK1':
        return
    n = struct.unpack_from('<I', b, 12)[0]
    for i in range(n):
        e = b[4096 + 128 * i: 4096 + 128 * (i + 1)]
        name = e[:96].split(b'\0')[0].decode()
        size, clen, off, crc, flags, rcrc, _ = struct.unpack_from('<IIQIIII', e, 96)
        if '/NVD_IMEI/' in name or '/CALIBRAT/' in name and size >= 64:
            st = b[off:off + clen]
            raw = zlib.decompress(st, -15) if flags & 1 else st
            if '/NVD_IMEI/' in name:
                ids.add('nv_identity_sha256', hashlib.sha256(raw).hexdigest(), 'nvdisk:' + name.rsplit('/', 2)[-2] + '/' + name.rsplit('/', 1)[-1])
    ids.add('nv_identity_sha256', hashlib.sha256(b).hexdigest(), 'nvdisk:whole blob')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--mirror'); ap.add_argument('--hfsx')
    ap.add_argument('--wifi', action='append', default=[])
    ap.add_argument('--adt', action='append', default=[])
    ap.add_argument('--android-cmdline')
    ap.add_argument('--ssh-dir', action='append', default=[])
    ap.add_argument('--scp-source')
    ap.add_argument('--nvdisk', action='append', default=[])
    ap.add_argument('--gpt-dir', help='owner GPT baseline dir with header.bin (LBA1, 4 KiB) + entries.bin (LBA2..5)')
    ap.add_argument('--literal', action='append', default=[], help='CLASS=VALUE (e.g. imei=... read from the phone box), private')
    ap.add_argument('--owner-string', action='append', default=[])
    a = ap.parse_args()
    ids = Ids()
    tmp = tempfile.mkdtemp(prefix='.ids-', dir=os.path.dirname(os.path.abspath(a.out)))
    os.chmod(tmp, 0o700)
    try:
        nm = from_mirror(a.mirror, a.hfsx, ids, tmp) if a.mirror else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for w in a.wifi:
        from_wifi(w, ids)
    for d in a.adt:
        from_adt(d, ids)
    if a.android_cmdline:
        from_cmdline(a.android_cmdline, ids)
    for d in a.ssh_dir:
        for f in sorted(os.listdir(d)):
            if f.startswith('ssh_host_'):
                take_ssh(os.path.join(d, f), ids, 'ssh-dir:' + f)
            if f == 'ssh-password.txt':
                pw = open(os.path.join(d, f)).read().strip()
                if pw:
                    ids.add('ssh_secret', pw, 'ssh-dir:ssh-password.txt')
    if a.scp_source:
        from_scp(a.scp_source, ids)
    for n in a.nvdisk:
        from_nvdisk(n, ids)
    if a.gpt_dir:
        h = open(os.path.join(a.gpt_dir, 'header.bin'), 'rb').read(); e = open(os.path.join(a.gpt_dir, 'entries.bin'), 'rb').read()
        ids.add('gpt_fingerprint', hashlib.sha256(h + e).hexdigest(), 'gpt: LBA1..5 (two-lane pin)')
        ids.add('gpt_fingerprint', hashlib.sha256(e).hexdigest(), 'gpt: entry array (vendor pin)')
    for lit in a.literal:
        c, _, v = lit.partition('=')
        ids.add(c, v, 'literal')
    for s in a.owner_string:
        ids.add('owner_string', s, 'owner')
    # IMEI candidates must pass Luhn (a 14-digit MEID-style value is kept as is)
    if 'imei' in ids.d:
        ids.d['imei'] = {k: v for k, v in ids.d['imei'].items() if len(k) != 15 or luhn_ok(k)}
    out = {c: [{'value': v, 'sources': sorted(s)} for v, s in sorted(e.items())] for c, e in sorted(ids.d.items())}
    fd = os.open(a.out + '.tmp', os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'w') as f:
        json.dump({'format': 'nord2-private-ids-1', 'classes': out}, f, indent=1)
    os.replace(a.out + '.tmp', a.out)
    os.chmod(a.out, 0o600)
    print('private ids written to %s (mode 0600); mirror files read: %d' % (a.out, nm))
    for c, e in sorted(out.items()):
        print('  %-20s %d value(s)' % (c, len(e)))


if __name__ == '__main__':
    main()
