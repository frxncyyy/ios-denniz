from pathlib import Path
import subprocess,os,stat,json,struct,gzip,shutil,sys
base=Path(__file__).resolve().parent;root=base/'initramfs';mode=sys.argv[1]
assert mode in ('smoke','build','finish','verify')
(root/'lib64').mkdir(exist_ok=True);shutil.copy2('/lib64/ld-linux-x86-64.so.2',root/'lib64/ld-linux-x86-64.so.2')
mods=json.loads((base/'modules.json').read_text())
init='''#!/bin/busybox sh
/bin/busybox mkdir -p /bbbin /proc /sys /dev /src /work/root
/bin/busybox --install -s /bbbin
export PATH=/bin:/usr/bin:/bbbin
mount -t devtmpfs devtmpfs /dev
exec </dev/console >/dev/console 2>&1
mount -t proc proc /proc
ln -s /proc/self/fd /dev/fd
ln -s /proc/self/fd/0 /dev/stdin
ln -s /proc/self/fd/1 /dev/stdout
ln -s /proc/self/fd/2 /dev/stderr
mount -t sysfs sysfs /sys
'''
init+='\n'.join('insmod /modules/'+m+' || poweroff -f' for m in mods)+'\n'
init+='''if /bin/bash /build/run.sh; then
 echo ROOTFS_VM_COMPLETE
else
 echo ROOTFS_VM_FAILED
fi
sync
poweroff -f
'''
(root/'init').write_text(init);(root/'init').chmod(0o755)
script='''#!/bin/bash
set -euo pipefail
mount -t 9p -o trans=virtio,version=9p2000.L,ro,cache=loose,msize=262144 inputs /src
mkfs.hfsplus -s -J -v iOSRoot /dev/vda
mount -t hfsplus -o rw,force /dev/vda /work/root
python3 -c 'import plistlib; print("PYTHON_READY")'
'''
if mode=='smoke':
 script+='''cp -dR --preserve=mode,ownership,timestamps,links /src/OS/root/sbin/launchd /work/root/
sha256sum /work/root/launchd
stat -c '%u:%g %a %s' /work/root/launchd
umount /work/root
fsck.hfsplus -f -y /dev/vda || true
fsck.hfsplus -f -n /dev/vda
'''
else:
 script+='''echo COPYING_OS
tar --numeric-owner --same-owner --same-permissions -xpf /src/OS.tar -C /work/root
mkdir -p /work/root/private/preboot/Cryptexes/OS /work/root/private/preboot/Cryptexes/App
echo COPYING_SYSTEMOS
tar --numeric-owner --same-owner --same-permissions -xpf /src/SystemOS.tar -C /work/root/private/preboot/Cryptexes/OS
echo COPYING_APPOS
tar --numeric-owner --same-owner --same-permissions -xpf /src/AppOS.tar -C /work/root/private/preboot/Cryptexes/App
/bin/bash /build/finish-base.sh
python3 /build/sweet-root-tweaks.py /work/root
sha256sum /work/root/sbin/launchd /work/root/usr/lib/dyld
stat -c '%u:%g %a %s' /work/root/sbin/launchd /work/root/usr/lib/dyld
python3 - <<'VERIFY'
import os,glob,plistlib
r='/work/root'
a=r+'/System/Library/Caches/com.apple.dyld'
b=r+'/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld'
files=glob.glob(b+'/*');assert len(files)==44
assert all(os.stat(p).st_ino==os.stat(a+'/'+os.path.basename(p)).st_ino for p in files)
for sub in ('diagnostics','uuidtext','diagnostics/timesync'):
 p=r+'/private/var/db/'+sub+'/.migrated';assert os.stat(p).st_uid==272
with open(r+'/System/Library/CoreServices/SystemVersion.plist','rb') as f:
 v=plistlib.load(f);assert v['ProductVersion']=='16.7.16' and v['ProductBuildVersion']=='20H392'
print('ROOT_CONTENT_CHECKS_PASS')
VERIFY
df -h /work/root
umount /work/root
fsck.hfsplus -f -y /dev/vda || true
fsck.hfsplus -f -n /dev/vda
'''
if mode=='finish':
 script=script.replace('mkfs.hfsplus -s -J -v iOSRoot /dev/vda\n','fsck.hfsplus -f -y /dev/vda || true\nfsck.hfsplus -f -n /dev/vda\n')
 a=script.index('echo COPYING_OS');z=script.index('/bin/bash /build/finish-base.sh')
 script=script[:a]+script[z:]
if mode=='verify':
 script=(base/'verify-content.sh').read_text()
(root/'build/run.sh').write_text(script)
# Write newc archive with root ownership; no host chown/mknod required.
archive=bytearray();ino=0
def add(name,mode,data=b'',rdevmajor=0,rdevminor=0):
 global ino
 ino+=1;nb=name.encode()+b'\0'
 fields=[ino,mode,0,0,2 if stat.S_ISDIR(mode) else 1,0,len(data),0,0,rdevmajor,rdevminor,len(nb),0]
 archive.extend(b'070701'+b''.join(f'{x:08x}'.encode() for x in fields)+nb)
 archive.extend(bytes((-len(archive))%4));archive.extend(data);archive.extend(bytes((-len(archive))%4))
for p in sorted(root.rglob('*')):
 st=p.lstat();data=os.readlink(p).encode() if p.is_symlink() else p.read_bytes() if p.is_file() else b''
 add(str(p.relative_to(root)),st.st_mode,data)
add('dev',stat.S_IFDIR|0o755);add('dev/console',stat.S_IFCHR|0o600,rdevmajor=5,rdevminor=1)
add('TRAILER!!!',0)
initrd=base/(mode+'-initrd.gz');initrd.write_bytes(gzip.compress(archive,compresslevel=1,mtime=0))
image=base/('smoke-hfs.img' if mode=='smoke' else 'ios16-root-20H392-nord2-v1.img')
if mode in ('finish','verify'):
 assert image.is_file() and image.stat().st_size==16<<30
 with image.open('rb') as f:f.seek(1024);assert f.read(2)==b'HX'
else:
 with image.open('xb') as f:f.truncate((128<<20) if mode=='smoke' else (16<<30))
cmd=['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','2048','-smp','2','-nodefaults','-no-reboot','-display','none','-serial','stdio','-monitor','none','-kernel','/usr/lib/modules/7.2.9-arch1-1/vmlinuz','-initrd',str(initrd),'-append','console=ttyS0 rdinit=/init panic=-1 loglevel=4','-drive',f'file={image},format=raw,if=virtio'+(',readonly=on' if mode=='verify' else ''),'-virtfs',f'local,path={(Path("research/nord2/ipsw/root-images/mounts").resolve() if mode=="smoke" else base/"archives")},mount_tag=inputs,security_model=none,readonly=on']
(base/(mode+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
with (base/(mode+'.log')).open('w') as log:
 p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=1800)
text=(base/(mode+'.log')).read_text(errors='replace');print(text[-6000:]);assert p.returncode==0 and 'ROOTFS_VM_COMPLETE' in text and 'ROOTFS_VM_FAILED' not in text
print('VM completed:',image)
