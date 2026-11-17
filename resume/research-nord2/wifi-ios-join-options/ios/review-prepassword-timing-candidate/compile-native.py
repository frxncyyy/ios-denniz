#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
R=next(p for p in Path(__file__).resolve().parents if(p/'AGENTS.md').exists());D=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,default=D/'native');a=ap.parse_args();W=a.output.resolve();W.mkdir(parents=True,exist_ok=True)
prior=R/'research/nord2/wifi-ios-identity/ios/review-join-profile/v2/native/compile.json'
rec=next(r for r in json.loads(prior.read_text())if r['name']=='wifi_ios_client');cmd=rec['argv'][:]
with tempfile.TemporaryDirectory(prefix='query-timing-native-')as t:
 root=Path(t)/'xnu';dev=root/'bsd/dev';arm=root/'iokit/Kernel/arm';dev.mkdir(parents=True);arm.mkdir(parents=True)
 for f in (R/'shim/nord2-xnu').glob('*.h'):(dev/f.name).write_bytes(f.read_bytes())
 src=arm/'wifi_ios_client.cpp';src.write_bytes((D/'proposed/shim/nord2-xnu/wifi_ios_client.cpp').read_bytes());obj=W/'wifi_ios_client.o'
 inputs=[i for i,s in enumerate(cmd)if s.endswith('/wifi_ios_client.cpp')];assert len(inputs)==1;cmd[inputs[0]]=str(src);cmd[cmd.index('-o')+1]=str(obj)
 r=subprocess.run(cmd,cwd=rec['cwd'],capture_output=True,text=True);(W/'compile.log').write_text(r.stdout+r.stderr)
 out={'name':'wifi_ios_client','argv':cmd,'cwd':rec['cwd'],'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'returncode':r.returncode,'limitations':['Compile only; no link/runtime proof.','Same selected XNU normal generic ARMv8.2 context; local Apple type-summary builtins use prior documented stand-ins.'],'command_template_sha256':hashlib.sha256(prior.read_bytes()).hexdigest()}
 if obj.exists():out['object_sha256']=hashlib.sha256(obj.read_bytes()).hexdigest();out['object_bytes']=obj.stat().st_size
 (W/'compile.json').write_text(json.dumps(out,indent=2)+'\n');r.check_returncode();print('PASS selected normal ARM64 owning client TU')
