#!/usr/bin/env python3
"""Read-only look at the rootfs mirror: throw-away qcow2 overlay, VM prints disk usage / system version / app layout. Never touches a phone."""
import sys, json, subprocess, time
from pathlib import Path
W = Path(sys.argv[1]).resolve(); OUT = Path(sys.argv[2]).resolve(); GUEST = Path(sys.argv[3]).read_text()
sys.path.insert(0, str(W/'gpu/tools'))
import update_metal_offline_fast as f
OUT.mkdir(exist_ok=False)
mirror = W/'gpu/build/mirror/rootfs-mirror.img'
payload = OUT/'inputs'; payload.mkdir()
(payload/'fastroot-journal.bin').write_bytes(f.journal_payload(mirror, W/'gpu/build/hardware-20261024/hfs-source/oracle', OUT/'journal-replay.json'))
if len(sys.argv) > 5:
    subprocess.run(['cp','-al','--',sys.argv[5]+'/.','.'], cwd=payload, check=True)      # hard-linked extra inputs (apps, plan)
overlay = OUT/'overlay.qcow2'
subprocess.run(['qemu-img','create','-q','-f','qcow2','-b',str(mirror),'-F','raw','-o','cluster_size=1M,lazy_refcounts=on',str(overlay)], check=True)
script = "#!/bin/bash\nset -uo pipefail\nmount -t 9p -o trans=virtio,version=9p2000.L,ro,cache=loose,msize=262144 inputs /src\n" + f.NEW_FSCK + "\n" + GUEST
initrd = OUT/'initrd.gz'
f.build_initrd(W.parent.parent.parent/'research/nord2/rootfs-vm/initramfs' if False else Path(sys.argv[4]), script.encode(), initrd)
cmd = ['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','2048','-smp','2','-nodefaults','-no-reboot','-display','none','-serial','stdio','-monitor','none',
       '-kernel',str(W/'gpu/build/vm-runtime/vmlinuz-7.2.9-arch1-1'),'-initrd',str(initrd),'-append','console=ttyS0 rdinit=/init panic=-1 loglevel=4',
       '-drive',f'file={overlay},format=qcow2,if=virtio,cache=unsafe','-virtfs',f'local,path={payload},mount_tag=inputs,security_model=none,readonly=on']
t=time.time()
with (OUT/'vm.log').open('wb') as o: subprocess.run(cmd, stdout=o, stderr=subprocess.STDOUT, check=True, timeout=1500)
print('vm seconds', round(time.time()-t))
