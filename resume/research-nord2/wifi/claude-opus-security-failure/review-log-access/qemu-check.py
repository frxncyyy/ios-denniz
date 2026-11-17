#!/usr/bin/env python3
"""Offline baseline-only comparison. Immutable HFS image is a readonly QEMU drive.
No live userdata, no fsck, no host share, no target file contents read."""
from pathlib import Path
import gzip,importlib.util,json,stat,subprocess
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'AGENTS.md').exists());B=R/'research/nord2/rootfs-vm'
s=importlib.util.spec_from_file_location('mk',R/'tools/nord2-mkrescue.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
script='''#!/bin/busybox sh
/bin/busybox mkdir -p /bbbin /proc /sys /dev /root
/bin/busybox --install -s /bbbin
export PATH=/bin:/usr/bin:/bbbin
mount -t devtmpfs devtmpfs /dev
exec </dev/console >/dev/console 2>&1
mount -t proc proc /proc
mount -t sysfs sysfs /sys
'''
script+='\n'.join('insmod /modules/'+n for n in json.loads((B/'modules.json').read_text()))+'\n'
script+='''mount -t hfsplus -o ro /dev/vda /root || { echo LOGCHECK_MOUNT_FAILED; poweroff -f; }
python3 - <<'INNER'
import os,json,re,stat
paths=['private/var/mobile/Library/Logs/CrashReporter','private/var/mobile/Library/Logs/CrashReporter/Retired','private/var/log/diagnosticd/stats']
pattern=re.compile(r'(?:wifid|wifip2pd|WiFiAgent|Preferences|JetsamEvent)[-_.][A-Za-z0-9_.-]+$')
for path in paths:
 p='/root/'+path
 try:
  st=os.lstat(p);row={'path':path,'found':True,'directory':stat.S_ISDIR(st.st_mode)}
  if row['directory']:
   names=os.listdir(p)
   if len(names)>256:raise ValueError('directory entry cap')
   row['allowed_names']=sorted((n for n in names if pattern.fullmatch(n)),reverse=True)[:8]
  print('LOGCHECK '+json.dumps(row))
 except FileNotFoundError:print('LOGCHECK '+json.dumps({'path':path,'found':False}))
INNER
umount /root
poweroff -f
'''
f=[0]*13;f[1]=stat.S_IFREG|0o755;initrd=P/'qemu-check-initrd.gz';initrd.write_bytes(gzip.compress(gzip.decompress((B/'verify-initrd.gz').read_bytes())+m.cpio_write({'init':(f,script.encode())})))
cmd=json.loads((B/'verify-command.json').read_text());cmd[cmd.index('-initrd')+1]=str(initrd)
i=cmd.index('-virtfs');del cmd[i:i+2]
assert cmd.count('-drive')==1 and cmd[cmd.index('-drive')+1].endswith(',readonly=on') and str(B/'ios16-root-20H392-nord2-v1.img') in cmd[cmd.index('-drive')+1]
(P/'qemu-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=55);(P/'qemu-check.log').write_bytes(r.stdout)
rows=[json.loads(x[9:]) for x in r.stdout.decode(errors='replace').splitlines() if x.startswith('LOGCHECK ')];assert r.returncode==0 and len(rows)==3,(r.returncode,rows)
assert rows[0]['found'] and rows[0]['directory'] and not rows[0]['allowed_names'];assert not rows[1]['found'] and not rows[2]['found']
(P/'qemu-result.json').write_text(json.dumps({'exit':r.returncode,'readonly_drive':True,'live_device':False,'file_contents_read':False,'baseline_only':True,'rows':rows},indent=2)+'\n');print('PASS readonly QEMU baseline agrees: CrashReporter ordinary directory, no matching reports; Retired/stats absent. No live-device claim.')
