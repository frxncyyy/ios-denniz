#!/usr/bin/env python3
"""Throwaway diagnostic VM (host only): a NEW qcow2 overlay on top of cc-txn-wall3/offline/overlay.qcow2 (never written itself),
fsck -d -n with the journal header as wall3 left it, then with start=end (what xnu replay_journal writes), fsck -d -n again."""
import subprocess, sys
from pathlib import Path
W = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(W/'gpu/tools'))
import update_metal_offline_fast as f
MAIN = Path('$NORD2_REPO')
out = Path(__file__).resolve().parent/'debug-wall3'; out.mkdir(exist_ok=False)
base = W/'gpu/build/cc-txn-wall3/offline/overlay.qcow2'
ov = out/'overlay.qcow2'
subprocess.run(['qemu-img','create','-q','-f','qcow2','-b',str(base),'-F','qcow2',str(ov)], check=True)
script = r'''#!/bin/bash
set -uo pipefail
echo DBG_A_HEADER_AS_WALL3
fsck.hfsplus -d -f -n /dev/vda 2>&1 | grep -a -i "journal\|corrupt\|appears\|information\|invalid\|error" 
python3 - <<'PY'
import os, struct
def ck(d):
    c = 0
    for x in d: c = ((c << 8) ^ (c + x)) & 0xffffffff
    return (~c) & 0xffffffff
fd = os.open('/dev/vda', os.O_RDWR)
vh = os.pread(fd, 512, 1024); jib = struct.unpack_from('>I', vh, 12)[0]
ji = os.pread(fd, 180, jib*4096); off, n = struct.unpack_from('>QQ', ji, 36)
h = bytearray(os.pread(fd, 512, off)); start, end = struct.unpack_from('<QQ', h, 8)
print('DBG_HEADER_BEFORE start', start, 'end', end, 'seq', struct.unpack_from('<I', h, 44)[0], 'vh.lastMountedVersion', vh[8:12])
struct.pack_into('<Q', h, 16, start); struct.pack_into('<I', h, 36, 0); struct.pack_into('<I', h, 36, ck(h[:44]))
os.pwrite(fd, bytes(h), off); os.fsync(fd); os.close(fd)
print('DBG_HEADER_AFTER start', start, 'end', start)
PY
echo DBG_B_HEADER_START_EQ_END
fsck.hfsplus -d -f -n /dev/vda 2>&1 | grep -a -i "journal\|corrupt\|appears\|information\|invalid\|error"
echo ROOTFS_VM_COMPLETE
'''
initrd = out/'initrd.gz'; f.build_initrd(MAIN/'research/nord2/rootfs-vm/initramfs', script.encode(), initrd)
cmd = ['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','2048','-smp','2','-nodefaults','-no-reboot','-display','none','-serial','stdio','-monitor','none',
       '-kernel',str(W/'gpu/build/vm-runtime/vmlinuz-7.2.9-arch1-1'),'-initrd',str(initrd),'-append','console=ttyS0 rdinit=/init panic=-1 loglevel=4',
       '-drive',f'file={ov},format=qcow2,if=virtio,cache=unsafe']
with (out/'vm.log').open('wb') as o: subprocess.run(cmd, stdout=o, stderr=subprocess.STDOUT, check=True, timeout=600)
print((out/'vm.log').read_text(errors='replace'))
ov.unlink(); initrd.unlink()
