#!/usr/bin/env python3
"""Apply the approved addon to a separate verified snapshot clone, never a phone."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import tarfile

ADDON_SHA = 'dece831149851e15bf557d582f575cc4464389a03f13ff0ccbefd6389437ce1c'


def digest(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--main', required=True, type=Path)
    ap.add_argument('--snapshot', required=True, type=Path)
    ap.add_argument('--addon', required=True, type=Path)
    ap.add_argument('--journal-oracle', required=True, type=Path)
    ap.add_argument('--output', required=True, type=Path)
    a = ap.parse_args()
    record = json.loads((a.snapshot.parent / 'verified.json').read_text())
    assert record['independent_full_hash_matches'] and digest(a.snapshot) == record['sha256']
    assert digest(a.addon) == ADDON_SHA
    a.output.mkdir(exist_ok=False)
    clone = (a.output / 'metal-rootfs.img').resolve()
    subprocess.run(['cp', '--reflink=auto', '--sparse=always', '--', str(a.snapshot), str(clone)], check=True)
    clone.chmod(0o600)
    subprocess.run(['python3', str(Path(__file__).with_name('replay_hfs_journal.py')),
                    str(clone), '--oracle', str(a.journal_oracle),
                    '--receipt', str(a.output/'journal-replay.json'), '--apply'], check=True)
    payload = (a.output / 'inputs').resolve()
    payload.mkdir()
    with tarfile.open(a.addon) as tf:
        tf.extractall(payload, filter='data')
    root = a.main / 'research/nord2/rootfs-vm/initramfs'
    init = (root / 'init').read_bytes()
    script = r'''#!/bin/bash
set -euo pipefail
mount -t 9p -o trans=virtio,version=9p2000.L,ro,cache=loose,msize=262144 inputs /src
echo METAL_CLONE_FSCK_BEGIN
set +e
fsck.hfsplus -f -y /dev/vda
rc=$?
set -e
echo METAL_CLONE_FSCK_RESULT=$rc
fsck.hfsplus -f -n /dev/vda
mount -t hfsplus -o rw,force /dev/vda /work/root
python3 - <<'PY'
from pathlib import Path
import hashlib, json, plistlib, sys
sys.path.insert(0, '/src')
import stage_metal_root as s
r=Path('/work/root'); payload=Path('/src/payload')
s.validate_payload(payload)
before=plistlib.loads((r/s.LAUNCH).read_bytes())['LaunchDaemons']
keep={}
for rel in ['sbin/launchd','usr/lib/dyld','System/Library/Audio/Plug-Ins/HAL',
            'Library/Audio/Plug-Ins/HAL','private/var/db/nord2-audio']:
    p=r/rel
    if p.is_file(): keep[rel]=s.sha(p)
    elif p.is_dir():
        for f in sorted(p.rglob('*')):
            if f.is_file() and not f.is_symlink(): keep[str(f.relative_to(r))]=s.sha(f)
assert len(keep)>2, 'Missing existing audio files'
s.apply(r,payload)
after=plistlib.loads((r/s.LAUNCH).read_bytes())['LaunchDaemons']
assert len(after)==len(before)+1 and all(after[k]==v for k,v in before.items())
assert all(s.sha(r/p)==v for p,v in keep.items())
receipt=json.loads((r/s.STATE/'receipt.json').read_text())
assert receipt['phase']=='committed'
for rel, expected in receipt['inventory'].items(): assert s.inventory(r/rel)==expected
print('METAL_INSTALL_VERIFIED '+json.dumps({'old_jobs':len(before),'new_jobs':len(after),
      'preserved_existing_files':len(keep),'original_launchd':receipt['original_launchd_sha256'],
      'installed_launchd':receipt['installed_launchd_sha256']}))
PY
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo METAL_CLONE_READY
'''.encode()
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
    command = ['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','2048','-smp','2',
               '-nodefaults','-no-reboot','-display','none','-serial','stdio','-monitor','none',
               '-kernel','/usr/lib/modules/7.2.9-arch1-1/vmlinuz','-initrd',str(initrd),
               '-append','console=ttyS0 rdinit=/init panic=-1 loglevel=4',
               '-drive',f'file={clone},format=raw,if=virtio',
               '-virtfs',f'local,path={payload},mount_tag=inputs,security_model=none,readonly=on']
    (a.output/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (a.output/'vm.log').open('wb') as out:
        subprocess.run(command,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=1200)
    log=(a.output/'vm.log').read_text()
    print(log[-6000:])
    assert 'ROOTFS_VM_COMPLETE' in log and 'ROOTFS_VM_FAILED' not in log and 'METAL_CLONE_READY' in log
    assert digest(a.snapshot)==record['sha256'], 'Original snapshot mutated'
    result={'before':record['sha256'],'after':digest(clone),'image':str(clone),
            'addon_sha256':ADDON_SHA,'bytes':clone.stat().st_size,'offline_verified':True}
    (a.output/'verified.json').write_text(json.dumps(result,indent=2)+'\n')
    clone.chmod(0o444)
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
