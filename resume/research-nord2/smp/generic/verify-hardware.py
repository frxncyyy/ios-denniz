from pathlib import Path
import subprocess,hashlib,json,re,sys
n=int(sys.argv[1]);assert n in (2,4,8)
logdir=Path(sys.argv[2]);assert logdir.name.endswith(f"smp-generic{n}-stop20")
out=Path(f"research/nord2/smp/generic/hardware-test-{n}")
adb="$HOME/Android/Sdk/platform-tools/adb";serial="<redacted-serial>"
r=subprocess.run([adb,"-s",serial,"shell","-T","id; cat /nord2-rescue; cat /proc/mounts"],capture_output=True,text=True,timeout=20,check=True)
(out/"rescue.txt").write_text(r.stdout)
assert "uid=0(root)" in r.stdout and "ADB RAM recovery; userdata is not mounted" in r.stdout
assert not any(l.startswith("/dev/block") or " /data " in l for l in r.stdout.splitlines())
p=logdir/"pstore/pmsg-ramoops-0";b=p.read_bytes();s=b.decode("utf-8","replace")
workers=re.findall(r"NORD2 SMP: worker=(\d+) actual=(\d+) MIDR=([0-9a-f]+) done=(\d+) ticks=(\d+) ipi_reply=(\d+) ipi_irqs=(\d+) ok=(\d+)",s)
assert len(workers)==n
for i,row in enumerate(workers):
 core,actual,midr,done,ticks,reply,irqs,ok=row
 assert int(core)==int(actual)==i and int(done)==1 and int(ticks)>0 and int(reply)==i+1 and int(irqs)>0 and int(ok)==1
 assert (int(midr,16)>>4)&0xfff==(0xd05 if i<4 else 0xd41)
marker=f"NORD2 SMP: scheduler result {n}/{n} counter={n*4096} expected={n*4096} console_dropped=0 PASS=1"
assert marker in s and "PANIC:" not in s
assert s.index(marker)<s.index("NORD2 SMP: XNU secondaries all OFF; CPU0 return permitted")<s.index("RETURN TO STOCK ANDROID KERNEL")
v=dict(result="passed",xnu_scheduler_cpus=n,workers=workers,counter=n*4096,all_secondaries_off_before_return=True,rescue_returned=True,userdata_not_mounted=True,pmsg=str(p),pmsg_bytes=len(b),pmsg_sha256=hashlib.sha256(b).hexdigest())
(out/"verified.json").write_text(json.dumps(v,indent=2)+"\n");print(json.dumps(v,indent=2))
