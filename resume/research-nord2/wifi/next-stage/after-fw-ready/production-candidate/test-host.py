#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent;r=p.parents[5];s=r/'shim/nord2-xnu';c=p/'canonical/shim/nord2-xnu';f=p/'tests';logs=p/'verification';logs.mkdir(exist_ok=True)
flags=['cc','-O1','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(c),'-I'+str(p/'proposed/bsd/dev')]
with tempfile.TemporaryDirectory() as d:
 for name in ['test-dispatcher','test-native-adapter','test-current-owner','test-drain']:
  deps=[] if name=='test-drain' else [c/'wifi_capability.c',c/'wifi_capability_dispatcher.c',s/'wifi_main_command.c',s/'dma_pool_contract.c']
  if name=='test-current-owner':deps += [s/('wifi_main_'+n+'.c') for n in ['initializer','dmashdl','init_plan','transport','layout']]+[s/'wifi_dma_preflight.c']
  subprocess.run(flags+[str(f/(name+'.c'))]+list(map(str,deps))+['-o',d+'/'+name],check=True)
  cp=subprocess.run([d+'/'+name],capture_output=True,text=True);(logs/(name+'.log')).write_text(cp.stdout+cp.stderr);cp.check_returncode();print(cp.stdout.splitlines()[-1])
