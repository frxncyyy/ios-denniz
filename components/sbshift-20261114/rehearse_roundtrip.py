#!/usr/bin/env python3
"""(sbshift-20261114 copy of final-20261114/rehearse_roundtrip.py, itself a copy of hotfix-20261114/rehearse_roundtrip.py: copies the whole payload dir) VM round-trip rehearsal (install, re-install no-op, undo, re-undo no-op) of a guest-*roundtrip.sh on a throw-away overlay of the mirror.
Never touches a phone or the mirror.  usage: rehearse_roundtrip.py GUEST_ROUNDTRIP_SH PAYLOAD_DIR OUTDIR   (OUTDIR must not exist; overlay deleted at the end)"""
import subprocess, sys, time, shutil
from pathlib import Path
HERE = Path(__file__).resolve().parent; W = HERE.parent.parent.parent
sys.path.insert(0, str(W/'gpu/tools'))
import update_metal_offline_fast as f
guest = Path(sys.argv[1]).resolve(); payload = Path(sys.argv[2]).resolve(); OUT = Path(sys.argv[3]).resolve(); OUT.mkdir(exist_ok=False)
mirror = W/'gpu/build/mirror/rootfs-mirror.img'; before = mirror.stat()
inp = OUT/"inputs"; shutil.copytree(payload, inp)   # final-20261114: the whole payload dir (inst/ + undo/ for the bind-mounted phases), not only plan.json
(inp/'fastroot-journal.bin').write_bytes(f.journal_payload(mirror, W/'gpu/build/hardware-20261024/hfs-source/oracle', OUT/'journal-replay.json'))
ov = OUT/'overlay.qcow2'
subprocess.run(['qemu-img','create','-q','-f','qcow2','-b',str(mirror),'-F','raw','-o','cluster_size=1M,lazy_refcounts=on',str(ov)], check=True)
script = "#!/bin/bash\nset -uo pipefail\nmount -t 9p -o trans=virtio,version=9p2000.L,ro,cache=loose,msize=262144 inputs /src\n" + f.NEW_FSCK + "\n" + guest.read_text()
initrd = OUT/'initrd.gz'; f.build_initrd(W.parent.parent.parent/'rootfs-vm/initramfs', script.encode(), initrd)
cmd = ['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','2048','-smp','2','-nodefaults','-no-reboot','-display','none','-serial','stdio','-monitor','none',
       '-kernel',str(W/'gpu/build/vm-runtime/vmlinuz-7.2.9-arch1-1'),'-initrd',str(initrd),'-append','console=ttyS0 rdinit=/init panic=-1 loglevel=4',
       '-drive',f'file={ov},format=qcow2,if=virtio,cache=unsafe','-virtfs',f'local,path={inp},mount_tag=inputs,security_model=none,readonly=on']
t = time.time()
with (OUT/'vm.log').open('wb') as o: subprocess.run(cmd, stdout=o, stderr=subprocess.STDOUT, check=True, timeout=1500)
after = mirror.stat(); assert (before.st_size, before.st_mtime_ns, before.st_ino) == (after.st_size, after.st_mtime_ns, after.st_ino), 'mirror changed'
print('vm seconds', round(time.time()-t)); ov.unlink(); initrd.unlink(); shutil.rmtree(inp)
