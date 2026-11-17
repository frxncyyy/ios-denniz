from pathlib import Path
import tempfile,json,subprocess,hashlib
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if x.name=='ios-sweet');S=R/'shim/nord2-xnu';base=R/'research/nord2/wifi-ios-scan/ios/focused/commands.json';rows=json.loads(base.read_text());records=[]
with tempfile.TemporaryDirectory(prefix='niwl-compat-normal-') as t:
 d=Path(t);dev=d/'xnu/bsd/dev';arm=d/'xnu/iokit/Kernel/arm';dev.mkdir(parents=True);arm.mkdir(parents=True)
 for f in S.iterdir():
  if f.suffix=='.h':(dev/f.name).write_bytes(f.read_bytes())
 for f in (P/'proposed').iterdir():((arm if f.suffix=='.cpp' else dev)/f.name).write_bytes(f.read_bytes())
 for name in ('wifi_ios_client','wifi_ios_scalar'):
  row=next(x for x in rows if x['name']==('wifi_ios_client' if name=='wifi_ios_client' else 'wifi_ios_control'));cmd=row['argv'].copy();source=(arm if name=='wifi_ios_client' else dev)/(name+('.cpp' if name=='wifi_ios_client' else '.c'));oldsource=next(x for x in cmd if x.endswith('/'+('wifi_ios_client.cpp' if name=='wifi_ios_client' else 'wifi_ios_control.c')));cmd[cmd.index(oldsource)]=str(source);output=P/(name+'.arm64.o');cmd[cmd.index('-o')+1]=str(output)
  result=subprocess.run(cmd,cwd=row['cwd'],capture_output=True,text=True);(P/(name+'.compile.log')).write_text(result.stdout+result.stderr)
  record={'name':name,'baseline_command':str(base.relative_to(R)),'argv':cmd,'cwd':row['cwd'],'returncode':result.returncode,'qualification':'Recorded normal generic flags/cwd unchanged, source/output only substituted; private normal xnu/iokit/Kernel/arm and xnu/bsd/dev sibling layout. No added include flags or forced board macros. Compile only.'};records.append(record);assert not result.returncode,result.stderr[-4000:]
(P/'compile-record.json').write_text(json.dumps(records,indent=2)+'\n');print('PASS normal generic ARM64 client + scalar/channel helper')
