#!/usr/bin/env python3
"""nvfile payload (modem NVRAM blob as /private/var/db/nord2-modem/n2nvdisk.bin, read first by kernel piece 0715) built from
THIS phone's own blob, only after n2_check_nvdisk.py proved the blob is made from this phone's dump.

    mk_nvfile_payload.py --blob n2nvdisk-this-device.bin --dump DUMP_DIR --out PAYLOAD_DIR

PAYLOAD_DIR gets payload-nvfile1/, payload-nvfile1-replace/, payload-nvfile1-undo/ (plan.json + blob, mode 0700/0600) and
owned-nvfile1.json, for the guest scripts guest-nvfile1.sh / guest-nvfile1-undo.sh run by n2_txn.sh.  Private: never share."""
import argparse, hashlib, json, os, shutil, subprocess, sys

H = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--blob', required=True); ap.add_argument('--dump', required=True); ap.add_argument('--out', required=True)
a = ap.parse_args()
r = subprocess.run([sys.executable, '-I', os.path.join(H, 'n2_check_nvdisk.py'), a.blob, '--dump', a.dump])
if r.returncode:
    raise SystemExit('REFUSED: the blob is not this phone\'s own (see above)')
blob = open(a.blob, 'rb').read()
sha = hashlib.sha256(blob).hexdigest()
os.makedirs(a.out, exist_ok=True); os.chmod(a.out, 0o700)
for v, rep in (('nvfile1', False), ('nvfile1-replace', True)):
    d = os.path.join(a.out, 'payload-' + v)
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d); os.chmod(d, 0o700)
    shutil.copyfile(a.blob, d + '/n2nvdisk.bin'); os.chmod(d + '/n2nvdisk.bin', 0o600)
    json.dump({'payload': v, 'blob_sha256': sha, 'replace': rep}, open(d + '/plan.json', 'w'))
d = os.path.join(a.out, 'payload-nvfile1-undo')
shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
json.dump({'payload': 'nvfile1-undo'}, open(d + '/plan.json', 'w'))
json.dump(['/private/var/db/nord2-modem', '/private/var/db/nord2-modem/n2nvdisk.bin', '/private/var/db/nord2-modem/nvfile1.json'],
          open(os.path.join(a.out, 'owned-nvfile1.json'), 'w'))
for g in ('guest-nvfile1.sh', 'guest-nvfile1-undo.sh'):
    shutil.copyfile(os.path.join(H, g), os.path.join(a.out, g))
print('NVFILE_PAYLOAD_OK %s (blob sha256 %s..., %d MiB)' % (a.out, sha[:12], len(blob) >> 20))
