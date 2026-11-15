#!/usr/bin/env python3
"""Release sanitizer for the Nord 2 iOS port.  Fails (exit 1) if a tree contains anything that must never be shared.

    sanitize_release.py TREE [--ids private/ids.json] [--report out.json] [--max-mib 512] [--quiet]

Two kinds of checks, both applied to every file AND to everything nested inside it (gzip streams at any offset, xz, lz4
legacy frames, tar and zip members; up to 4 levels):
 1. identity needles (only with --ids, the owner's private list from collect_private_ids.py): IMEI/ICCID/IMSI/MSISDN/serial in
    ASCII, UTF-16LE and swapped-nibble BCD (SIM/3GPP/MTK NVRAM encoding); Wi-Fi/BT MAC as raw bytes (both byte orders) and text;
    SSIDs; SSH host public keys, key slices, password; per-unit sensor calibration as int32 LE triples; sha256 of the owner's Wi-Fi
    calibration file / modem identity files as raw 32 bytes, hex text, or as the sha256 of a file/member; owner host strings
    (home path, user name) in text.
 2. structural detectors (no list needed, they also protect the RECIPIENT later): modem NVRAM blob (N2NVDSK1), MTK Wi-Fi
    calibration file (6146 B, header 00 03 00 01 at 256), an ADT carrying nord2-wifi-calibration / mac-address-wifi0 /
    nord2-sensor-cal / nord2-device-pins, OpenSSH/PEM private keys, HFS+/HFSX volumes (iOS root images), ext4 images (Android
    partitions), SQLite databases, files named like secrets/logs/dumps (see DENY_NAMES).
Nothing found is printed by value: only path, class, encoding and count.  The report never contains identifiers.
"""
import argparse, fnmatch, hashlib, io, json, lzma, os, re, struct, subprocess, sys, tarfile, zipfile, zlib

DENY_NAMES = ['*nvdisk*.bin', 'n2nvdisk*', 'ssh_host_*_key', 'ssh_host_*_key.pub', 'ssh-password*', 'ids.json', 'ids*.json.tmp',
              'xnu-live.log', 'usb.log', 'dmesg*.txt', 'dmesg*.log', '*pstore*', 'dmesg-ramoops*', 'logcat*', 'ranges.jsonl', 'events',
              'rootfs-mirror.img*', '*.manifest.json', 'WIFI', 'WIFI_CUSTOM', 'nvdata*.img', 'protect*.img', 'proinfo*.img',
              'persist*.img', 'nvram*.img', 'nvcfg*.img', 'oplus_custom*', 'seccfg*.img', 'otp*.img', 'data_ark.plist',
              'keychain-2.db*', 'com.apple.commcenter*.plist', 'com.apple.wifi*.plist', 'known-networks*', 'bdaddr',
              'device-profile*.json', 'partitions.sha256', 'n2dump-*', '*.tracev3', 'unifiedlog*', 'cmdline.txt',
              'kallsyms*.txt', 'config.gz', 'iomem*.txt', 'snapshot', 'verified.json']
DENY_DIRS = ['private', 'secrets', 'device-dump', 'n2dump', 'arc', 'runs', 'ui-usb', 'cc-txn-*', 'cc-fetch-*', 'IOS-BACKUP-*',
             'snapshot', 'mirror', '.ids-*']
ADT_DEVICE_PROPS = [b'nord2-wifi-calibration', b'mac-address-wifi0', b'nord2-sensor-cal', b'nord2-device-pins',
                    b'nord2-wifi-calibration-sha256']
MAX_DEPTH = 4


def bcd_swapped(d, pad='F'):
    if len(d) % 2:
        d += pad
    return bytes(int(d[i + 1], 16) << 4 | int(d[i], 16) for i in range(0, len(d), 2))


def needles_from_ids(path):
    """-> (list of (class, encoding, bytes)), set of sha256 hex of forbidden whole files, list of (class, text)"""
    d = json.load(open(path))['classes']
    nd, shas = [], {}
    def add(c, enc, b):
        if b and len(b) >= 4:
            nd.append((c, enc, b))
    for c, entries in d.items():
        for e in entries:
            v = e['value']
            if c in ('imei', 'iccid', 'imsi', 'msisdn', 'serial', 'udid', 'ssid', 'ssh_pub', 'owner_string'):
                add(c, 'ascii', v.encode())
                add(c, 'utf16le', v.encode('utf-16-le'))
                if c in ('imei', 'iccid', 'imsi', 'msisdn') and v.isdigit():
                    add(c, 'bcd-swapped', bcd_swapped(v))
                    if c in ('imei', 'imsi'):
                        add(c, 'bcd-3gpp', bcd_swapped(('a' if c == 'imei' else '9') + v)[:8] if len(v) == 15 else b'')
                        add(c, 'bcd-3gpp-len', bytes([8]) + bcd_swapped(('a' if c == 'imei' else '9') + v)[:8] if len(v) == 15 else b'')
                    if c == 'imei' and len(v) == 15:
                        add(c, 'ascii-14', v[:14].encode())
            elif c.startswith('mac'):
                raw = bytes.fromhex(v)
                add(c, 'raw', raw)
                add(c, 'raw-reversed', raw[::-1])
                for sep in (':', '-', ''):
                    for t in (v, v.upper()):
                        s = sep.join(t[i:i + 2] for i in range(0, 12, 2))
                        add(c, 'text' + (sep or 'plain'), s.encode())
                        add(c, 'utf16le' + (sep or 'plain'), s.encode('utf-16-le'))
            elif c == 'sensor_cal':
                name, _, vals = v.partition(':')
                xs = [int(x) for x in vals.split(',')]
                add(c, name + ' int32le', struct.pack('<%di' % len(xs), *xs))
                nd.append((c, name + ' text', re.compile((r'(?<![\d-])' + r'\s*,\s*'.join(re.escape(str(x)) for x in xs) + r'(?!\d)').encode())))
            elif c in ('wifi_cal_sha256', 'nv_identity_sha256', 'gpt_fingerprint') or (c == 'ssh_secret' and re.fullmatch(r'[0-9a-f]{64}', v)):
                if c != 'gpt_fingerprint':
                    shas[v] = c
                add(c, 'sha256-raw (pinned digest)', bytes.fromhex(v))
                add(c, 'sha256-hex', v.encode())
            elif c == 'ssh_secret':
                add(c, 'ascii', v.encode())
    return nd, shas


