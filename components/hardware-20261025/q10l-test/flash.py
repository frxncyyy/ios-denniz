#!/usr/bin/env python3
"""One-use boot-only flasher for the exact q10l artifact (rootfs unchanged since the verified cc delta)."""
from pathlib import Path
import hashlib,json,subprocess,time
E=Path(__file__).resolve().parent
auth=json.loads((E/'authorization.json').read_text());assert auth['authorization']=='user-20261104-claude-q10l' and auth['approved']
art=auth['artifacts']['q10l'];image=Path(art['path'])
with image.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
assert actual==art['sha256'] and image.stat().st_size<=0x2000000
fb='$HOME/Android/Sdk/platform-tools/fastboot'
d=subprocess.check_output([fb,'devices'],text=True,timeout=8).strip().splitlines();assert len(d)==1
serial,mode=d[0].split();assert mode=='fastboot'
out=E/'flash-q10l';out.mkdir(exist_ok=False)
def call(args):
    p=subprocess.run([fb,'-s',serial]+args,capture_output=True,text=True,timeout=90);t=p.stdout+p.stderr
    with (out/'fastboot.log').open('a') as f:f.write(' '.join(args)+'\n'+t)
    print(t,flush=True);p.check_returncode();return t
assert 'product: oplus6893' in call(['getvar','product'])
assert 'partition-size:boot: 2000000' in call(['getvar','partition-size:boot'])
(out/'started.json').write_text(json.dumps({'time':time.time(),'image':str(image),'sha256':actual})+'\n')
call(['flash','boot',str(image)]);call(['reboot'])
(out/'completed.json').write_text(json.dumps({'time':time.time(),'boot_sha256':actual,'fastboot_flash_reboot_ok':True})+'\n')
