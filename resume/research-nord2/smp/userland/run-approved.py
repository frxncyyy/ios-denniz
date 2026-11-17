# Run only after the human authorizes this exact boot image.
from pathlib import Path
import hashlib,subprocess,datetime,json
out=Path('research/nord2/smp/userland')
img=Path('shim/out/nord2-xnu/nord2-smp-usb-userland-stop21.img')
backup=Path('backup/nord2/boot.img')
for p,size,digest in [(img,33552384,'c5ce4445204cf81fa4d77afba3126775a9a14f15344947f6bd83ce01f368e86a'),(backup,33554432,'04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01')]:
 assert p.stat().st_size==size and hashlib.sha256(p.read_bytes()).hexdigest()==digest
ci=json.load(open('research/ci-39437387549/run.json'));assert ci['conclusion']=='success' and ci['headSha']=='1f5efbaff13fa3bbcf596cf0718a6b970cde91e6'
fb='$HOME/Android/Sdk/platform-tools/fastboot';serial='<redacted-serial>'
r=subprocess.run([fb,'devices'],capture_output=True,text=True,timeout=10,check=True)
assert [l.split() for l in r.stdout.splitlines() if l.strip()]==[[serial,'fastboot']]
assert not (out/'hardware-live').exists()
with (out/'flash.log').open('x') as log:
 def run(*args):
  r=subprocess.run([fb,'-s',serial,*args],capture_output=True,text=True,timeout=120);s=r.stdout+r.stderr
  log.write(datetime.datetime.now().astimezone().isoformat()+' '+repr(args)+'\n'+s);log.flush();print(s,flush=True);r.check_returncode();return s
 assert 'product: oplus6893' in run('getvar','product')
 assert 'partition-size:boot: 2000000' in run('getvar','partition-size:boot')
 assert 'is-userspace: no' in run('getvar','is-userspace')
 run('flash','boot',str(img))
 with (out/'client-hardware.log').open('x') as clientlog, (out/'rescue-collector.log').open('x') as rescuolog:
  client=subprocess.Popen(['python3','tools/nord2-usb-console.py',str(out/'hardware-live'),'--wait','240','--read-seconds','60'],stdout=clientlog,stderr=subprocess.STDOUT)
  rescue=subprocess.Popen(['tools/nord2-pull-logs.sh','--rescue','smp-usb-userland-stop21'],stdout=rescuolog,stderr=subprocess.STDOUT)
  run('reboot')
  rr=rescue.wait();cr=client.wait()
  print(json.dumps(dict(rescue_collector_exit=rr,usb_client_exit=cr)),flush=True)
  if rr or cr: raise SystemExit(1)
