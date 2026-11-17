from pathlib import Path
import tempfile,json,subprocess,hashlib,shutil
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if p.name=='ios-sweet')
row=next(x for x in json.loads((R/'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json').read_text()) if x['name']=='wifi_normal_backend')
records=[]
with tempfile.TemporaryDirectory(prefix='nss-helper-') as td:
 d=Path(td)
 for f in (R/'shim/nord2-xnu').iterdir():
  if f.suffix=='.h':shutil.copy2(f,d/f.name)
 for f in (P/'proposed').iterdir():shutil.copy2(f,d/f.name)
 for name in ('wifi_normal_transport','wifi_normal_rx'):
  cmd=row['argv'].copy();cmd[cmd.index('-c')+1]=str(d/(name+'.c'));cmd[cmd.index('-o')+1]=str(P/(name+'.arm64.o'))
  result=subprocess.run(cmd,cwd=row['cwd'],capture_output=True,text=True)
  (P/(name+'.compile.log')).write_text(result.stdout+result.stderr)
  records.append({'name':name,'argv':cmd,'cwd':row['cwd'],'returncode':result.returncode,'qualification':'Recorded normal generic ARM64 flags unchanged; only source/output substituted with private sibling header overlay. No extra -I or forced NORD2. Compile only.'})
  assert not result.returncode,result.stderr
(P/'compile-helpers.json').write_text(json.dumps(records,indent=2)+'\n');print('PASS two normal-context ARM64 helper TUs')
