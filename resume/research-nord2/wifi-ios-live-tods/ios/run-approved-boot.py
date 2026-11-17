from pathlib import Path
import datetime,hashlib,json,subprocess,sys,os
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file());D=Path(__file__).resolve().parent
os.umask(0o077)
expected='618e62b541d4cf57b0b5db21e6f7a5d89855ea14338663d905ba42282d61c9fa'
prepared=json.loads((D/'prepared.json').read_text())
assert prepared['image_sha256']==expected and prepared['source_commit']=='a92be30891637cc8be7b435e3d67cebcd8a3dafd'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(D/'independent-image-review/final/report.json')==prepared['image_verification_sha256']
assert sha(D/'ci-verification.json')==prepared['ci_verification_sha256']
image=R/prepared['image'];assert image.stat().st_size==33488896 and sha(image)==expected
for name,h in [('shim/out/nord2-xnu/nord2-ios-wifi-identity-keychain-stop23.img','d9a44316f6b257221a948a32483a6778b33cdf5057a3f79ffecfceebf0318de3'),('research/nord2/rescue/nord2-adb-rescue.img','d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5')]:assert sha(R/name)==h
fastboot='$HOME/Android/Sdk/platform-tools/fastboot'
devices=subprocess.run([fastboot,'devices'],capture_output=True,text=True,timeout=10,check=True)
rows=[l.split() for l in devices.stdout.splitlines() if l.strip()];assert len(rows)==1 and len(rows[0])==2 and rows[0][1]=='fastboot';serial=rows[0][0]
assert not (D/'hardware-live').exists() and not (D/'RUNNER_EXECUTION_USED').exists()
with (D/'authorization.json').open('x') as f:json.dump(dict(human_reply='call_789ff96268dd43009c36a5d029b68678',image_sha256=expected,scope='boot-only; installed iOS; corrected Ethernet first; conditional d9a or RAM ADB recovery',human_exact_image_authorized=True,conditional_recovery_authorized=True,device_serial=serial),f,indent=2)
with (D/'flash.log').open('x') as log:
 def fb(*args):
  x=subprocess.run([fastboot,'-s',serial,*args],capture_output=True,text=True,timeout=120)
  output=x.stdout+x.stderr;log.write(datetime.datetime.now().astimezone().isoformat()+' '+repr(args)+'\n'+output);log.flush();x.check_returncode();return output
 assert 'product: oplus6893' in fb('getvar','product')
 assert 'partition-size:boot: 2000000' in fb('getvar','partition-size:boot')
 assert 'is-userspace: no' in fb('getvar','is-userspace')
 (D/'RUNNER_EXECUTION_USED').open('x').write(datetime.datetime.now().astimezone().isoformat()+'\n')
 out=fb('flash','boot',str(image));assert 'OKAY' in out;print('Boot-only flash completed OKAY',flush=True)
 with (D/'client-hardware.log').open('x') as clientlog:
  client=subprocess.Popen([sys.executable,'tools/nord2-usb-console.py',str(D/'hardware-live'),'--wait','300','--read-seconds','600','--timeout-ms','5000','--timeout-retries','5'],cwd=R,stdout=clientlog,stderr=subprocess.STDOUT)
  (D/'client-hardware.pid').write_text(str(client.pid)+'\n')
  out=fb('reboot');assert 'OKAY' in out;print('Reboot OKAY; bounded USB collector started',flush=True)
  code=client.wait();(D/'collector-exit.json').open('x').write(json.dumps({'exit':code})+'\n');print('USB collector completed:',code,flush=True)
  raise SystemExit(code)
