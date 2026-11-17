#!/usr/bin/env python3
"""One-use boot-only flasher for the DRAM measurement session (rescue, then the p23 DRAM-register probe; no rootfs write)."""
from pathlib import Path
import argparse,hashlib,json,subprocess,time
E=Path(__file__).resolve().parent
AUTH='user-20261029-claude-prfcnt-v3'
STAGES={'rescue':'rescue','pf3':'p30_ui_pf3','clk':'p29_ui_clk','ram':'p28_ui_ram3776','ram2016':'p26_ui_ram2016','uidvfs':'p21_ui654fb60dvfs_cap3000','ui654fb60':'p19_ui654fb60','restore':'fallback'}
ap=argparse.ArgumentParser();ap.add_argument('stage',choices=STAGES);ap.add_argument('--tag',default='');a=ap.parse_args()
auth=json.loads((E/'authorization.json').read_text());assert auth['authorization']==AUTH and auth['approved']
art=auth['artifacts'][STAGES[a.stage]];image=Path(art['path'])
with image.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
assert actual==art['sha256'] and image.stat().st_size<=0x2000000
fb='$HOME/Android/Sdk/platform-tools/fastboot'
d=subprocess.check_output([fb,'devices'],text=True,timeout=8).strip().splitlines();assert len(d)==1
serial,mode=d[0].split();assert mode=='fastboot'
out=E/('flash-'+a.stage+a.tag);out.mkdir(exist_ok=False)
def call(args):
    p=subprocess.run([fb,'-s',serial]+args,capture_output=True,text=True,timeout=90);t=p.stdout+p.stderr
    with (out/'fastboot.log').open('a') as f:f.write(' '.join(args)+'\n'+t)
    print(t,flush=True);p.check_returncode();return t
assert 'product: oplus6893' in call(['getvar','product'])
assert 'partition-size:boot: 2000000' in call(['getvar','partition-size:boot'])
(out/'started.json').write_text(json.dumps({'time':time.time(),'image':str(image),'sha256':actual})+'\n')
call(['flash','boot',str(image)]);call(['reboot'])
(out/'completed.json').write_text(json.dumps({'time':time.time(),'boot_sha256':actual,'fastboot_flash_reboot_ok':True})+'\n')
