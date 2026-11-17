from pathlib import Path
import json,shlex,subprocess,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());O=P/'verification/native-client-final';O.mkdir(exist_ok=False);D=O/'xnu/bsd/dev';A=O/'xnu/iokit/Kernel/arm';D.mkdir(parents=True);A.mkdir(parents=True)
for folder in (R/'shim/nord2-xnu',P/'proposed/shim/nord2-xnu'):
 for p in folder.iterdir():
  if p.is_file():((A if p.suffix=='.cpp' else D)/p.name).write_bytes(p.read_bytes())
line=next(l for l in (R/'research/ci-39932615713/build.log').read_text().splitlines() if 'clang++ -o wifi_ios_client.cpo -c ' in l)
raw=shlex.split(line[line.index('/Applications/'):]);flags=[];i=1
while i<len(raw):
 a=raw[i]
 if a in ('-o','-MF','-arch'):i+=2;continue
 if a.startswith('-I'):
  i+=1 if a!='-I' else 2;continue
 if a.endswith('wifi_ios_client.cpp'):i+=1;continue
 if a.startswith('-mmacosx-version-min'):i+=1;continue
 flags.append(a);i+=1
old=json.loads((R/'research/nord2/wifi-vcn33/ios/focused/commands.json').read_text());t=next(x for x in old if x['name']=='sweet_touch')
inc=[a for a in t['argv'] if a.startswith('-I') and a!='-I'+str(R/'shim/nord2-xnu')]
cmd=['clang++','-target','arm64-apple-macos14.0',*flags,*inc,'-I'+str(D),'-I'+str(R/'research/ci-39158169212/buildtree/xnu/iokit'),'-include','meta_features.h','-fstack-usage','-o',str(O/'wifi_ios_client.o'),'-MF',str(O/'wifi_ios_client.d'),str(A/'wifi_ios_client.cpp')]
with (O/'compile.log').open('w') as f:code=subprocess.run(cmd,cwd=t['cwd'],stdout=f,stderr=subprocess.STDOUT).returncode
(O/'report.json').write_text(json.dumps({'exit':code,'argv':cmd,'source_sha256':hashlib.sha256((A/'wifi_ios_client.cpp').read_bytes()).hexdigest(),'parent_final_merged_compile_still_required':True},indent=2)+'\n');print('client ARM64',code)
if code:print((O/'compile.log').read_text()[-5000:]);raise SystemExit(code)
