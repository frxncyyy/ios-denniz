#!/usr/bin/env python3
"""One-use boot-only runner for the exact already-authorized Metal sequence."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import time

EXPECTED = {
    'rescue': ('rescue', 'd78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5'),
    'probe': ('probe_boot', '1e9d810a55db3bfe8391a86f669bd91f0f64abe98e722213107f723b93ae3bd9'),
    'restore': ('fallback', 'a5b6e6e3c86a825e2828018597e687922496f82dbd1ff705113a99db1fb6be41'),
}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('stage',choices=EXPECTED)
    ap.add_argument('--evidence',required=True,type=Path)
    a=ap.parse_args()
    approval=json.loads((a.evidence/'authorization.json').read_text())
    assert approval['authorization']=='user-20261025-autorizzo-viewport-addon'
    if a.stage=='probe':
        install=json.loads((a.evidence/'device-install/verified.json').read_text())
        assert install['full_device_readback_verified']
        updated=json.loads((a.evidence/'offline-update/verified.json').read_text())
        assert install['after']==updated['after'] and updated['addon_sha256']==approval['artifacts']['addon']['sha256']
        assert not updated['rehearsal']
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
