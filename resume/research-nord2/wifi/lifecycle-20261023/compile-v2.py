from pathlib import Path
import json,subprocess,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());B=R/'research/ci-39158169212/buildtree/xnu';O=P/'v2/verification/arm64';O.mkdir(exist_ok=False);D=O/'dev';D.mkdir()
for f in(R/'shim/nord2-xnu').iterdir():
 if f.is_file() and f.suffix in('.h','.c','.inc'):(D/f.name).write_bytes(f.read_bytes())
for f in(P/'v2/candidate/shim/nord2-xnu').iterdir():(D/f.name).write_bytes(f.read_bytes())
(D/'sweet_touch.c').write_bytes((B/'bsd/dev/sweet_touch.c').read_bytes())
t=next(x for x in json.loads((R/'research/nord2/wifi-vcn33/ios/focused/commands.json').read_text())if x['name']=='sweet_touch');results=[]
for name in('wifi_station_transport','wifi_ios_event','sweet_touch'):
 src=D/(name+'.c');cmd=[x for x in t['argv'] if x!='-I'+str(R/'shim/nord2-xnu')];cmd[cmd.index('-c')+1]=str(src);cmd[cmd.index('-o')+1]=str(O/(name+'.o'))
 if '-MF'in cmd:cmd[cmd.index('-MF')+1]=str(O/(name+'.d'))
 with(O/(name+'.log')).open('wb')as log:p=subprocess.run(cmd,cwd=t['cwd'],stdout=log,stderr=subprocess.STDOUT)
 results.append({'name':name,'exit':p.returncode,'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'argv':cmd});(O/'report.json').write_text(json.dumps(results,indent=2)+'\n');print(name,p.returncode,flush=True)
 if p.returncode:print((O/(name+'.log')).read_text()[-3500:]);raise SystemExit(p.returncode)
