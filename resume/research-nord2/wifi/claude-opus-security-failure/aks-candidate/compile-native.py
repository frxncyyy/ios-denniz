#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
P=Path(__file__).resolve().parent
R=next(x for x in P.parents if(x/'AGENTS.md').exists())
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path);args=ap.parse_args()
out=args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='nord2-aks-native-'));out.mkdir(parents=True,exist_ok=True)
reference=R/'research/nord2/wifi-ios-security/ios/focused/commands.json'
saved=next(x for x in json.loads(reference.read_text()) if x['name']=='wifi_ios_client')
cmd=list(saved['argv']);assert cmd[-2]=='-o' and cmd[-3].endswith('/wifi_ios_client.cpp')
source=P/'proposed/iokit/Kernel/arm/SweetKeyStore.cpp'
cmd[-3]=str(source);cmd[-1]=str(out/'SweetKeyStore.o')
run=subprocess.run(cmd,cwd=R,capture_output=True,text=True)
(out/'compile.log').write_text(run.stdout+run.stderr)
report=dict(command=cmd,cwd=str(R),exit=run.returncode,
 source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
 reference_commands_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
 scope='Selected XNU headers, actual owning C++ TU, generic ARMv8.2+crypto compile only; no link/hardware proof.')
if not run.returncode:report['object_sha256']=hashlib.sha256((out/'SweetKeyStore.o').read_bytes()).hexdigest()
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
assert run.returncode==0,run.stderr[-6000:]
print(json.dumps(dict(exit=0,source_sha256=report['source_sha256'],output=str(out)),indent=2))
