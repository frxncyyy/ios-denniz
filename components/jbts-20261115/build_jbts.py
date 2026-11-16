#!/usr/bin/env python3
"""jbts1 payload builder (host only, never touches the phone): Sileo + OpenSSH (rootless Procursus prefix /var/jb) + TrollStore.

Inputs (inputs/, fetched 2026-11-15 and VERIFIED here on every run):
  procursus/InRelease  signed by the Procursus key C59F3798A305ADD7E7E6C7256430292CF9551B0E; key taken from GitHub
                       ProcursusTeam/Procursus build_misc/keyrings/procursus/memo.gpg (second channel)
  procursus/Packages.xz  sha256 must equal the one InRelease lists; every .deb in debs/ must equal its Packages SHA256
  trollstore/*         opa334/TrollStore release 2.1.1 (GitHub asset digests), opa334/ldid v2.1.5-procursus7-new (pinned sha256)
The prefix is built from the .debs only (bootstrap package set = Procursus bootstrap 1800 'bootstrap-iphoneos-arm64.tar.zst' dpkg status,
minus libkrw0 libdimentio0 shshd, plus Sileo 2.5.1 + openssh 9.7p1-1 + com.opa334.trollstorehelper 2.1.1). The bootstrap tarball's own
binaries are re-signed copies (383 of 4759 files differ from the debs only in their signature) and are NOT used.
Every arm64 Mach-O of the result passes tools/csverify.py (what our kernel checks: CD structure, page hashes, entitlement slots).

Secrets (made once, kept in secrets/, mode 0700; NOT in jbroot.tar): SSH host keys, the root/mobile password hash.
  SSH_PASSWORD=...  or  SSH_PASSWORD_FILE=path   password for root and mobile (default: random, written to secrets/ssh-password.txt)
  SSH_PUBKEY=path.pub                            authorized_keys for root and mobile (optional)
  SSH_PASSWORD_AUTH=yes|no   (default yes)       SSH_ROOT_LOGIN=yes|prohibit-password|no (default yes)
  NO_TROLLSTORE_AUTOINSTALL=1                    do not set the boot-time TrollStore install gate
Outputs: payload-jbts1{,-undo,-off,-on,-sshoff,-sshon}/, owned-jbts1.json, owned-jbts1-flags.json, build.json
usage: python3 -I build_jbts.py
"""
import gzip, hashlib, io, json, lzma, os, secrets, shutil, stat, string, subprocess, sys, tarfile, tempfile, time
from pathlib import Path

H = Path(__file__).resolve().parent
sys.path.insert(0, str(H / 'tools'))
I = H / 'inputs'
PROCURSUS_FPR = 'C59F3798A305ADD7E7E6C7256430292CF9551B0E'
PINNED = {  # GitHub release asset digests (TrollStore 2.1.1) / download hash recorded 2026-11-15 (ldid has no GitHub digest)
    'trollstore/TrollStore.tar': '144dc741d1b74d582c861e6ed504ddced3745724e216e3de79ab7d37b27d0551',
    'trollstore/com.opa334.trollstorehelper_2.1.1_iphoneos-arm64.deb': '88cad85852ef55aea41468ecc4e781415dbd3ab14543cb45f627de5907c02549',
    'trollstore/ldid': 'b6aa4018e7ef3df9ee3fee5889d162649719109521c5a2136ca98a81fccdf52a',
}
LDID_VERSION = 'v2.1.5-procursus7-new'
DROP = {'libkrw0', 'libdimentio0', 'shshd'}
MTIME = 1763164800  # 2025-11-15 00:00 UTC, fixed for a reproducible tar
sha = lambda b: hashlib.sha256(b).hexdigest()

def die(msg):
    print('BUILD_JBTS_FAILED', msg); sys.exit(1)

