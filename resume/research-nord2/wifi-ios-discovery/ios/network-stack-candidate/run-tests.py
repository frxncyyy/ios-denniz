#!/usr/bin/env python3
"""Private host verification only. Does not build a kernel or mutate canonical."""
from pathlib import Path
import hashlib,json,shutil,subprocess
r=Path(__file__).resolve().parent; root=r.parents[4]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert digest(root/'shim/nord2-xnu/wifi_ios_registry.cpp')=='2a38f9c80acdb62536b11f59dd5947fe24d280142cbf763c295a2a5198d286e0','canonical base changed; review/rebase explicitly'
assert (r/'network-stack.inc').read_text() in (r/'wifi_ios_registry.cpp').read_text()
t=r/'test-root';(t/'iokit/Kernel/arm').mkdir(parents=True,exist_ok=True);(t/'bsd/dev').mkdir(parents=True,exist_ok=True)
for f in ['wifi_ios_registry.cpp','test-registry-xnu.h']:shutil.copyfile(r/f,t/'iokit/Kernel/arm'/f)
for p in (root/'shim/nord2-xnu').glob('*.h'):shutil.copyfile(p,t/'bsd/dev'/p.name)
base=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-g']
receipt={'status':'PASS','scope':'host mock IOKit/BSD primitives; actual private adapter and expanded registry C++ bodies','kernel_build':False,'tests':[]}
for name,args in [('test-stack',[]),('test-registry',['-Wno-unused-parameter','-I',str(t/'iokit/Kernel/arm')])]:
 command=base+args+[str(r/(name+'.cpp')),'-o',str(r/name)]
 compile_result=subprocess.run(command,capture_output=True,text=True)
 (r/(name+'.compile.log')).write_text(compile_result.stdout+compile_result.stderr)
 assert compile_result.returncode==0,compile_result.stderr
 result=subprocess.run([str(r/name)],capture_output=True,text=True)
 (r/(name+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0 and 'PASS ' in result.stdout,result.stderr
 receipt['tests'].append({'name':name,'command':command,'source_sha256':digest(r/(name+'.cpp')),'binary_sha256':digest(r/name),'exit':result.returncode,'result':result.stdout.splitlines()[-1]})
receipt['adapter_sha256']=digest(r/'network-stack.inc');receipt['expanded_registry_sha256']=digest(r/'wifi_ios_registry.cpp')
(r/'test-results.json').write_text(json.dumps(receipt,indent=2)+'\n')
print('\n'.join(v['result'] for v in receipt['tests']))
