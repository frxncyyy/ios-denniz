#!/usr/bin/env python3
"""Offline cleanup + app install on a qcow2 overlay of the refreshed mirror; produces a delta receipt like update_metal_offline_fast.py. Never touches a phone."""
import argparse, hashlib, json, subprocess, sys, time
from pathlib import Path
W = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(W/'gpu/tools'))
import fastroot as fr
import update_metal_offline_fast as f
ap = argparse.ArgumentParser()
ap.add_argument('--snapshot-receipt', type=Path, required=True); ap.add_argument('--payload', type=Path, required=True)
ap.add_argument('--guest', type=Path, required=True); ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--initramfs', type=Path, required=True)
a = ap.parse_args(); t0 = time.monotonic()
snap = json.loads(a.snapshot_receipt.read_text())
assert snap['format'] == fr.FORMAT and snap['per_chunk_manifest_matches'] and snap['phone_partition_writes'] == 0
mirror = Path(snap['mirror']); size = snap['bytes']
side = json.loads(Path(str(mirror) + '.manifest.json').read_text()); s = mirror.stat()
assert side['clean'] and side['root'] == snap['root'] and fr.root_of(side['hashes']) == snap['root']
assert side['signature'] == {'size': s.st_size, 'mtime_ns': s.st_mtime_ns, 'ino': s.st_ino}, 'mirror changed since snapshot'
a.output.mkdir(exist_ok=False)
payload = (a.output/'inputs').resolve(); payload.mkdir()
subprocess.run(['cp','-al','--',str(a.payload)+'/.','.'], cwd=payload, check=True)
(payload/'fastroot-journal.bin').write_bytes(f.journal_payload(mirror, W/'gpu/build/hardware-20261024/hfs-source/oracle', a.output/'journal-replay.json'))
overlay = (a.output/'overlay.qcow2').resolve()
subprocess.run(['qemu-img','create','-q','-f','qcow2','-b',str(mirror.resolve()),'-F','raw','-o','cluster_size=1M,lazy_refcounts=on',str(overlay)], check=True)
script = "#!/bin/bash\nset -uo pipefail\nmount -t 9p -o trans=virtio,version=9p2000.L,ro,cache=loose,msize=262144 inputs /src\n" + f.NEW_FSCK + "\n" + a.guest.read_text()
initrd = a.output/'initrd.gz'; f.build_initrd(a.initramfs, script.encode(), initrd)
cmd = ['qemu-system-x86_64','-enable-kvm','-cpu','host','-m','2048','-smp','2','-nodefaults','-no-reboot','-display','none','-serial','stdio','-monitor','none',
       '-kernel',str(W/'gpu/build/vm-runtime/vmlinuz-7.2.9-arch1-1'),'-initrd',str(initrd),'-append','console=ttyS0 rdinit=/init panic=-1 loglevel=4',
       '-drive',f'file={overlay},format=qcow2,if=virtio,cache=unsafe','-virtfs',f'local,path={payload},mount_tag=inputs,security_model=none,readonly=on']
t = time.monotonic()
with (a.output/'vm.log').open('wb') as o: subprocess.run(cmd, stdout=o, stderr=subprocess.STDOUT, check=True, timeout=1500)
log = (a.output/'vm.log').read_text(errors='replace'); print(log[-1800:])
assert 'INSTALL_DONE' in log and 'ROOTFS_VM_COMPLETE' in log and 'ROOTFS_VM_FAILED' not in log and 'INSTALLED Terraria.app' in log and 'INSTALLED SubwaySurf.app' in log
assert 'appears to be OK' in log.rsplit('INSTALL_DONE', 1)[0].rsplit('FREE_AFTER_INSTALL_MB', 1)[1]
s2 = mirror.stat(); assert side['signature'] == {'size': s2.st_size, 'mtime_ns': s2.st_mtime_ns, 'ino': s2.st_ino}, 'mirror mutated'
table, allocated = f.extract_delta(overlay, mirror, a.output, size)
delta = {t_['chunk']: t_['after'] for t_ in table}
res = {'format': fr.FORMAT, 'kind': 'offline-update', 'before_root': snap['root'], 'mirror': str(mirror), 'after_id': fr.delta_id(snap['root'], delta),
       'addon_sha256': 'apps-' + hashlib.sha256((a.payload/'plan.json').read_bytes()).hexdigest(), 'bytes': size, 'chunk_bytes': fr.CHUNK,
       'overlay_allocated_chunks': allocated, 'changed_chunks': len(table), 'delta': table, 'offline_verified': True, 'rehearsal': False,
       'fsck_clean_after': True, 'timings': {'vm_s': round(time.monotonic()-t, 1), 'total_s': round(time.monotonic()-t0, 1)}}
fr.write_json_atomic(a.output/'verified.json', res)
print(json.dumps({k: v for k, v in res.items() if k != 'delta'}, indent=1))