# ---------------------------------------------------------------- verification
def verify_inputs():
    g = tempfile.mkdtemp(prefix='jbts-gpg-')
    try:
        env = dict(os.environ, GNUPGHOME=g)
        subprocess.run(['gpg', '--batch', '--quiet', '--import', str(I / 'procursus/memo-github.gpg')], env=env, check=True, capture_output=True)
        st = Path(g) / 'status'
        r = subprocess.run(['gpg', '--batch', '--status-file', str(st), '--decrypt', str(I / 'procursus/InRelease')], env=env, capture_output=True)
        if r.returncode != 0 or ('[GNUPG:] VALIDSIG ' + PROCURSUS_FPR) not in st.read_text():
            die('InRelease signature not valid for ' + PROCURSUS_FPR)
        rel = r.stdout.decode()
    finally:
        shutil.rmtree(g, ignore_errors=True)
    pxz = (I / 'procursus/Packages.xz').read_bytes()
    want = [l.split()[0] for l in rel.split('SHA256:')[1].split('\n') if l.strip().endswith(' main/binary-iphoneos-arm64/Packages.xz')]
    if not want or want[0] != sha(pxz): die('Packages.xz hash differs from InRelease')
    idx = {}
    for s in lzma.decompress(pxz).decode().split('\n\n'):
        d = {}
        for l in s.split('\n'):
            if l and l[0] not in ' \t' and ':' in l: k, _, v = l.partition(':'); d[k] = v.strip()
        if 'Package' in d: idx[(d['Package'], d['Version'])] = d
    for rel_path, h in PINNED.items():
        if sha((I / rel_path).read_bytes()) != h: die(rel_path + ' sha256 differs from the pinned release value')
    return idx

# ---------------------------------------------------------------- deb handling
import debtool  # noqa: E402  (pure python ar/tar reader)

def deb_members(path):
    c, d = debtool.deb_tars(str(path))
    ctl = {debtool.norm(m.name): c.extractfile(m).read() for m in c.getmembers() if m.isfile()}
    return ctl, d

def stanza(ctl_text):
    lines = [l for l in ctl_text.decode().split('\n') if l.strip()]
    return lines

def strip_firmware(lines):
    # HakiOS mk-jb.sh: Sileo's private apt index calls the runtime 'firmware' package "not installable" -> drop it from Depends of
    # installed records (the dependency only says "Darwin is new enough", true by construction on iOS 16.7).
    out = []
    for l in lines:
        if l.startswith('Depends:'):
            groups = [g.strip() for g in l[8:].split(',')]
            keep = [g for g in groups if not g.split('|')[0].strip().split('(')[0].strip() == 'firmware']
            if not keep: continue
            l = 'Depends: ' + ', '.join(keep)
        out.append(l)
    return out

class Tree:
    """In-memory tar image of /var/jb: path -> (TarInfo, data)."""
    def __init__(self): self.e = {}
    def add_dir(self, path, mode=0o755, uid=0, gid=0):
        if path in self.e: return
        parent = path.rsplit('/', 1)[0]
        if '/' in path and parent not in self.e: self.add_dir(parent)
        t = tarfile.TarInfo(path); t.type = tarfile.DIRTYPE; t.mode = mode; t.uid = uid; t.gid = gid; t.mtime = MTIME
        self.e[path] = (t, None)
    def add_file(self, path, data, mode=0o644, uid=0, gid=0):
        self.add_dir(path.rsplit('/', 1)[0])
        t = tarfile.TarInfo(path); t.size = len(data); t.mode = mode; t.uid = uid; t.gid = gid; t.mtime = MTIME
        self.e[path] = (t, data)
    def add_link(self, path, target):
        self.add_dir(path.rsplit('/', 1)[0])
        t = tarfile.TarInfo(path); t.type = tarfile.SYMTYPE; t.linkname = target; t.mode = 0o777; t.mtime = MTIME
        self.e[path] = (t, None)
    def write(self, out):
        with tarfile.open(out, 'w', format=tarfile.PAX_FORMAT) as tf:
            for p in sorted(self.e):
                t, d = self.e[p]
                t.uname = t.gname = ''
                tf.addfile(t, io.BytesIO(d) if d is not None else None)

