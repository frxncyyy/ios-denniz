#!/usr/bin/env python3
from pathlib import Path
import json,subprocess,hashlib,tempfile,shutil
p=Path(__file__).resolve().parent;r=p.parents[5];proof=r/'research/nord2/wifi-main-swint/focused/sweet_touch.c.json';base=json.loads(proof.read_text());out=p/'verification';out.mkdir(exist_ok=True)
h=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
commands=[]
for name in ['sweet_touch','wifi_capability','wifi_capability_dispatcher']:
 argv=base['argv'].copy();argv[argv.index('-c')+1]=str(p/'proposed/bsd/dev'/(name+'.c'));argv[argv.index('-o')+1]=str(out/(name+'.o'))
 assert len(argv)==len(base['argv'])
 assert all(a==b for i,(a,b) in enumerate(zip(argv,base['argv'])) if i not in [argv.index('-c')+1,argv.index('-o')+1])
 cp=subprocess.run(argv,cwd=base['cwd'],capture_output=True,text=True);(out/(name+'.log')).write_text(cp.stdout+cp.stderr);assert cp.returncode==0,cp.stderr[-4000:];commands.append({'cwd':base['cwd'],'argv':argv})
nm=r.parent/'toolchains/proton-clang/bin/llvm-nm';symbols={}
for n in ['sweet_touch','wifi_capability','wifi_capability_dispatcher']:
 symbols[n]=subprocess.check_output([str(nm),str(out/(n+'.o'))],text=True)
assert ' U _ncd_run' in symbols['sweet_touch'] and '_ncd_worker' in symbols['sweet_touch']
assert ' T _ncp_parse' in symbols['wifi_capability'] and ' T _ncp_query' in symbols['wifi_capability']
assert ' T _ncd_run' in symbols['wifi_capability_dispatcher'] and ' U _ncp_query' in symbols['wifi_capability_dispatcher']
(out/'symbols.txt').write_text(''.join(n+'\n'+s for n,s in symbols.items()));(out/'compile-commands.json').write_text(json.dumps({'prior_command_sha256':h(proof),'substitutions_only':['-c source','-o output'],'no_extra_include_or_forced_NORD2':True,'commands':commands},indent=2)+'\n')
# Reproduce integration edits only inside a temp tree.
inputs={'bsd/dev/sweet_touch.c':r/'research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_touch.c','bsd/conf/files':r/'research/ci-39158169212/buildtree/xnu/bsd/conf/files','tools/nord2-mkpatch.sh':r/'tools/nord2-mkpatch.sh'}
with tempfile.TemporaryDirectory() as d:
 d=Path(d)
 for rel,original in inputs.items():f=d/rel;f.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,f)
 with (p/'integration.patch').open('rb') as stdin:subprocess.run(['patch','-p1','-s'],cwd=d,stdin=stdin,check=True)
 for rel in inputs:assert (d/rel).read_bytes()==(p/'proposed'/rel).read_bytes()
print('PASS exact prior focused argv with onlysource/output substitutions; normal NORD2 export, sibling includes; three nonempty objects; temp patch exact')
