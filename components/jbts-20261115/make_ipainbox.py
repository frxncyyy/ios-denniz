#!/usr/bin/env python3
"""Make an ipainbox1 payload (host only): payload-ipainbox-NAME/{plan.json, ipainbox.sh, ipas/*.ipa (hard links)}.
usage: python3 -I make_ipainbox.py NAME [--apps-undo] [--ids-only] IPA...
  --ids-only   do not queue the IPAs: only record their bundle ids for ipainbox.sh's registration check (repair payload)
  --apps-undo  also remove /Applications/Terraria.app + /Applications/SubwaySurf.app installed by apps-20261104/payload (checked
               by bundle id + main executable sha256 of the IPAs that put them there: apps payload / payload-ssclean1),
               so TrollStore can install the same ids.
Each IPA must be a zip with exactly one Payload/<X>.app/Info.plist; its id and executable are printed. Encrypted main binaries
(LC_ENCRYPTION_INFO cryptid != 0) are refused here already (TrollStore would return 180)."""
import hashlib, io, json, os, plistlib, shutil, struct, sys, zipfile
from pathlib import Path
H = Path(__file__).resolve().parent
APPS = Path(__file__).resolve().parents[1] / 'apps-20261104/payload'
JOB = {'Label': 'com.nord2.jb.ipainbox', 'UserName': 'root', 'RunAtLoad': True, 'WatchPaths': ['/private/var/mobile/Documents/ipa-inbox'],
       'ThrottleInterval': 10, 'ProcessType': 'Interactive', 'EnablePressuredExit': False,
       'JetsamProperties': {'JetsamPriority': 140, 'JetsamMemoryLimit': 3072},
       'ProgramArguments': ['/var/jb/bin/sh', '/var/jb/usr/libexec/nord2-jb/ipainbox.sh'],
       'StandardOutPath': '/private/var/jb/var/log/ipainbox-job.log', 'StandardErrorPath': '/private/var/jb/var/log/ipainbox-job.log'}

def fsha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()

def ipa_info(p):
    z = zipfile.ZipFile(p)
    infos = [n for n in z.namelist() if n.count('/') == 2 and n.startswith('Payload/') and n.endswith('.app/Info.plist')]
    if len(infos) != 1: sys.exit('%s: expected one Payload/X.app/Info.plist, found %r' % (p, infos))
    ip = plistlib.loads(z.read(infos[0])); app = infos[0].split('/')[1]
    exe = z.read('Payload/%s/%s' % (app, ip['CFBundleExecutable']))
    return app, ip['CFBundleIdentifier'], exe

def cryptids(b):
    out = []
    def one(mb):
        if mb[:4] != b'\xcf\xfa\xed\xfe': return
        n = struct.unpack('<I', mb[16:20])[0]; q = 32
        for _ in range(n):
            c, s = struct.unpack('<II', mb[q:q + 8])
            if c == 0x2c: out.append(struct.unpack('<I', mb[q + 16:q + 20])[0])
            q += s
    if b[:4] == b'\xca\xfe\xba\xbe':
        for i in range(struct.unpack('>I', b[4:8])[0]):
            _, _, off, size, _ = struct.unpack('>iiIII', b[8 + 20 * i:28 + 20 * i]); one(b[off:off + size])
    else: one(b)
    return out

def main():
    a = sys.argv[1:]
    if not a: sys.exit(__doc__)
    name = a.pop(0); undo = '--apps-undo' in a; idsonly = '--ids-only' in a; ipas = [Path(x).resolve() for x in a if not x.startswith('--')]
    d = H / ('payload-ipainbox-' + name)
    if d.exists(): sys.exit('%s exists: pick a new NAME' % d)
    plan = {'mode': 'ipainbox', 'name': name, 'job': JOB, 'ipas': [], 'apps_undo': [], 'ids': []}
    (d / 'ipas').mkdir(parents=True)
    for p in ipas:
        app, bid, exe = ipa_info(p)
        if idsonly:
            plan['ids'].append({'bundle': app, 'bundle_id': bid}); print('ID', app, bid); continue
        if any(cryptids(exe)): sys.exit('%s: main binary is encrypted (cryptid %r): TrollStore cannot sign it' % (p, cryptids(exe)))
        dst = d / 'ipas' / p.name
        try: os.link(p, dst)
        except OSError: shutil.copy2(p, dst)
        plan['ipas'].append({'name': p.name, 'sha256': fsha(dst), 'bundle': app, 'bundle_id': bid, 'bytes': dst.stat().st_size})
        print('IPA', p.name, app, bid, dst.stat().st_size, 'B')
    if undo:
        # sources of the /Applications copies: the old apps payload (Terraria 4.5.0, Subway 3.71.0) and payload-ssclean1 (Subway
        # 3.71.10, 2026-11-13). An installed app is removed only if its main executable equals one of these.
        srcs = sorted((APPS / 'apps').glob('*.ipa')) + sorted((APPS.parent / 'payload-ssclean1/apps').glob('*.ipa'))
        ap = json.loads((APPS / 'plan.json').read_text())
        for e in ap['apps']:
            if fsha(APPS / 'apps' / e['ipa']) != e['sha256']: sys.exit('apps payload IPA %s differs from its plan' % e['ipa'])
        want = {e['bundle']: e['bundle_id'] for e in ap['apps']}
        acc = {}
        for ipa in srcs:
            app, bid, exe = ipa_info(ipa)
            if app in want and bid == want[app]:
                acc.setdefault(app, set()).add(hashlib.sha256(exe).hexdigest()); print('APPS_UNDO source', ipa.name, app, hashlib.sha256(exe).hexdigest()[:12])
        for app, bid in want.items():
            plan['apps_undo'].append({'bundle': app, 'bundle_id': bid, 'exe_sha256': sorted(acc[app])})
    shutil.copy2(H / 'device/ipainbox.sh', d / 'ipainbox.sh')
    plan['script_sha256'] = fsha(d / 'ipainbox.sh')
    (d / 'plan.json').write_text(json.dumps(plan, indent=1) + '\n')
    print('MAKE_IPAINBOX_OK', d, 'apps_undo', ','.join(x['bundle'] for x in plan['apps_undo']) or '-')

if __name__ == '__main__':
    main()
