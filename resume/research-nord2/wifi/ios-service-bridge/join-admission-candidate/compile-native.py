#!/usr/bin/env python3
from pathlib import Path
import tempfile,subprocess,json,argparse,hashlib
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if(x/'AGENTS.md').exists());a=argparse.ArgumentParser();a.add_argument('--output',type=Path,default=P/'verification/native-compile');args=a.parse_args();args.output.mkdir(parents=True,exist_ok=True)
commands=json.loads((R/'research/nord2/wifi-ios-security/ios/focused/commands.json').read_text());out=[]
with tempfile.TemporaryDirectory(prefix='join-native-compile-') as td:
 D=Path(td);dev=D/'xnu/bsd/dev';arm=D/'xnu/iokit/Kernel/arm';dev.mkdir(parents=True);arm.mkdir(parents=True)
 for folder in [R/'shim/nord2-xnu',P.parent/'connect-review-wpa/proposed/shim/nord2-xnu',R/'research/nord2/wifi/claude-opus-connect-review/review-native/v2/candidate',P/'proposed/shim/nord2-xnu']:
  for f in folder.iterdir():
   if f.suffix in ('.c','.h','.inc','.cpp'):((arm if f.suffix=='.cpp' else dev)/f.name).write_bytes(f.read_bytes())
 for old in commands:
  name=old['name'];cmd=old['argv'][:];source=next(x for x in cmd if x.endswith('/sweet_touch.c')or x.endswith('/wifi_ios_client.cpp'))
  target=(dev/'sweet_touch.c')if name=='sweet_touch'else arm/'wifi_ios_client.cpp'
  if name=='sweet_touch':target.write_bytes(Path(source).read_bytes())
  cmd[cmd.index(source)]=str(target);cmd[cmd.index('-o')+1]=str(args.output/(name+'.o'));cmd.insert(1,'-I'+str(dev))
  run=subprocess.run(cmd,cwd=old['cwd'],capture_output=True,text=True);(args.output/(name+'.log')).write_text(run.stdout+run.stderr);print(name,run.returncode);run.check_returncode();out.append(dict(name=name,argv=cmd,exit=0,source_sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
  if name=='sweet_touch':
   cmd[cmd.index(str(target))]=str(dev/'wifi_ios_join.c');cmd[cmd.index('-o')+1]=str(args.output/'wifi_ios_join.o');run=subprocess.run(cmd,cwd=old['cwd'],capture_output=True,text=True);(args.output/'wifi_ios_join.log').write_text(run.stdout+run.stderr);print('wifi_ios_join',run.returncode);run.check_returncode();out.append(dict(name='wifi_ios_join',argv=cmd,exit=0))
(args.output/'report.json').write_text(json.dumps(out,indent=2)+'\n')
