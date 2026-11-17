#!/usr/bin/env python3
"""One-use boot-only flasher for the exact y2sios-driver authorization (rescue, then the UI kernel after a verified install)."""
from pathlib import Path
import argparse,hashlib,json,subprocess,time
E=Path(__file__).resolve().parent
AUTH='user-20261102-claude-p48'
STAGES={'rescue':'rescue','p48':'p48_sched','p47':'p47_sched','p46':'p46_tele','p44':'p44_alloc2','p43':'p43_alloc','p42':'p42_dvfs','p41':'p41_irq','p40':'p40_wc','p39e4':'p39_ext4','p39e2':'p39_ext2','p38e1':'p38_ext1','p38e2':'p38_ext2','p37':'p37_ui_wc','p36':'p36_ui_hwinfo','p35':'p35_ui_seen','p34':'p34_ui_fastlog','p28':'p28_ui_ram3776','uidvfs':'p21_ui654fb60dvfs_cap3000','ui654fb60':'p19_ui654fb60','restore':'fallback'}
ap=argparse.ArgumentParser();ap.add_argument('stage',choices=STAGES);ap.add_argument('--tag',default='');a=ap.parse_args()
auth=json.loads((E/'authorization.json').read_text());assert auth['authorization']==AUTH and auth['approved']
if a.stage!='rescue':
    i=json.loads((E/'device-install/verified.json').read_text());u=json.loads((E/'offline-update/verified.json').read_text())
    assert i['all_device_preimages_verified'] and i['changed_ranges_readback_verified']
    assert i['after_id']==u['after_id'] and u['addon_sha256']==auth['artifacts']['addon']['sha256'] and not u['rehearsal']
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
