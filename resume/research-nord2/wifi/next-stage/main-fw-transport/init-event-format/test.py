#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,json
p=Path(__file__).resolve().parent;shim=p.parents[5]/'shim/nord2-xnu'
common=['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(shim)]
commands=[]
with tempfile.TemporaryDirectory(prefix='nord2-init-format-') as temp:
 for name in ['transport','native-finish','specific','native-guards']:
  sources=[p/'wifi_main_transport.c',shim/'wifi_main_command.c'] if name!='specific' else [p/'wifi_main_layout.c',p/'wifi_main_phy_force.c']
  if name=='native-guards':sources += [shim/'wifi_main_initializer.c',shim/'wifi_main_dmashdl.c',shim/'wifi_main_init_plan.c',shim/'wifi_dma_preflight.c',shim/'dma_pool_contract.c',p/'wifi_main_layout.c']
  cmd=common+(['-DNFA_HOST_TEST'] if name=='specific' else [])+list(map(str,sources))+[str(p/('test-'+name+'.c'))]+(['-lcrypto'] if name=='specific' else [])+['-o',temp+'/'+name]
  subprocess.run(cmd,check=True);r=subprocess.run([temp+'/'+name],capture_output=True,text=True);(p/('test-'+name+'.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr;print(r.stdout.splitlines()[-1]);commands.append({'name':name,'argv':cmd[:-1]+['TEMP_BINARY'],'returncode':r.returncode})
(p/'test-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
