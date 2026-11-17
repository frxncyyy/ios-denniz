from pathlib import Path
import gzip,importlib.util,json,subprocess,stat
root=Path.cwd();b=root/'research/nord2/display-capabilities';base=root/'research/nord2/rootfs-vm'
s=importlib.util.spec_from_file_location('mk',root/'tools/nord2-mkrescue.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
script='''#!/bin/busybox sh
/bin/busybox mkdir -p /bbbin /proc /sys /dev /root
/bin/busybox --install -s /bbbin
export PATH=/bin:/usr/bin:/bbbin
mount -t devtmpfs devtmpfs /dev
exec </dev/console >/dev/console 2>&1
mount -t proc proc /proc
mount -t sysfs sysfs /sys
'''
script+='\n'.join('insmod /modules/'+n for n in json.loads((base/'modules.json').read_text()))+'\n'
script+='''mount -t hfsplus -o ro /dev/vda /root || poweroff -f
python3 - <<'INNER'
import os,plistlib,json,hashlib
paths=['private/var/mobile/Library/Preferences','Library/Preferences','private/var/root/Library/Preferences']
for rel in paths:
 d='/root/'+rel
 if not os.path.isdir(d):print('PREF_DIR_MISSING',rel);continue
 for n in sorted(os.listdir(d)):
  if not n.endswith('.plist'):continue
  p=d+'/'+n
  try:
   if os.stat(p).st_size>262144:continue
   raw=open(p,'rb').read();obj=plistlib.loads(raw)
   relevant={k:v for k,v in obj.items() if any(x in k.lower() for x in ['zoom','canvas','display'])}
   if relevant or any(x in n.lower() for x in ['graphics','backboard','springboard']):
    print('PREF_EVIDENCE',json.dumps(dict(path=rel+'/'+n,sha256=hashlib.sha256(raw).hexdigest(),keys=list(obj),relevant=relevant),default=str))
  except Exception as e:print('PREF_ERROR',p,str(e))
INNER
umount /root
poweroff -f
'''
f=[0]*13;f[1]=stat.S_IFREG|0o755
initrd=b/'baseline-read-initrd.gz';initrd.write_bytes(gzip.compress(gzip.decompress((base/'verify-initrd.gz').read_bytes())+m.cpio_write({'init':(f,script.encode())})))
cmd=json.loads((base/'verify-command.json').read_text());cmd[cmd.index('-initrd')+1]=str(initrd)
# No host export is needed.
i=cmd.index('-virtfs');del cmd[i:i+2]
r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=55)
(b/'baseline-read.log').write_bytes(r.stdout)
print(r.returncode);print('\n'.join(l for l in r.stdout.decode(errors='replace').splitlines() if 'PREF_' in l))
