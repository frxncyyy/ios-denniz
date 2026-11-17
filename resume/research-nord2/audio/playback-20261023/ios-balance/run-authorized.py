"""One-use exact-image audio runner. Offline preparation is not authorization."""
from pathlib import Path
import argparse, datetime, hashlib, json, os, subprocess, sys
os.umask(0o077)
if sys.flags.optimize: raise SystemExit('Python -O is not supported')
D=Path(__file__).resolve().parent
R=next(p for p in D.parents if (p/'AGENTS.md').is_file())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('role',choices=('playback','rollback'))
args=ap.parse_args()
p=json.loads((D/'prepared.json').read_text())
a=json.loads((D/'authorization.json').read_text())
assert p['state']=='PREPARED_VERIFIED_NOT_AUTHORIZED'
assert a['human_exact_image_authorized'] is True
assert a['persistent_hal_install_authorized'] is True
assert a['conditional_rollback_authorized'] is True
assert a['human_reply_id'] and a['scope']=='boot-and-ios-hal-with-backup'
assert a['playback_sha256']==p['images']['playback']['image_sha256']
assert a['rollback_sha256']==p['images']['rollback']['image_sha256']
assert sha(Path(__file__))==p['runner_sha256']
assert sha(D/'ci-verification.json')==p['ci_verification_sha256']
for role,item in p['images'].items():
 image=R/item['image']
 assert image.stat().st_size==item['image_bytes']<=33554432
 assert sha(image)==item['image_sha256']
 assert sha(R/item['review'])==item['review_sha256']
 report=json.loads((R/item['review']).read_text())
 assert report['verdict']=='PASS' and report['mode']==role
 assert report['image_sha256']==item['image_sha256']
 assert report['source']==p['source_commit'] and report['ci_run']==p['ci_run']
for item in p['fallbacks']:
 assert sha(R/item['path'])==item['sha256']
assert not (D/('RUNNER_'+args.role.upper()+'_USED')).exists()
out=D/('hardware-'+args.role)
assert not out.exists()
fastboot='$HOME/Android/Sdk/platform-tools/fastboot'
r=subprocess.run([fastboot,'devices'],capture_output=True,text=True,timeout=10,check=True)
rows=[l.split() for l in r.stdout.splitlines() if l.strip()]
assert len(rows)==1 and len(rows[0])==2 and rows[0][1]=='fastboot'
serial=rows[0][0]
image=R/p['images'][args.role]['image']
with (D/('flash-'+args.role+'.log')).open('x') as log:
 def fb(*argv):
  r=subprocess.run([fastboot,'-s',serial,*argv],capture_output=True,text=True,timeout=120)
  s=r.stdout+r.stderr
  log.write(datetime.datetime.now().astimezone().isoformat()+' '+repr(argv)+'\n'+s)
  log.flush();r.check_returncode();return s
 assert 'product: oplus6893' in fb('getvar','product')
 assert 'partition-size:boot: 2000000' in fb('getvar','partition-size:boot')
 assert 'is-userspace: no' in fb('getvar','is-userspace')
 (D/('RUNNER_'+args.role.upper()+'_USED')).open('x').write(datetime.datetime.now().astimezone().isoformat()+'\n')
 assert 'OKAY' in fb('flash','boot',str(image))
 print('Exact '+args.role+' boot image flash OKAY',flush=True)
 with (D/('collector-'+args.role+'.log')).open('x') as output:
  client=subprocess.Popen([sys.executable,'tools/nord2-usb-console.py',str(out),'--wait','300','--read-seconds','600','--timeout-ms','5000','--timeout-retries','5'],cwd=R,stdout=output,stderr=subprocess.STDOUT)
  (D/('collector-'+args.role+'.pid')).write_text(str(client.pid)+'\n')
  assert 'OKAY' in fb('reboot')
  print('Reboot OKAY; bounded USB collector started',flush=True)
  code=client.wait()
  (D/('collector-'+args.role+'-exit.json')).open('x').write(json.dumps({'exit':code})+'\n')
  print('USB collector completed:',code,flush=True)
  raise SystemExit(code)
