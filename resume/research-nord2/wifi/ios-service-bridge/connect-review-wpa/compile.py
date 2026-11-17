from pathlib import Path
import tempfile,json,subprocess,shutil,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());V=P/'verification/native';V.mkdir(exist_ok=True)
rows=json.loads((R/'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json').read_text());row=next(x for x in rows if x['name']=='wifi_default_feature');out=[]
with tempfile.TemporaryDirectory(prefix='wpa-followup-native-')as td:
 t=Path(td)/'xnu';d=t/'bsd/dev';d.mkdir(parents=True);(t/'libkern/crypto').mkdir(parents=True);(t/'libkern/crypto/sweet_bearssl').symlink_to(R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl')
 for f in(R/'shim/nord2-xnu').iterdir():
  if f.suffix in('.h','.c','.inc'):shutil.copy2(f,d/f.name)
 for f in(P/'proposed/shim/nord2-xnu').iterdir():shutil.copy2(f,d/f.name)
 for n in ['wifi_wpa2','wifi_station_transaction','wifi_wpa_owner','wifi_wpa_nst']:
  cmd=row['argv'].copy();cmd[cmd.index('-c')+1]=str(d/(n+'.c'));cmd[cmd.index('-o')+1]=str(V/(n+'.o'));r=subprocess.run(cmd,cwd=row.get('cwd',str(R)),capture_output=True,text=True);(V/(n+'.log')).write_text(r.stdout+r.stderr);print(n,r.returncode);assert not r.returncode,r.stderr;out.append({'unit':n,'source_sha256':hashlib.sha256((d/(n+'.c')).read_bytes()).hexdigest(),'argv':cmd,'exit':r.returncode})
(V/'report.json').write_text(json.dumps({'status':'PASS','scope':'Recorded selected generic ARMv8.2 pure TU compile, not linked/runtime','units':out},indent=2)+'\n')
