from pathlib import Path
import datetime,hashlib,json,subprocess,sys
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[3]
assert sys.argv[1:]==['--authorized-conditional-recovery']
a=json.loads((OUT/'authorization-required.json').read_text())
assert a['human_exact_image_authorized'] and a['conditional_recovery_authorized']
assert a['image_sha256']=='89b3eb2abe4e906f420263803033a2a33a3717b23aecdafd488344e168e42066'
serial=a['device_serial'];assert serial
image=ROOT/'shim/out/nord2-xnu/nord2-ios-wifi-dma-battery-stop23.img'
assert image.stat().st_size==33325056
assert hashlib.sha256(image.read_bytes()).hexdigest()=='e0d3a50f7a706f27a8d9fb19bbd6951c85f016e8b179b47b726c331331ec65d5'
for name in ('restore-e0d3-flash.log','restore-e0d3-client.log','restore-e0d3-live','RESTORE_EXECUTION_USED'):
 assert not (OUT/name).exists(),name
fb='$HOME/Android/Sdk/platform-tools/fastboot'
r=subprocess.run([fb,'devices'],capture_output=True,text=True,check=True,timeout=10)
assert [l.split() for l in r.stdout.splitlines() if l.strip()]==[[serial,'fastboot']]
(OUT/'RESTORE_EXECUTION_USED').write_text(datetime.datetime.now().astimezone().isoformat()+'\n')
with (OUT/'restore-e0d3-flash.log').open('x') as log:
 def run(*args):
  r=subprocess.run([fb,'-s',serial,*args],capture_output=True,text=True,timeout=120)
  text=r.stdout+r.stderr;log.write(datetime.datetime.now().astimezone().isoformat()+' '+repr(args)+'\n'+text);log.flush();print(text,flush=True);r.check_returncode();return text
 assert 'product: oplus6893' in run('getvar','product')
 assert 'partition-size:boot: 2000000' in run('getvar','partition-size:boot')
 assert 'is-userspace: no' in run('getvar','is-userspace')
 run('flash','boot',str(image))
 with (OUT/'restore-e0d3-client.log').open('x') as logclient:
  client=subprocess.Popen([sys.executable,'tools/nord2-usb-console.py',str(OUT/'restore-e0d3-live'),'--wait','300','--read-seconds','90','--timeout-ms','5000','--timeout-retries','5'],cwd=ROOT,stdout=logclient,stderr=subprocess.STDOUT)
  (OUT/'restore-e0d3-client.pid').write_text(str(client.pid)+'\n')
  run('reboot');code=client.wait();print(json.dumps({'restore_collector_exit':code}),flush=True);raise SystemExit(code)