class Scanner:
    def __init__(self, needles, shas, max_bytes, allow=None):
        self.needles, self.shas, self.max = needles, shas, max_bytes
        self.allow = allow or {}      # sha256 -> reason: upstream public binaries that embed sample/public PEM keys
        lits = [n for n in needles if isinstance(n[2], bytes)]
        self.big = re.compile(b'|'.join(re.escape(b) for b in sorted({n[2] for n in lits}, key=len, reverse=True))) if lits else None
        self.bymap = {}
        for c, e, b in lits:
            self.bymap.setdefault(b, []).append((c, e))
        self.rx = [n for n in needles if not isinstance(n[2], bytes)]
        self.findings = []
        self.files = 0

    def hit(self, where, cls, enc, count=1):
        self.findings.append({'path': where, 'class': cls, 'what': enc, 'count': count})

    def scan_bytes(self, where, data, name, depth):
        self.files += 1
        h = hashlib.sha256(data).hexdigest()
        if h in self.shas:
            self.hit(where, self.shas[h], 'whole file is a forbidden identity/calibration file (sha256 match)')
        if self.big is not None:
            counts = {}
            for m in self.big.finditer(data):
                for ce in self.bymap[m.group(0)]:
                    counts[ce] = counts.get(ce, 0) + 1
            for (c, e), n in sorted(counts.items()):
                self.hit(where, c, e, n)
        for c, e, rx in self.rx:
            n = len(rx.findall(data))
            if n:
                self.hit(where, c, e, n)
        is_tar = len(data) > 512 and data[257:262] == b'ustar'
        self.structural(where, data, name, pem=not is_tar and h not in self.allow)
        if depth < MAX_DEPTH:
            self.nested(where, data, depth)

    def structural(self, where, data, name, pem=True):
        if b'N2NVDSK1' in data[:16] or re.search(b'N2NVDSK1\x01\x00\x00\x00', data):
            self.hit(where, 'modem_nvram_blob', 'N2NVDSK1 blob (IMEI + RF calibration of one phone)')
        if len(data) == 6146 and data[256:260] == b'\x00\x03\x00\x01':
            self.hit(where, 'wifi_calibration_file', 'MTK WIFI NVRAM file (factory MAC + RF calibration)')
        for p in ADT_DEVICE_PROPS:
            if p + b'\0' * (32 - len(p)) in data:
                self.hit(where, 'device_adt', 'ADT property %s (per-device data)' % p.decode())
        # a real PEM key = header + newline + base64 body (a bare header string inside ssh/gnutls binaries is not a key)
        for m in (re.finditer(rb'-----BEGIN ((?:OPENSSH |RSA |EC |DSA |ENCRYPTED )?PRIVATE KEY)-----\r?\n[A-Za-z0-9+/=\r\n]{40}', data) if pem else ()):
            self.hit(where, 'private_key', m.group(1).decode() + ' (PEM with key body)')
        if len(data) >= 1026 and data[1024:1026] in (b'H+', b'HX') and data[1026:1028] in (b'\x00\x04', b'\x00\x05'):
            self.hit(where, 'hfs_volume', 'HFS+/HFSX volume image (an iOS root volume holds user data)')
        if len(data) >= 1082 and data[1080:1082] == b'\x53\xef':
            self.hit(where, 'ext4_image', 'ext4 image (Android partition dump)')
        if data[:16] == b'SQLite format 3\x00':
            self.hit(where, 'sqlite_db', 'SQLite database (user data)')

    def nested(self, where, data, depth):
        # tar / zip
        if len(data) > 512 and data[257:262] == b'ustar':
            try:
                with tarfile.open(fileobj=io.BytesIO(data)) as t:
                    for m in t.getmembers():
                        nm = m.name
                        self.check_name(where + '!' + nm, os.path.basename(nm), nm)
                        if m.isfile() and m.size <= self.max:
                            self.scan_bytes(where + '!' + nm, t.extractfile(m).read(), os.path.basename(nm), depth + 1)
                return
            except tarfile.TarError:
                pass
        if data[:4] == b'PK\x03\x04':
            try:
                with zipfile.ZipFile(io.BytesIO(data)) as z:
                    for i in z.infolist():
                        self.check_name(where + '!' + i.filename, os.path.basename(i.filename), i.filename)
                        if i.file_size <= self.max:
                            self.scan_bytes(where + '!' + i.filename, z.read(i), os.path.basename(i.filename), depth + 1)
                return
            except zipfile.BadZipFile:
                pass
        # compressed streams at any offset
        for m in re.finditer(b'\x1f\x8b\x08', data):
            o = m.start()
            try:
                d = zlib.decompressobj(31)
                out = d.decompress(data[o:o + (1 << 30)], self.max)
            except zlib.error:
                continue
            if len(out) >= 8:
                self.scan_bytes('%s@gz+%#x' % (where, o), out, '', depth + 1)
        for m in re.finditer(b'\xfd7zXZ\x00', data):
            o = m.start()
            try:
                out = lzma.LZMADecompressor().decompress(data[o:], self.max)
            except lzma.LZMAError:
                continue
            if out:
                self.scan_bytes('%s@xz+%#x' % (where, o), out, '', depth + 1)
        for m in re.finditer(b'\x02\x21\x4c\x18', data):
            o = m.start()
            try:
                r = subprocess.run(['lz4', '-dc'], input=data[o:], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=120)
                out = r.stdout[:self.max]
            except (OSError, subprocess.TimeoutExpired):
                out = b''
            if len(out) >= 256:
                self.scan_bytes('%s@lz4+%#x' % (where, o), out, '', depth + 1)

    def check_name(self, where, base, rel):
        for g in DENY_NAMES:
            if fnmatch.fnmatch(base, g):
                self.hit(where, 'forbidden_name', 'file name matches %s' % g)
                break
        parts = rel.replace('\\', '/').split('/')[:-1]
        for p in parts:
            for g in DENY_DIRS:
                if fnmatch.fnmatch(p, g):
                    self.hit(where, 'forbidden_dir', 'inside a %s directory' % g)
                    return


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tree')
    ap.add_argument('--ids', help="owner's private ids.json (collect_private_ids.py); without it only structural checks run")
    ap.add_argument('--report')
    ap.add_argument('--max-mib', type=int, default=512, help='largest file/member scanned; bigger = finding (too big to ship)')
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--allow-known', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'known-upstream-pem.json'),
                    help='sha256 list of upstream binaries whose embedded PEM keys are public (TrollStore helper, GnuTLS)')
    a = ap.parse_args()
    needles, shas = needles_from_ids(a.ids) if a.ids else ([], {})
    allow = json.load(open(a.allow_known)) if a.allow_known and os.path.exists(a.allow_known) else {}
    s = Scanner(needles, shas, a.max_mib << 20, allow)
    root = os.path.abspath(a.tree)
    paths = [root] if os.path.isfile(root) else []
    if os.path.isdir(root):
        for dp, dns, fns in os.walk(root, followlinks=False):
            dns.sort()
            for f in sorted(fns):
                paths.append(os.path.join(dp, f))
            for dn in dns:
                if os.path.islink(os.path.join(dp, dn)):
                    s.hit(os.path.relpath(os.path.join(dp, dn), root), 'symlink', 'symlinked directory (not allowed in a release)')
    for p in paths:
        rel = os.path.relpath(p, root) if p != root else os.path.basename(p)
        s.check_name(rel, os.path.basename(p), rel)
        if os.path.islink(p):
            s.hit(rel, 'symlink', 'symlink (not allowed in a release)')
            continue
        st = os.stat(p)
        if st.st_size > (a.max_mib << 20):
            s.hit(rel, 'too_big', 'file larger than %d MiB (not scanned, not shippable)' % a.max_mib)
            continue
        s.scan_bytes(rel, open(p, 'rb').read(), os.path.basename(p), 0)
    rep = {'tree': root, 'ids_used': bool(a.ids), 'needles': len(needles), 'files_scanned': s.files,
           'findings': s.findings, 'verdict': 'FAIL' if s.findings else 'CLEAN'}
    if a.report:
        with open(a.report, 'w') as f:
            json.dump(rep, f, indent=1)
    if not a.quiet:
        agg = {}
        for f in s.findings:
            top = f['path'].split('!')[0].split('@')[0]
            agg.setdefault(top, {}).setdefault(f['class'], 0)
            agg[top][f['class']] += f['count']
        for top, cl in sorted(agg.items()):
            print('FINDING %s: %s' % (top, ', '.join('%s x%d' % kv for kv in sorted(cl.items()))))
    print('SANITIZE_%s: %d file(s)/member(s) scanned, %d finding(s), %d needles' % (rep['verdict'], s.files, len(s.findings), len(needles)))
    sys.exit(1 if s.findings else 0)


if __name__ == '__main__':
    main()
