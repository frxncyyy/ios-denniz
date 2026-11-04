#!/usr/bin/env python3
"""Build the ssclean1 payloads (clean Subway Surfers 3.71.10 replacing the iGameGod-modded 3.71.0 bundle).

Outputs in apps-20261104/: payload-ssclean1, payload-ssclean1-undo, payload-ssclean1-undoipa, payload-ssclean1-dropbackup,
owned-ssclean1.json (= owned-ssclean1-undo.json, also used by -undoipa), owned-ssclean1-dropbackup.json.
Manifests and owned lists are generated from the two IPA file lists (the same extraction rule as guest-install.sh:
directories 0755, files 0755 when they start with a Mach-O/fat magic else 0644). IPAs are hard-linked, never copied.
Never touches the mirror or a phone. usage: python3 -I make_ssclean1.py
"""
import hashlib, json, os, shutil, sys, zipfile
from pathlib import Path

A = Path(__file__).resolve().parent
DL = Path.home() / 'Downloads'
NEW_IPA, OLD_IPA = 'com.kiloo.subwaysurfers-3.71.10-Decrypted.ipa', 'Subway-Surfers-IPAOMTK.COM.ipa'
BUNDLE, BID = 'SubwaySurf.app', 'com.kiloo.subwaysurfers'
LSDIR = '/private/var/mobile/Containers/Data/InternalDaemon/E7BA914B-B80E-478B-B724-FD94EE472472/Library/Caches'
CSSTORES = [LSDIR + '/com.apple.LaunchServices-4035-v2.csstore', LSDIR + '/SystemDataOnly-com.apple.LaunchServices-4035-v2.csstore']
DB = '/private/var/db/nord2-ssclean1'
MAGIC = (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca')


def manifest(ipa):
    z = zipfile.ZipFile(ipa); root = 'Payload/' + BUNDLE + '/'; files, dirs = {}, set()
    for i in z.infolist():
        if not i.filename.startswith(root) or i.filename == root:
            continue
        rel = i.filename[len(root):]
        assert '..' not in rel.split('/') and rel.isascii()
        if i.filename.endswith('/'):
            dirs.add(rel.rstrip('/')); continue
        assert (i.external_attr >> 16) & 0o170000 != 0o120000, 'symlink in IPA: ' + rel
        data = z.read(i)
        files[rel] = [hashlib.sha256(data).hexdigest(), 0o755 if data[:4] in MAGIC else 0o644, len(data)]
        parts = rel.split('/')
        for k in range(1, len(parts)):
            dirs.add('/'.join(parts[:k]))
    return {'bundle': BUNDLE, 'files': files, 'dirs': sorted(dirs)}


def mhash(m):
    return hashlib.sha256(json.dumps(m, sort_keys=True).encode()).hexdigest()


def fsha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def alloc_mib(m):
    return round(sum(-(-v[2] // 4096) * 4096 for v in m['files'].values()) / 2**20, 1)


def tree_paths(prefix, m):
    return [prefix] + [prefix + '/' + d for d in m['dirs']] + [prefix + '/' + f for f in m['files']]


old, new = manifest(DL / OLD_IPA), manifest(DL / NEW_IPA)
assert 'Frameworks/iGameGod.framework/iGameGod' in old['files'] and not any('iGameGod' in k for k in new['files'])
common = {
    'bundle_id': BID, 'bundle': '/Applications/' + BUNDLE, 'backup': DB + '/' + BUNDLE, 'receipt': DB + '/receipt.json',
    'new': {'ipa': NEW_IPA, 'sha256': fsha(DL / NEW_IPA), 'version': '3.71.10 (97735)', 'manifest_sha256': mhash(new),
            'files': len(new['files']), 'dirs': len(new['dirs']), 'allocated_mib': alloc_mib(new)},
    'old': {'ipa': OLD_IPA, 'sha256': fsha(DL / OLD_IPA), 'version': '3.71.0 (97328) + iGameGod.framework + libSubwaySurfers.dylib',
            'manifest_sha256': mhash(old), 'files': len(old['files']), 'dirs': len(old['dirs']), 'allocated_mib': alloc_mib(old)},
    'csstores': CSSTORES, 'csstore_suffix': '.pre-ssclean1', 'margin_mib': 300,
    'signing': 'none, like guest-install.sh: both builds keep their original Apple LC_CODE_SIGNATURE (cryptid 0); SWEET AMFI (0035/0036) '
               'marks rootfs binaries CS_SIGNED|CS_PLATFORM_BINARY and sets no CS_ENFORCEMENT/CS_HARD/CS_KILL, so stale page hashes are not fatal '
               '(the installed modded app already runs with 6 bad pages in SubwaySurf and 641 in AppLovinSDK)',
}
PURPOSE = {
    'payload-ssclean1': 'ssclean1: /Applications/SubwaySurf.app = clean decrypted Subway Surfers 3.71.10 (no iGameGod, no libSubwaySurfers.dylib), '
                        'extracted like guest-install.sh (system app, root:wheel, 0755 Mach-O / 0644 other); the modded 3.71.0 bundle is renamed (not copied) '
                        'to /private/var/db/nord2-ssclean1/SubwaySurf.app for the undo; the two LaunchServices csstores are renamed *.pre-ssclean1 so LS '
                        'rescans /Applications at boot. Same bundle id: data container and preferences are kept.',
    'payload-ssclean1-undo': 'ssclean1 undo: delete the clean bundle, rename the kept modded bundle back (same CNIDs/extents: byte for byte), put the '
                             '*.pre-ssclean1 LaunchServices csstores back (the rebuilt ones are deleted), remove /private/var/db/nord2-ssclean1.',
    'payload-ssclean1-undoipa': 'ssclean1 undo after -dropbackup: delete the clean bundle and re-extract the modded IPA (hard link of the original '
                                'install file) like guest-install.sh, verify every file against the modded manifest, LS csstores as in -undo.',
    'payload-ssclean1-dropbackup': 'ssclean1 dropbackup: delete the kept modded bundle /private/var/db/nord2-ssclean1/SubwaySurf.app (frees ~409 MiB); '
                                   'afterwards only payload-ssclean1-undoipa can undo.',
}
for name, ipa in (('payload-ssclean1', NEW_IPA), ('payload-ssclean1-undo', None), ('payload-ssclean1-undoipa', OLD_IPA), ('payload-ssclean1-dropbackup', None)):
    d = A / name
    if d.exists():
        shutil.rmtree(d)
    d.mkdir()
    (d / 'old-manifest.json').write_text(json.dumps(old, sort_keys=True))
    (d / 'new-manifest.json').write_text(json.dumps(new, sort_keys=True))
    if ipa:
        (d / 'apps').mkdir(); os.link(DL / ipa, d / 'apps' / ipa)
    (d / 'plan.json').write_text(json.dumps(dict(purpose=PURPOSE[name], **common), indent=1) + '\n')

css = CSSTORES + [c + common['csstore_suffix'] for c in CSSTORES]
owned = sorted(set(tree_paths('/Applications/' + BUNDLE, old) + tree_paths('/Applications/' + BUNDLE, new)
                   + tree_paths(DB + '/' + BUNDLE, old) + [DB, DB + '/receipt.json'] + css))
for n in ('owned-ssclean1.json', 'owned-ssclean1-undo.json'):
    (A / n).write_text(json.dumps(owned, indent=1) + '\n')
(A / 'owned-ssclean1-dropbackup.json').write_text(json.dumps(sorted(tree_paths(DB + '/' + BUNDLE, old) + [DB, DB + '/receipt.json']), indent=1) + '\n')
print(json.dumps({k: common[k] for k in ('new', 'old')}, indent=1))
print('owned-ssclean1.json', len(owned), 'paths')
