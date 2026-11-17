from pathlib import Path
import datetime,hashlib,json,subprocess,sys,resource
resource.setrlimit(resource.RLIMIT_AS,(1073741824,1073741824))
s=Path(__file__).resolve().parent;root=s.parents[3]
assert root.name=='ios-sweet'
a=json.loads((s/'authorization-required.json').read_text());r=json.loads((s/'recovery-image.json').read_text())
assert a['conditional_recovery_authorized'] and a['authorization_call']=='call_f0646fc645ca4d96abc9cfccfc4bf655'
assert r['sha256']=='d9a44316f6b257221a948a32483a6778b33cdf5057a3f79ffecfceebf0318de3'
image=root/r['path'];assert image.stat().st_size==r['bytes']==33462272
assert hashlib.sha256(image.read_bytes()).hexdigest()==r['sha256']
serial=a['device_serial'];fb='$HOME/Android/Sdk/platform-tools/fastboot'
dev=subprocess.run([fb,'devices'],capture_output=True,text=True,timeout=10,check=True)
assert [l.split() for l in dev.stdout.splitlines() if l.strip()]==[[serial,'fastboot']]
assert not (s/'recovery-live').exists()
with (s/'recovery-flash.log').open('x') as log:
 def run(*args):
  v=subprocess.run([fb,'-s',serial,*args],capture_output=True,text=True,timeout=120)
  t=v.stdout+v.stderr;log.write(datetime.datetime.now().astimezone().isoformat()+' '+repr(args)+'\n'+t);log.flush();print(t,flush=True);v.check_returncode();return t
 assert 'product: oplus6893' in run('getvar','product')
 assert 'partition-size:boot: 2000000' in run('getvar','partition-size:boot')
 assert 'is-userspace: no' in run('getvar','is-userspace')
 run('flash','boot',str(image))
 with (s/'recovery-client.log').open('x') as out:
  client=subprocess.Popen([sys.executable,'tools/nord2-usb-console.py',str(s/'recovery-live'),'--wait','300','--read-seconds','180','--timeout-ms','5000','--timeout-retries','5'],cwd=root,stdout=out,stderr=subprocess.STDOUT)
  (s/'recovery-client.pid').write_text(str(client.pid)+'\n')
  run('reboot');code=client.wait();print(json.dumps({'recovery_usb_exit':code}),flush=True);sys.exit(code)
