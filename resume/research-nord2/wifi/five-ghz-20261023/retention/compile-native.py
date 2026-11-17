from pathlib import Path
import json,subprocess,hashlib,re
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());S=R/'shim/nord2-xnu';C=P/'proposed/shim/nord2-xnu';B=R/'research/ci-39158169212/buildtree/xnu'
O=P/'verification/native-final-seal';O.mkdir(exist_ok=False);D=O/'source';D.mkdir()
for p in S.iterdir():
 if p.is_file():(D/p.name).write_bytes(p.read_bytes())
for p in C.iterdir():(D/p.name).write_bytes(p.read_bytes())
(D/'sweet_touch.c').write_bytes((B/'bsd/dev/sweet_touch.c').read_bytes())
old=json.loads((R/'research/nord2/wifi-vcn33/ios/focused/commands.json').read_text());template=next(x for x in old if x['name']=='sweet_touch');reports=[]
for name in ('wifi_ios_snapshot','wifi_ios_lifecycle','wifi_scan_bss','sweet_touch'):
 argv=[a for a in template['argv'] if a!='-I'+str(S)];argv.insert(1,'-I'+str(D));argv[argv.index('-c')+1]=str(D/(name+'.c'));argv[argv.index('-o')+1]=str(O/(name+'.o'))
 if '-MF' in argv:argv[argv.index('-MF')+1]=str(O/(name+'.d'))
 argv+=['-fstack-usage'];assert '-mcpu=generic' in argv and '-march=armv8.2-a+crypto' in argv
 with (O/(name+'.log')).open('w') as f:code=subprocess.run(argv,cwd=template['cwd'],stdout=f,stderr=subprocess.STDOUT).returncode
 reports.append({'name':name,'exit':code,'argv':argv,'source_sha256':hashlib.sha256((D/(name+'.c')).read_bytes()).hexdigest()});print(name,code,flush=True)
 (O/'commands.json').write_text(json.dumps(reports,indent=2)+'\n')
 if code:print((O/(name+'.log')).read_text()[-4000:]);raise SystemExit(code)
stacks={}
for p in O.glob('*.su'):
 for line in p.read_text().splitlines():
  cols=line.split('\t')
  if len(cols)>=3:
   name=cols[0].rsplit(':',1)[-1];stacks[name]={'bytes':int(cols[1]),'kind':cols[2]}
(O/'stack-usage.json').write_text(json.dumps(stacks,indent=2)+'\n');print('frame_max',max(v['bytes'] for v in stacks.values()))
