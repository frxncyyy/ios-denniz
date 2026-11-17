from pathlib import Path
import tempfile,json,subprocess,hashlib
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if x.name=='ios-sweet');S=R/'shim/nord2-xnu';base=R/'research/nord2/wifi-ios-scan/ios/focused/commands.json';rows=json.loads(base.read_text());records=[];B=P/'_build';B.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='niwj-normal-') as t:
 d=Path(t);dev=d/'xnu/bsd/dev';arm=d/'xnu/iokit/Kernel/arm';dev.mkdir(parents=True);arm.mkdir(parents=True)
 for f in S.iterdir():
  if f.suffix=='.h':(dev/f.name).write_bytes(f.read_bytes())
 for f in (P/'proposed/shim/nord2-xnu').iterdir():((arm if f.suffix=='.cpp' else dev)/f.name).write_bytes(f.read_bytes())
 for name in ('wifi_ios_client','wifi_ios_join','wifi_ios_station_view'):
  cpp=name=='wifi_ios_client';baseline='wifi_ios_client' if cpp else 'wifi_ios_control';row=next(x for x in rows if x['name']==baseline);cmd=row['argv'].copy();source=(arm if cpp else dev)/(name+('.cpp' if cpp else '.c'));oldsource=next(x for x in cmd if x.endswith('/'+baseline+('.cpp' if cpp else '.c')));cmd[cmd.index(oldsource)]=str(source);output=B/(name+'.arm64.o');cmd[cmd.index('-o')+1]=str(output)
  result=subprocess.run(cmd,cwd=row['cwd'],capture_output=True,text=True);(B/(name+'.compile.log')).write_text(result.stdout+result.stderr)
  records.append({'name':name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'argv':cmd,'cwd':row['cwd'],'returncode':result.returncode,'object_sha256':hashlib.sha256(output.read_bytes()).hexdigest() if result.returncode==0 else None,'qualification':'Normal generic ARM64 flags/cwd unchanged; only source/output substituted; private normal sibling layout. Compile only, no linked kernel or live native API proof.'});print(name,result.returncode);assert not result.returncode,result.stderr[-4000:]
(P/'compile-record.json').write_text(json.dumps(records,indent=2)+'\n')
