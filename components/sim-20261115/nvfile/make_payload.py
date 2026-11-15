#!/usr/bin/env python3
# payload-nvfile1 from a blob of THIS phone (default: the blob its modem booted from, scratchpad-backup-20261106/nvbuild/
# n2nvdisk-android-20261106.bin; or --blob <file> made by portable/nvdisk_from_device.sh on the phone it goes to). PRIVATE: holds the
# phone's IMEI/RF calibration -- never publish payload-nvfile1*.
import argparse, hashlib, json, os, shutil
H=os.path.dirname(os.path.abspath(__file__)); B=os.path.abspath(H+'/../..')
ap=argparse.ArgumentParser(); ap.add_argument('--blob', default=B+'/scratchpad-backup-20261106/nvbuild/n2nvdisk-android-20261106.bin'); a=ap.parse_args()
blob=open(a.blob,'rb').read(); sha=hashlib.sha256(blob).hexdigest()
for v,rep in (('nvfile1',False),('nvfile1-replace',True)):
    d=H+'/payload-'+v; shutil.rmtree(d,ignore_errors=True); os.makedirs(d); os.chmod(d,0o700)
    shutil.copy2(a.blob,d+'/n2nvdisk.bin'); os.chmod(d+'/n2nvdisk.bin',0o600)
    json.dump({'payload':v,'blob_sha256':sha,'replace':rep},open(d+'/plan.json','w'))
d=H+'/payload-nvfile1-undo'; shutil.rmtree(d,ignore_errors=True); os.makedirs(d); json.dump({'payload':'nvfile1-undo'},open(d+'/plan.json','w'))
json.dump(['/private/var/db/nord2-modem','/private/var/db/nord2-modem/n2nvdisk.bin','/private/var/db/nord2-modem/nvfile1.json'],open(H+'/owned-nvfile1.json','w'))
print('payload-nvfile1 built from', os.path.basename(a.blob), 'sha256', sha[:16], len(blob)>>20, 'MiB')
