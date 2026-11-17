#!/usr/bin/env python3
"""One-use boot-only flash of UI image 124eb19b, gated on C2 phase-A qualification."""
from pathlib import Path
import hashlib,json,subprocess,time
E=Path(__file__).resolve().parent
b=json.loads((E/'authorization-phase-b.json').read_text())
assert b['authorization']=='user-20261026-claude-c6e-safe-phase-b'
r=json.loads((E/'phase-a-result.json').read_text())
assert r['batch_hardware_qualified'] and r['addon_sha256']==b['installed_addon_sha256']
rec=json.loads((E/'reconciled/provenance.json').read_text())
assert rec['combined_sha256']==r['combined_sha256']
image=Path(b['ui']['path'])
with image.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
assert actual==b['ui']['sha256'] and image.stat().st_size<=0x2000000
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
