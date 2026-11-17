"""One-use boot-only runner. Requires a separate exact-image human authorization."""
from pathlib import Path
import datetime, hashlib, json, os, subprocess, sys
os.umask(0o077)
D=Path(__file__).resolve().parent
R=next(p for p in D.parents if (p/'AGENTS.md').is_file())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=json.loads((D/'prepared.json').read_text())
a=json.loads((D/'authorization.json').read_text())
assert p['source_commit']=='f8e4daca68b4c2b12cc59c91106006032cfce68a' and p['ci_run']==39952094220
assert a['human_exact_image_authorized'] is True and a['conditional_recovery_authorized'] is True
assert a['scope']=='boot-only' and a['image_sha256']==p['image_sha256'] and a['human_reply_id']
assert sha(D/'independent-image-review/final/report.json')==p['image_verification_sha256']
assert sha(D/'ci-verification.json')==p['ci_verification_sha256']
image=R/p['image']
assert image.stat().st_size==p['image_bytes']<=33554432 and sha(image)==p['image_sha256']
for path,h in [('research/nord2/known-good/wifi-dhcp-618e62b5/boot.img','618e62b541d4cf57b0b5db21e6f7a5d89855ea14338663d905ba42282d61c9fa'),('research/nord2/rescue/nord2-adb-rescue.img','d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5')]:
 assert sha(R/path)==h
assert not (D/'RUNNER_EXECUTION_USED').exists() and not (D/'hardware-live').exists()
fastboot='$HOME/Android/Sdk/platform-tools/fastboot'
r=subprocess.run([fastboot,'devices'],capture_output=True,text=True,timeout=10,check=True)
rows=[l.split() for l in r.stdout.splitlines() if l.strip()]
assert len(rows)==1 and len(rows[0])==2 and rows[0][1]=='fastboot'
serial=rows[0][0]
with (D/'flash.log').open('x') as log:
 def fb(*args):
  r=subprocess.run([fastboot,'-s',serial,*args],capture_output=True,text=True,timeout=120)
  s=r.stdout+r.stderr;log.write(datetime.datetime.now().astimezone().isoformat()+' '+repr(args)+'\n'+s);log.flush();r.check_returncode();return s
 assert 'product: oplus6893' in fb('getvar','product')
 assert 'partition-size:boot: 2000000' in fb('getvar','partition-size:boot')
 assert 'is-userspace: no' in fb('getvar','is-userspace')
 (D/'RUNNER_EXECUTION_USED').open('x').write(datetime.datetime.now().astimezone().isoformat()+'\n')
 assert 'OKAY' in fb('flash','boot',str(image))
 print('Boot-only flash OKAY',flush=True)
 with (D/'client-hardware.log').open('x') as output:
  client=subprocess.Popen([sys.executable,'tools/nord2-usb-console.py',str(D/'hardware-live'),'--wait','300','--read-seconds','600','--timeout-ms','5000','--timeout-retries','5'],cwd=R,stdout=output,stderr=subprocess.STDOUT)
  (D/'client-hardware.pid').write_text(str(client.pid)+'\n')
  assert 'OKAY' in fb('reboot')
  print('Reboot OKAY; bounded USB collector started',flush=True)
  code=client.wait();(D/'collector-exit.json').open('x').write(json.dumps({'exit':code})+'\n')
  print('USB collector completed:',code,flush=True)
  raise SystemExit(code)