def unpack_deb(tree, path, pkgname, conflicts):
    ctl, d = deb_members(path)
    names = []
    hard = []
    for m in d.getmembers():
        n = debtool.norm(m.name)
        if not n or n in ('var', '.'): continue
        if not (n == 'var/jb' or n.startswith('var/jb/')): die('%s ships %s outside /var/jb' % (pkgname, n))
        if '..' in n.split('/'): die('bad path ' + n)
        names.append('/' + n)
        if m.isdir():
            if n not in tree.e: tree.add_dir(n, m.mode & 0o7777, m.uid, m.gid)
        elif m.isfile():
            if n in tree.e and tree.e[n][1] is not None: conflicts.append((pkgname, n))
            tree.add_file(n, d.extractfile(m).read(), m.mode & 0o7777, m.uid, m.gid)
        elif m.issym():
            if n in tree.e: conflicts.append((pkgname, n))
            tree.add_link(n, m.linkname)
        elif m.islnk():
            hard.append((n, debtool.norm(m.linkname), m))
        else:
            die('%s: unsupported member type %s' % (pkgname, n))
    for n, target, m in hard:   # HFS+ hard links from Linux are avoided: plain copies
        t, data = tree.e[target]; tree.add_file(n, data, m.mode & 0o7777, m.uid, m.gid)
    return ctl, names

