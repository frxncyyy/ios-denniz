from pathlib import Path
import subprocess,hashlib,json,re,sys
out=Path('research/nord2/smp/userland')
logdir=Path(sys.argv[1]);assert logdir.name.endswith('smp-usb-userland-stop21')
adb='$HOME/Android/Sdk/platform-tools/adb'
serial='<redacted-serial>'
r=subprocess.run([adb,'-s',serial,'shell','-T','id; cat /nord2-rescue; cat /proc/mounts'],capture_output=True,text=True,timeout=20,check=True)
(out/'rescue.txt').write_text(r.stdout)
assert 'uid=0(root)' in r.stdout and 'ADB RAM recovery; userdata is not mounted' in r.stdout
assert not any(l.startswith('/dev/block') or ' /data ' in l for l in r.stdout.splitlines())
p=logdir/'pstore/pmsg-ramoops-0';b=p.read_bytes();s=b.decode('utf-8','replace')
workers=re.findall(r'NORD2 SMP: worker=(\d+) actual=(\d+) MIDR=([0-9a-f]+) done=(\d+) ticks=(\d+) ipi_reply=(\d+) ipi_irqs=(\d+) ok=(\d+)',s)
assert len(workers)==8
for i,row in enumerate(workers):
 core,actual,midr,done,ticks,reply,irqs,ok=row
 assert int(core)==int(actual)==i and int(done)==1 and int(ticks)>0 and int(reply)==i+1 and int(irqs)>0 and int(ok)==1
 assert (int(midr,16)>>4)&0xfff==(0xd05 if i<4 else 0xd41)
markers=['NORD2 SMP: scheduler result 8/8 counter=32768 expected=32768 console_dropped=0 PASS=1',
 'NORD2 SMP: all8 passed; continuing to USB and RAM userland',
 'NORD2 USB: service bound to CPU0',
 'NORD2 USB: background console ready; continuing BSD boot',
 'NORD2 SMP: XNU secondaries all OFF; CPU0 return permitted',
 'NORD2 USB: controller restored before kernel return',
 'RETURN TO STOCK ANDROID KERNEL']
positions=[s.index(m) for m in markers];assert positions==sorted(positions)
live=(out/'hardware-live/xnu-live.log').read_bytes();assert b.startswith(live)
usb=json.loads((out/'hardware-live/result.json').read_text())
assert usb['native_usb'] and not usb['userdata_commands_sent'] and usb['log_bytes']==len(live)
assert usb['log_sha256']==hashlib.sha256(live).hexdigest()
assert usb['status_samples'] and all(x[5]==0 and x[6]==1 for x in usb['status_samples'])
# PID1 and HFS markers must be checked against exact diagnostics after capture.
report=dict(result='smp_usb_cleanup_verified_pid1_markers_pending',workers=workers,counter=32768,
 all_secondaries_off_before_usb_cleanup=True,usb_restored_before_return=True,rescue_root_verified=True,
 block_filesystems_mounted=False,live_is_exact_pmsg_prefix=True,live_bytes=len(live),
 live_sha256=hashlib.sha256(live).hexdigest(),pmsg_path=str(p),pmsg_bytes=len(b),
 pmsg_sha256=hashlib.sha256(b).hexdigest(),usb_last_status=usb['status_samples'][-1])
(out/'hardware-partial.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
