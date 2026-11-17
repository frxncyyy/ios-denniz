#!/usr/bin/env python3
from pathlib import Path
import subprocess
p=Path(__file__).resolve().parent;r=p.parents[5];s=r/'shim/nord2-xnu';logs=p/'verification'
flags=['cc','-O1','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(p),'-I'+str(s)]
for name in ['test-dispatcher','test-native-main','test-current-owner','test-drain','test-header']:
 deps=[] if name=='test-drain' else [p/'wifi_capability.c',p/'wifi_capability_dispatcher.c',s/'wifi_main_command.c',s/'dma_pool_contract.c']
 if name=='test-current-owner':deps += [s/('wifi_main_'+n+'.c') for n in ['initializer','dmashdl','init_plan','transport','layout']]+[s/'wifi_dma_preflight.c']
 command=flags+[str(p/'tests'/(name+'.c'))]+list(map(str,deps))+['-o',str(logs/name)]
 cp=subprocess.run(command,capture_output=True,text=True);(logs/(name+'-compile.log')).write_text(cp.stdout+cp.stderr);cp.check_returncode()
 cp=subprocess.run([str(logs/name)],capture_output=True,text=True);(logs/(name+'.log')).write_text(cp.stdout+cp.stderr);cp.check_returncode();print(cp.stdout.splitlines()[-1])