# ---------------------------------------------------------------- main build
def main():
    t0 = time.time()
    idx = verify_inputs()
    print('VERIFY procursus InRelease signature OK (%s), Packages.xz OK, TrollStore/ldid pinned hashes OK' % PROCURSUS_FPR)
    boot = [e for e in json.loads((I / 'procursus/bootstrap-packages.json').read_text()) if e['package'] not in DROP]
    added = json.loads((I / 'procursus/added-packages.json').read_text())
    pkgs = []
    for e in boot + added:
        d = idx.get((e['package'], e['version']))
        if not d: die('package %s %s not in the signed index' % (e['package'], e['version']))
        p = I / 'debs' / os.path.basename(d['Filename'])
        if sha(p.read_bytes()) != d['SHA256']: die(p.name + ' differs from its signed Packages SHA256')
        pkgs.append((e['package'], p))
    pkgs.append(('com.opa334.trollstorehelper', I / 'trollstore/com.opa334.trollstorehelper_2.1.1_iphoneos-arm64.deb'))
    print('VERIFY %d debs match the signed index (+ trollstorehelper pinned)' % (len(pkgs) - 1))

    tree = Tree(); tree.add_dir('var', 0o755); tree.add_dir('var/jb', 0o755)
    conflicts = []; status = []
    for name, p in pkgs:
        ctl, names = unpack_deb(tree, p, name, conflicts)
        lines = stanza(ctl['control'])
        if lines[0].split(':', 1)[1].strip() != name: die('control Package mismatch ' + name)
        status.append('\n'.join(strip_firmware(lines) + ['Status: install ok installed']))
        info = 'var/jb/Library/dpkg/info/' + name
        dirs = set()
        for n in names:
            parts = n.split('/')
            for i in range(2, len(parts)): dirs.add('/'.join(parts[:i]))
        lst = ['/.'] + sorted(set(names) | dirs)
        tree.add_file(info + '.list', ('\n'.join(lst) + '\n').encode())
        for k, v in ctl.items():
            if k == 'control': continue
            mode = 0o755 if k in ('preinst', 'postinst', 'prerm', 'postrm', 'extrainst_', 'config') else 0o644
            tree.add_file(info + '.' + k, v, mode)
    if conflicts: print('NOTE file overlaps (later package wins):', conflicts[:10], len(conflicts))
    dp = 'var/jb/Library/dpkg/'
    tree.add_file(dp + 'status', ('\n\n'.join(status) + '\n\n').encode())
    tree.add_file(dp + 'available', b'')
    for dd in ('alternatives', 'parts', 'updates', 'triggers'): tree.add_dir(dp + dd)
    tree.add_file('var/jb/.procursus_strapped', b'')
    # Procursus bootstrap's own non-package files (same text), HakiOS' apt CA setting
    tree.add_file('var/jb/etc/apt/sources.list.d/procursus.sources',
                  b'Types: deb\nURIs: https://apt.procurs.us/\nSuites: 1800\nComponents: main\n\n')
    tree.add_file('var/jb/etc/apt/preferences.d/procursus', b'Package: *\nPin: release o=Procursus\nPin-Priority: 1001\n\n'
                  b'Package: *\nPin: release o=Bingner/Elucubratus\nPin-Priority: -10\n\nPackage: *\nPin: release o=Cydia/Telesphoreo\n'
                  b'Pin-Priority: -10\n\nPackage: * \nPin: release o="Chimera Repo"  \nPin-Priority: -10\n')
    tree.add_file('var/jb/etc/apt/apt.conf.d/10nord2.conf', b'// nord2 jbts1 (after HakiOS mk-jb.sh): apt trusts the prefix CA bundle directly\n'
                  b'Acquire::https::CaInfo "/var/jb/etc/ssl/certs/cacert.pem";\n')
    if 'var/jb/etc/ssl/certs/cacert.pem' not in tree.e: die('no cacert.pem in the prefix')
    # sshd privilege separation user: Procursus sshd asks for "sshd" (master.passwd only has _sshd), and the empty chroot dir
    mp = tree.e['var/jb/etc/master.passwd'][1].decode()
    if '\nsshd:' not in mp:
        mp = mp.rstrip('\n') + '\nsshd:*:75:75::0:0:sshd Privilege separation (nord2 jbts1 alias of _sshd):/var/jb/var/empty:/var/jb/usr/bin/false\n'
    t, _ = tree.e['var/jb/etc/master.passwd']; tree.add_file('var/jb/etc/master.passwd', mp.encode(), t.mode, t.uid, t.gid)
    for p, mode, uid in (('var/jb/var/empty', 0o755, 0), ('var/jb/var/root', 0o700, 0), ('var/jb/var/mobile', 0o755, 501)):
        tree.add_dir(p); t, _ = tree.e[p]; t.mode = mode; t.uid = uid; t.gid = uid
    for p in ('var/jb/var/run/nord2-jb', 'var/jb/var/log', 'var/jb/etc/nord2-jb', 'var/jb/etc/ssh'): tree.add_dir(p)
    # our files
    dev = H / 'device'
    tree.add_file('var/jb/usr/libexec/nord2-jb/jbinit.sh', (dev / 'jbinit.sh').read_bytes(), 0o755)
    tree.add_file('var/jb/usr/libexec/nord2-jb/sshd-run.sh', (dev / 'sshd-run.sh').read_bytes(), 0o755)
    tree.add_file('var/jb/usr/bin/nord2-jb', (dev / 'nord2-jb').read_bytes(), 0o755)
    tree.add_file('var/jb/usr/share/nord2-jb/TrollStore.tar', (I / 'trollstore/TrollStore.tar').read_bytes())
    tree.add_file('var/jb/usr/share/nord2-jb/ldid', (I / 'trollstore/ldid').read_bytes(), 0o755)
    tree.add_file('var/jb/usr/share/nord2-jb/ldid.version', LDID_VERSION.encode())
    tree.add_file('var/jb/usr/share/nord2-jb/SETUP.md', (H / 'device/SETUP-jbts1.md').read_bytes())
    # Mach-O signature model over everything (arm64 slices)
    import csverify
    bad = []; n = 0
    for p, (t, d) in tree.e.items():
        if d is None or len(d) < 32: continue
        sl = csverify.slices(d)
        if not sl: continue
        for ct, st, mb in sl:
            if ct != 0x0100000c or mb[:4] != b'\xcf\xfa\xed\xfe': continue
            import struct
            if struct.unpack('<I', mb[12:16])[0] not in (2, 6, 8): continue
            r = csverify.check(mb); n += 1
            if not r.startswith('OK'): bad.append((p, r))
    if bad: die('signature model failures: %r' % bad[:20])
    print('CSVERIFY %d arm64 slices OK' % n)
    cpu = {}
    for p, (t, d) in tree.e.items():
        if d and d[:4] in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
            sl = csverify.slices(d) or []
            if not any(ct == 0x0100000c and (st & 0xff) in (0, 1) for ct, st, _ in sl): cpu[p] = [hex(st) for _, st, _ in sl]
    if cpu: die('Mach-O without a plain arm64 slice (A11 has no arm64e): %r' % list(cpu.items())[:10])
    suid = sorted(p for p, (t, d) in tree.e.items() if d is not None and t.mode & 0o6000)
    print('SETID', suid)

    # secrets
    sec = H / 'secrets'; sec.mkdir(mode=0o700, exist_ok=True); os.chmod(sec, 0o700)
    for kt, extra in (('ed25519', []), ('ecdsa', ['-b', '256']), ('rsa', ['-b', '3072'])):
        k = sec / ('ssh_host_%s_key' % kt)
        if not k.exists():
            subprocess.run(['ssh-keygen', '-q', '-t', kt] + extra + ['-N', '', '-C', 'nord2-jbts1', '-f', str(k)], check=True)
    pwf = os.environ.get('SSH_PASSWORD_FILE')
    pw = os.environ.get('SSH_PASSWORD') or (Path(pwf).read_text().strip() if pwf else None)
    if not pw:
        f = sec / 'ssh-password.txt'
        if not f.exists():
            a = string.ascii_letters + string.digits
            f.write_text(''.join(secrets.choice(a) for _ in range(16)) + '\n'); os.chmod(f, 0o600)
        pw = f.read_text().strip(); pw_src = str(f)
    else:
        pw_src = 'SSH_PASSWORD' if os.environ.get('SSH_PASSWORD') else pwf
    hp = subprocess.run(['openssl', 'passwd', '-6', '-stdin'], input=pw.encode(), capture_output=True, check=True).stdout.decode().strip()
    if not hp.startswith('$6$'): die('openssl passwd -6 failed')
    pub = os.environ.get('SSH_PUBKEY'); ak = Path(pub).read_text() if pub else ''
    pa = os.environ.get('SSH_PASSWORD_AUTH', 'yes'); rl = os.environ.get('SSH_ROOT_LOGIN', 'yes')
    if pa not in ('yes', 'no') or rl not in ('yes', 'prohibit-password', 'no'): die('bad SSH_PASSWORD_AUTH / SSH_ROOT_LOGIN')
    if pa == 'no' and not ak: die('SSH_PASSWORD_AUTH=no needs SSH_PUBKEY (no way in otherwise)')
    sshd_config = '\n'.join([
        '# nord2 jbts1 sshd_config (written by the installer; edit here, then "nord2-jb ssh on" to restart)',
        'Port 22', 'ListenAddress 0.0.0.0', 'ListenAddress ::',
        'HostKey /var/jb/etc/ssh/ssh_host_ed25519_key', 'HostKey /var/jb/etc/ssh/ssh_host_ecdsa_key', 'HostKey /var/jb/etc/ssh/ssh_host_rsa_key',
        'PermitRootLogin ' + rl, 'PasswordAuthentication ' + pa, 'KbdInteractiveAuthentication no', 'PubkeyAuthentication yes',
        'PermitEmptyPasswords no', 'AuthorizedKeysFile .ssh/authorized_keys',
        '# PAM off: Procursus pam_unix asks Darwin for users, the prefix users live in /var/jb/etc/spwd.db (HakiOS rc.jobs.ssh/users)',
        'UsePAM no', 'UseDNS no', 'PrintMotd no', 'MaxAuthTries 6', 'LoginGraceTime 60', 'PidFile /var/jb/var/run/sshd.pid',
        'Subsystem sftp /var/jb/usr/libexec/sftp-server', ''])
    tree.add_file('var/jb/etc/ssh/sshd_config', sshd_config.encode())

    # write payloads
    out_tar = H / 'jbroot-jbts1.tar'
    tree.write(out_tar)
    tsha = sha(out_tar.read_bytes())
    jobs = {
        'com.nord2.jb.init': {'Label': 'com.nord2.jb.init', 'UserName': 'root', 'RunAtLoad': True, 'ProcessType': 'Interactive',
                              'EnablePressuredExit': False, 'JetsamProperties': {'JetsamPriority': 140, 'JetsamMemoryLimit': 2048},
                              'ProgramArguments': ['/var/jb/bin/sh', '/var/jb/usr/libexec/nord2-jb/jbinit.sh'],
                              'StandardOutPath': '/private/var/jb/var/log/nord2-jb.log', 'StandardErrorPath': '/private/var/jb/var/log/nord2-jb.log'},
        'com.nord2.jb.sshd': {'Label': 'com.nord2.jb.sshd', 'UserName': 'root', 'RunAtLoad': True, 'KeepAlive': {'SuccessfulExit': False},
                              'ThrottleInterval': 15, 'ProcessType': 'Interactive', 'EnablePressuredExit': False,
                              'JetsamProperties': {'JetsamPriority': 140, 'JetsamMemoryLimit': 2048},
                              'ProgramArguments': ['/var/jb/bin/sh', '/var/jb/usr/libexec/nord2-jb/sshd-run.sh'],
                              'StandardErrorPath': '/private/var/jb/var/log/sshd-job.log'},
    }
    base = {'payload': 'jbts1', 'tar': 'jbroot.tar', 'tar_sha256': tsha, 'jobs': jobs, 'built': time.strftime('%Y-%m-%dT%H:%M:%S'),
            'packages': [n for n, _ in pkgs]}
    def make(name, plan, files):
        d = H / ('payload-' + name)
        if d.exists(): shutil.rmtree(d)
        d.mkdir()
        for dst, src in files.items():
            os.link(src, d / dst) if Path(src).stat().st_dev == d.stat().st_dev else shutil.copy2(src, d / dst)
        (d / 'plan.json').write_text(json.dumps(plan, indent=1) + '\n')
        return d
    sshdir = {}
    for kt in ('ed25519', 'ecdsa', 'rsa'):
        sshdir['ssh_host_%s_key' % kt] = sec / ('ssh_host_%s_key' % kt); sshdir['ssh_host_%s_key.pub' % kt] = sec / ('ssh_host_%s_key.pub' % kt)
    (sec / 'settings.json').write_text(json.dumps({'password_sha512crypt': hp, 'authorized_keys': ak}) + '\n'); os.chmod(sec / 'settings.json', 0o600)
    files = dict(sshdir); files['jbroot.tar'] = out_tar; files['ssh-settings.json'] = sec / 'settings.json'
    plan = dict(base, mode='install', flags_set=[] if os.environ.get('NO_TROLLSTORE_AUTOINSTALL') else ['install-trollstore'],
                files={k: sha(Path(v).read_bytes()) for k, v in files.items()})
    pd = make('jbts1', plan, files)
    for f in pd.iterdir():
        if f.name.startswith('ssh_host_') and not f.name.endswith('.pub') or f.name == 'ssh-settings.json': os.chmod(f, 0o600)
    make('jbts1-undo', dict(base, mode='undo', remove_trollstore=True), {})
    for nm, fs, fc in (('off', ['disabled'], []), ('on', [], ['disabled', 'ssh-disabled']),
                       ('sshoff', ['ssh-disabled'], []), ('sshon', [], ['ssh-disabled'])):
        make('jbts1-' + nm, dict(base, mode='flags', flags_set=fs, flags_clear=fc), {})
    owned = sorted({'/private/' + p for p in tree.e if p != 'var'} | {
        '/System/Library/xpc/launchd.plist', '/private/var/db/nord2-metal/receipt.json', '/private/var/db/nord2-jbts.json',
        '/private/var/mobile/Library/Preferences/com.apple.springboard.plist',
        '/private/var/jb/etc/ssh/ssh_host_ed25519_key', '/private/var/jb/etc/ssh/ssh_host_ecdsa_key', '/private/var/jb/etc/ssh/ssh_host_rsa_key',
        '/private/var/jb/etc/ssh/ssh_host_ed25519_key.pub', '/private/var/jb/etc/ssh/ssh_host_ecdsa_key.pub', '/private/var/jb/etc/ssh/ssh_host_rsa_key.pub',
        '/private/var/jb/var/root/.ssh', '/private/var/jb/var/root/.ssh/authorized_keys',
        '/private/var/jb/var/mobile/.ssh', '/private/var/jb/var/mobile/.ssh/authorized_keys',
        '/private/var/jb/etc/nord2-jb/install-trollstore', '/private/var/jb.nord2tmp'})
    (H / 'owned-jbts1.json').write_text(json.dumps(owned, indent=0) + '\n')
    (H / 'owned-jbts1-flags.json').write_text(json.dumps(['/private/var/jb/etc/nord2-jb'] + ['/private/var/jb/etc/nord2-jb/' + f for f in
                                              ('disabled', 'ssh-disabled', 'install-trollstore')], indent=0) + '\n')
    res = {'tar_sha256': tsha, 'tar_bytes': out_tar.stat().st_size, 'entries': len(tree.e), 'packages': len(pkgs), 'csverify_slices': n,
           'setid': suid, 'password_source': pw_src, 'password_auth': pa, 'root_login': rl, 'authorized_keys': bool(ak),
           'owned_paths': len(owned), 'seconds': round(time.time() - t0, 1)}
    (H / 'build.json').write_text(json.dumps(res, indent=1) + '\n')
    print('BUILD_JBTS_OK', json.dumps(res))

main()
