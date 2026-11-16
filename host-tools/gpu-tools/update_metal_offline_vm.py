#!/usr/bin/env python3
"""Update a separate HFS+ clone in a VM; never mounts/contact/writes a phone.
A rehearsal snapshot is historical and MUST NOT be transferred to the device.
"""
import argparse, gzip, hashlib, json, os, stat, subprocess, tarfile
from pathlib import Path

def digest(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()

VM_SCRIPT = r'''#!/bin/bash
set -euo pipefail
mount -t 9p -o trans=virtio,version=9p2000.L,ro,cache=loose,msize=262144 inputs /src
# The phone snapshot is crash-consistent. After checked journal replay, fsck
# must publish the clean-unmount bit or Linux still mounts HFS+ read-only.
# All writes here are to the separate local clone, never the device/snapshot.
set +e
fsck.hfsplus -f -y /dev/vda
rc=$?
set -e
echo METAL_UPDATE_FSCK_RESULT=$rc
fsck.hfsplus -f -n /dev/vda
mount -t hfsplus -o rw,force /dev/vda /work/root
python3 - <<'GUEST'
from pathlib import Path
import json, os, shutil, sys
sys.path.insert(0,'/src')
import update_metal_root as u
s=u.s;r=Path('/work/root');payload=Path('/src/payload')
assert not os.statvfs(r).f_flag & os.ST_RDONLY, 'offline clone mounted read-only'
before=u.files(r);launch=(r/s.LAUNCH).read_bytes();original=(r/s.STATE/'launchd.original').read_bytes()
keep={}
for rel in ['sbin/launchd','usr/lib/dyld','System/Library/Audio/Plug-Ins/HAL','Library/Audio/Plug-Ins/HAL','private/var/db/nord2-audio']:
    p=r/rel
    if p.is_file():keep[rel]=s.sha(p)
    elif p.is_dir():
        for f in sorted(p.rglob('*')):
            if f.is_file() and not f.is_symlink():keep[str(f.relative_to(r))]=s.sha(f)
assert len(keep)>2,'missing audio files'
backup=u.update(r,payload);after=u.files(r)
if REHEARSE_VALUE:
    u.restore(r,backup)
    assert u.files(r)==before and (r/s.LAUNCH).read_bytes()==launch
    assert (r/s.STATE/'launchd.original').read_bytes()==original
    # This is only a disposable PC rehearsal. Preserve its first rollback,
    # then repeat installation so the clone also exercises a committed update.
    backup.rename(backup.with_name(backup.name+'.rehearsal-restored'))
    backup=u.update(r,payload);assert u.files(r)==after
    print('METAL_UPDATE_RESTORE_REAPPLY_PASS')
assert (r/s.LAUNCH).read_bytes()==launch and (r/s.STATE/'launchd.original').read_bytes()==original
assert all(s.sha(r/p)==v for p,v in keep.items())
receipt=json.loads((r/s.STATE/'receipt.json').read_text());assert receipt['phase']=='committed'
for rel,expected in receipt['inventory'].items():assert s.inventory(r/rel)==expected
print('METAL_UPDATE_VERIFIED '+json.dumps({'preserved_existing_files':len(keep),'backup':str(backup.relative_to(r)),'old':before,'new':after}))
prefs_path=Path('/src/ca-preferences.json')
if prefs_path.exists():
    import plistlib
    prefs=json.loads(prefs_path.read_text());assert prefs['version']==1
    applied=[]
    for f in prefs['files']:
        rel=f['path'];assert rel.startswith('private/var/') and '..' not in rel and rel.endswith('.plist')
        p=r/rel;p.parent.mkdir(parents=True,exist_ok=True)
        old=plistlib.loads(p.read_bytes()) if p.exists() else {}
        assert isinstance(old,dict)
        new=dict(old);new.update(prefs['keys'])
        p.write_bytes(plistlib.dumps(new,fmt=plistlib.FMT_BINARY));os.chown(p,f['uid'],f['gid']);os.chmod(p,f['mode'])
        assert {k:plistlib.loads(p.read_bytes())[k] for k in prefs['keys']}==prefs['keys']
        applied.append({'path':rel,'existed':bool(old),'kept_keys':len(old)})
    print('METAL_CA_PREFS_APPLIED '+json.dumps(applied))
# METAL_CACHE_DIR_BEGIN
# Shared pipeline-cache directory (frontend candidate 1).  backboardd and
# SpringBoard run as mobile (UserName in their LaunchDaemons plists) and the
# probe as root; the directory the probe once created there was root:mobile
# 0700, unusable to mobile.  Own it as mobile, 0755 (root bypasses permissions,
# nobody else needs it), and drop the unusable root-owned leftovers: entries
# are keyed by build id, so old ones can never be served anyway.
caches=r/'private/var/mobile/Library/Caches';leaf=caches/'com.tegu.metal.pipelines'
if not caches.is_dir():
    caches.mkdir(parents=True);os.chown(caches,501,501);os.chmod(caches,0o700)
if leaf.is_symlink() or (leaf.exists() and not leaf.is_dir()):
    raise SystemExit('unexpected non-directory '+str(leaf))
leaf.mkdir(exist_ok=True)
removed=0
for f in leaf.iterdir():
    if f.is_file() and not f.is_symlink() and (f.name=='misses.txt' or f.name.startswith(('.tmp-','.probe-')) or
       (f.name.startswith(('k1-','k2-')) and f.name.endswith('.tpc'))):
        f.unlink();removed+=1
os.chown(leaf,501,501);os.chmod(leaf,0o755)
st=os.stat(leaf);assert (st.st_uid,st.st_gid,st.st_mode&0o7777)==(501,501,0o755)
print('METAL_CACHE_DIR_READY '+json.dumps({'path':str(leaf.relative_to(r)),'removed_stale_entries':removed,'remaining':len(list(leaf.iterdir()))}))
# METAL_CACHE_DIR_END
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo METAL_UPDATE_READY
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('main','snapshot','addon','output'):ap.add_argument('--'+name,required=True,type=Path)
    for name in ('snapshot-sha256','addon-sha256'):ap.add_argument('--'+name,required=True)
    ap.add_argument('--rehearse',action='store_true')
    ap.add_argument('--journal-oracle',type=Path)
    ap.add_argument('--vm-kernel',type=Path,default=Path('/usr/lib/modules/7.2.9-arch1-1/vmlinuz'),
                    help='Linux kernel matching the modules in the prepared initramfs')
    a=ap.parse_args()
    if not a.vm_kernel.is_file():ap.error('matching VM kernel is missing; use --vm-kernel')
    assert digest(a.snapshot)==a.snapshot_sha256, 'snapshot hash mismatch'
    assert digest(a.addon)==a.addon_sha256, 'addon hash mismatch'
    a.output.mkdir(exist_ok=False)
    clone=(a.output/'metal-rootfs.img').resolve()
    subprocess.run(['cp','--reflink=auto','--sparse=always','--',str(a.snapshot),str(clone)],check=True)
    clone.chmod(0o600)
    if a.journal_oracle:
        subprocess.run(['python3',str(Path(__file__).with_name('replay_hfs_journal.py')),str(clone),
                        '--oracle',str(a.journal_oracle),'--receipt',str(a.output/'journal-replay.json'),'--apply'],check=True)
    payload=(a.output/'inputs').resolve();payload.mkdir()
    with tarfile.open(a.addon) as tf:tf.extractall(payload,filter='data')
    root=a.main/'research/nord2/rootfs-vm/initramfs'
    init=(root/'init').read_bytes()
    script = VM_SCRIPT.replace('REHEARSE_VALUE',str(a.rehearse)).encode()
    initrd = a.output / 'metal-initrd.gz'
    ino = 0
    offset = 0
    with gzip.open(initrd, 'wb', compresslevel=1) as stream:
        def add(name, mode, data=b'', major=0, minor=0):
            nonlocal ino, offset
            ino += 1
            name = name.encode() + b'\0'
            fields = [ino, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, 0,
                      len(data), 0, 0, major, minor, len(name), 0]
            head = b'070701' + b''.join(f'{x:08x}'.encode() for x in fields) + name
            stream.write(head); offset += len(head)
            pad = (-offset) % 4; stream.write(bytes(pad)); offset += pad
            stream.write(data); offset += len(data)
            pad = (-offset) % 4; stream.write(bytes(pad)); offset += pad
        for p in sorted(root.rglob('*')):
            rel = str(p.relative_to(root))
            data = (script if rel == 'build/run.sh' else init if rel == 'init' else
                    os.readlink(p).encode() if p.is_symlink() else p.read_bytes() if p.is_file() else b'')
            add(rel, p.lstat().st_mode, data)
        if not (root/'dev').exists(): add('dev', stat.S_IFDIR | 0o755)
        add('dev/console', stat.S_IFCHR | 0o600, major=5, minor=1)
        add('TRAILER!!!', 0)
    command=['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','2048','-smp','2',
             '-nodefaults','-no-reboot','-display','none','-serial','stdio','-monitor','none',
             '-kernel',str(a.vm_kernel.resolve()),'-initrd',str(initrd),
             '-append','console=ttyS0 rdinit=/init panic=-1 loglevel=4',
             '-drive',f'file={clone},format=raw,if=virtio',
             '-virtfs',f'local,path={payload},mount_tag=inputs,security_model=none,readonly=on']
    (a.output/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (a.output/'vm.log').open('wb') as out:
        subprocess.run(command,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=1200)
    log=(a.output/'vm.log').read_text();print(log[-5000:])
    assert 'ROOTFS_VM_COMPLETE' in log and 'ROOTFS_VM_FAILED' not in log and 'METAL_UPDATE_READY' in log and 'METAL_CACHE_DIR_READY' in log
    assert digest(a.snapshot)==a.snapshot_sha256, 'original snapshot mutated'
    result={'before':a.snapshot_sha256,'after':digest(clone),'image':str(clone),
            'addon_sha256':a.addon_sha256,'bytes':clone.stat().st_size,'offline_verified':True,
            'rehearsal':a.rehearse,'device_source_current_verified':False}
    (a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n')
    clone.chmod(0o444);print(json.dumps(result,indent=2))

if __name__=='__main__':main()
