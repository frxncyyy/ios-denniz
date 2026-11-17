#!/usr/bin/env python3
"""One-use boot-only flasher for the exact P21 probe350dvfs authorization (addon C6v2 already installed, nothing else written)."""
from pathlib import Path
import hashlib,json,subprocess,sys,time
E=Path(__file__).resolve().parent
AUTH='user-20261029-claude-p21-probe350dvfs-cap3000'
STAGES={'cap3000':'p21_probe350dvfs_cap3000','ui654fb60':'p19_ui654fb60','restore':'fallback'}
stage=sys.argv[1];tag=sys.argv[2] if len(sys.argv)>2 else ''
auth=json.loads((E/'authorization.json').read_text());assert auth['authorization']==AUTH and auth['approved'] and stage in STAGES
art=auth['artifacts'][STAGES[stage]];image=Path(art['path'])
with image.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
assert actual==art['sha256'] and image.stat().st_size<=0x2000000
fb='$HOME/Android/Sdk/platform-tools/fastboot'
d=subprocess.check_output([fb,'devices'],text=True,timeout=8).strip().splitlines();assert len(d)==1
serial,mode=d[0].split();assert mode=='fastboot'
out=E/('flash-'+stage+tag);out.mkdir(exist_ok=False)
def call(args):
    p=subprocess.run([fb,'-s',serial]+args,capture_output=True,text=True,timeout=90);t=p.stdout+p.stderr
    with (out/'fastboot.log').open('a') as f:f.write(' '.join(args)+'\n'+t)
    print(t,flush=True);p.check_returncode();return t
assert 'product: oplus6893' in call(['getvar','product'])
assert 'partition-size:boot: 2000000' in call(['getvar','partition-size:boot'])
(out/'started.json').write_text(json.dumps({'time':time.time(),'image':str(image),'sha256':actual})+'\n')
call(['flash','boot',str(image)]);call(['reboot'])
(out/'completed.json').write_text(json.dumps({'time':time.time(),'boot_sha256':actual,'fastboot_flash_reboot_ok':True})+'\n')
