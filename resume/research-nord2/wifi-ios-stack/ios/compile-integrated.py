from pathlib import Path
import hashlib,json,subprocess
R=Path(__file__).resolve().parents[4];P=Path(__file__).resolve().parent;O=P/'focused';O.mkdir(exist_ok=True)
source=R/'research/ci-39158169212/buildtree/xnu/iokit/Kernel/arm/wifi_ios_registry.cpp'
prior=json.loads((R/'research/nord2/wifi-ios-discovery/ios/focused/commands.json').read_text())
record=next(x for x in prior if x['name']=='wifi_ios_client');cmd=record['argv'].copy()
index=next(i for i,x in enumerate(cmd) if x.endswith('/wifi_ios_client.cpp'));cmd[index]=str(source)
cmd[cmd.index('-o')+1]=str(O/'wifi_ios_registry.o')
assert '-mcpu=generic' in cmd and '-march=armv8.2-a+crypto' in cmd
with (O/'wifi_ios_registry.log').open('wb') as log:r=subprocess.run(cmd,cwd=record['cwd'],stdout=log,stderr=subprocess.STDOUT)
(O/'commands.json').write_text(json.dumps([dict(name='wifi_ios_registry',argv=cmd,cwd=record['cwd'],source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),exit=r.returncode)],indent=2)+'\n')
if r.returncode: print((O/'wifi_ios_registry.log').read_text())
print('actual generated wifi_ios_registry compile exit',r.returncode);raise SystemExit(r.returncode)
