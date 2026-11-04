#!/usr/bin/env python3
"""One-use boot-only runner for the exact already-authorized Metal sequence."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import time

EXPECTED = {
    'probe': ('metal_probe', '473f65c0d1a7fc4993e5e21413ead129810ab37b67f1f02fa9196aae23cac34c'),
    'ui': ('metal_ui', '25ed71a4a1d170de29c08c09d286d5aafd6d7b995fa4e00d95f62dc718005099'),
    'restore': ('fallback', 'a5b6e6e3c86a825e2828018597e687922496f82dbd1ff705113a99db1fb6be41'),
}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('stage',choices=EXPECTED)
    ap.add_argument('--evidence',required=True,type=Path)
    a=ap.parse_args()
    approval=json.loads((a.evidence/'authorization.json').read_text())
    assert approval['authorization'].startswith('call_85b549e707694654a0b968af3fb3933f:0')
    if a.stage!='restore':
        install=json.loads((a.evidence/'device-install/verified.json').read_text())
        assert install['full_device_readback_verified']
        assert install['after']=='10e403da49a87cff33e862424624c69cc0b22f06515947609435d7841383b182'
    if a.stage=='ui':
        result=json.loads((a.evidence/'probe-qualified.json').read_text())
        assert result['compute_verified'] and result['pixels_verified'] and result['iosurface_verified']
        assert result['probe_boot_sha256']==EXPECTED['probe'][1]
        assert result['failed_checks']==0
    name,expected=EXPECTED[a.stage]
    image=Path(approval['artifacts'][name]['path'])
    with image.open('rb') as f:actual=hashlib.file_digest(f,'sha256').hexdigest()
    assert actual==expected and actual==approval['artifacts'][name]['sha256']
    assert image.stat().st_size<=0x2000000
    out=a.evidence/('flash-'+a.stage);out.mkdir(exist_ok=False)
    fastboot='$HOME/Android/Sdk/platform-tools/fastboot'
    devices=subprocess.check_output([fastboot,'devices'],text=True,timeout=8).strip().splitlines()
    assert len(devices)==1
    serial,mode=devices[0].split();assert mode=='fastboot'
    def call(args):
        p=subprocess.run([fastboot,'-s',serial]+args,capture_output=True,text=True,timeout=90)
        text=p.stdout+p.stderr
        with (out/'fastboot.log').open('a') as f:f.write(' '.join(args)+'\n'+text)
        print(text,flush=True);p.check_returncode();return text
    assert 'product: oplus6893' in call(['getvar','product'])
    assert 'partition-size:boot: 2000000' in call(['getvar','partition-size:boot'])
    (out/'started.json').write_text(json.dumps({'time':time.time(),'image':str(image),'sha256':actual})+'\n')
    call(['flash','boot',str(image)])
    call(['reboot'])
    (out/'completed.json').write_text(json.dumps({'time':time.time(),'boot_sha256':actual,'fastboot_flash_reboot_ok':True})+'\n')


if __name__=='__main__':main()
