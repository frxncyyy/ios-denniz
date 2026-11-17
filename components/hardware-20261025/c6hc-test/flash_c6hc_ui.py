#!/usr/bin/env python3
"""One-use boot-only flash of UI image 124eb19b after a verified C6e-compositor install."""
from pathlib import Path
import hashlib,json,subprocess,time
E=Path(__file__).resolve().parent
a=json.loads((E/'authorization.json').read_text())
assert a['authorization']=='user-20261026-claude-c6h-compositor-install-and-ui'
i=json.loads((E/'device-install/verified.json').read_text())
assert i['all_device_preimages_verified'] and i['changed_ranges_readback_verified']
u=json.loads((E/'offline-update/verified.json').read_text())
assert u['after']==i['after'] and u['addon_sha256']==a['artifacts']['addon']['sha256'] and not u['rehearsal']
image=Path(a['ui']['path'])
with image.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
assert actual==a['ui']['sha256'] and image.stat().st_size<=0x2000000
fastboot='$HOME/Android/Sdk/platform-tools/fastboot'
devices=subprocess.check_output([fastboot,'devices'],text=True,timeout=8).strip().splitlines()
assert len(devices)==1
serial,mode=devices[0].split();assert mode=='fastboot'
out=E/'flash-ui';out.mkdir(exist_ok=False)
def call(args):
    p=subprocess.run([fastboot,'-s',serial]+args,capture_output=True,text=True,timeout=90)
    text=p.stdout+p.stderr
    with (out/'fastboot.log').open('a') as f:f.write(' '.join(args)+'\n'+text)
    print(text,flush=True);p.check_returncode();return text
assert 'product: oplus6893' in call(['getvar','product'])
assert 'partition-size:boot: 2000000' in call(['getvar','partition-size:boot'])
(out/'started.json').write_text(json.dumps({'time':time.time(),'image':str(image),'sha256':actual})+'\n')
call(['flash','boot',str(image)]);call(['reboot'])
(out/'completed.json').write_text(json.dumps({'time':time.time(),'boot_sha256':actual,'fastboot_flash_reboot_ok':True})+'\n')
