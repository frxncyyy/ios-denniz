from pathlib import Path
import hashlib, subprocess, datetime, json, sys
n=int(sys.argv[1]);assert n in (4,8)
out=Path("research/nord2/smp/generic")
previous=json.loads((out/f"hardware-test-{n//2}/verified.json").read_text())
assert previous["result"]=="passed" and previous["xnu_scheduler_cpus"]==n//2
assert previous["all_secondaries_off_before_return"] and previous["rescue_returned"] and previous["userdata_not_mounted"]
expected={4:"f0db75801b4cbef20a8341d53808a691914e675e9e9e0e2d17acfe1ebcda513b",8:"cb172efbfe4c059c8a9c1f7ade25feac758b051847f84602f0669f8251ffc276"}
img=Path(f"shim/out/nord2-xnu/nord2-smp-generic{n}-stop20.img");backup=Path("backup/nord2/boot.img")
for p,size,digest in [(img,33542144,expected[n]),(backup,33554432,"04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01")]:
 assert p.stat().st_size==size and hashlib.sha256(p.read_bytes()).hexdigest()==digest
fb="$HOME/Android/Sdk/platform-tools/fastboot";serial="<redacted-serial>"
r=subprocess.run([fb,"devices"],capture_output=True,text=True,timeout=10,check=True)
assert [l.split() for l in r.stdout.splitlines() if l.strip()]==[[serial,"fastboot"]]
out=out/f"hardware-test-{n}";out.mkdir(exist_ok=True)
with (out/"flash.log").open("x") as log:
 def run(*args):
  r=subprocess.run([fb,"-s",serial,*args],capture_output=True,text=True,timeout=120);s=r.stdout+r.stderr
  log.write(datetime.datetime.now().astimezone().isoformat()+" "+repr(args)+"\n"+s);log.flush();print(s,flush=True);r.check_returncode();return s
 assert "product: oplus6893" in run("getvar","product")
 assert "partition-size:boot: 2000000" in run("getvar","partition-size:boot")
 assert "is-userspace: no" in run("getvar","is-userspace")
 run("flash","boot",str(img));run("reboot")
subprocess.run(["tools/nord2-pull-logs.sh","--rescue",f"smp-generic{n}-stop20"],check=True)
