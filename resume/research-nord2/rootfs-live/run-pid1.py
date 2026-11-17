from pathlib import Path
import subprocess,os,stat,json,struct,gzip,shutil,sys
base=Path(__file__).resolve().parent;root=base/'initramfs';mode=sys.argv[1]
assert mode == 'pid1'
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
script='#!/bin/bash\nset -euo pipefail\nmkfs.hfsplus -s -v Nord2Probe /dev/vda\nmount -t hfsplus -o rw /dev/vda /work/root\nmkdir -p /work/root/sbin /work/root/dev /work/root/private/var /work/root/tmp\ncp /build/nord2-init-probe /work/root/sbin/launchd\nchmod 0755 /work/root/sbin/launchd\nprintf \'NORD2_HFS_READ_OK\\n\' > /work/root/nord2-probe.txt\nchown 0:0 /work/root/sbin/launchd /work/root/nord2-probe.txt\nsha256sum /work/root/sbin/launchd\nstat -c \'%u:%g %a %s\' /work/root/sbin/launchd\nsync\numount /work/root\nfsck.hfsplus -f -y /dev/vda || true\nfsck.hfsplus -f -n /dev/vda\nmount -t hfsplus -o ro /dev/vda /work/root\ncmp /build/nord2-init-probe /work/root/sbin/launchd\npython3 -c "from pathlib import Path; assert Path(\'/work/root/nord2-probe.txt\').read_bytes() == b\'NORD2_HFS_READ_OK\\n\'"\numount /work/root\necho PID1_RAMROOT_VERIFIED\n'
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
image=base/'nord2-hfs-pid1.img'
with image.open('xb') as f:f.truncate(8<<20)
cmd=['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','2048','-smp','2','-nodefaults','-no-reboot','-display','none','-serial','stdio','-monitor','none','-kernel','/usr/lib/modules/7.2.9-arch1-1/vmlinuz','-initrd',str(initrd),'-append','console=ttyS0 rdinit=/init panic=-1 loglevel=4','-drive',f'file={image},format=raw,if=virtio'+(',readonly=on' if mode=='verify' else ''),'-virtfs',f'local,path={(Path("research/nord2/ipsw/root-images/mounts").resolve() if mode=="smoke" else base/"archives")},mount_tag=inputs,security_model=none,readonly=on']
(base/(mode+'-command.json')).write_text(json.dumps(cmd,indent=2)+'\n')
with (base/(mode+'.log')).open('w') as log:
 p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=1800)
text=(base/(mode+'.log')).read_text(errors='replace');print(text[-6000:]);assert p.returncode==0 and 'ROOTFS_VM_COMPLETE' in text and 'ROOTFS_VM_FAILED' not in text
print('VM completed:',image)
