#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent;r=p.parents[5];s=r/'shim/nord2-xnu'
flags=['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(p/'baseline')]
with tempfile.TemporaryDirectory() as d:
 for name in ['test-dispatcher','test-native-adapter','test-current-owner']:
  deps=[s/'wifi_main_command.c',s/'dma_pool_contract.c']
  if name=='test-current-owner':deps += [s/('wifi_main_'+n+'.c') for n in ['initializer','dmashdl','init_plan','transport','layout']]+[s/'wifi_dma_preflight.c']
  cmd=flags+[str(p/'dispatcher.c'),str(p.parent/'capability.c'),str(p/(name+'.c'))]+list(map(str,deps))+['-o',d+'/'+name]
  subprocess.run(cmd,check=True);q=subprocess.run([d+'/'+name],capture_output=True,text=True);(p/(name+'.log')).write_text(q.stdout+q.stderr);q.check_returncode();print(q.stdout.splitlines()[-1])
 cmd=flags+[str(p/'test-drain.c'),'-o',d+'/drain'];subprocess.run(cmd,check=True);q=subprocess.run([d+'/drain'],capture_output=True,text=True);(p/'test-drain.log').write_text(q.stdout+q.stderr);q.check_returncode();print(q.stdout.strip())
 for name,inc in [('snapshot',p/'baseline'),('canonical',s)]:subprocess.run([f for f in flags if not f.startswith('-I')]+['-I'+str(inc),'-c',str(p/('abi-'+name+'.c')),'-o',d+'/'+name+'.o'],check=True)
 subprocess.run(flags+[d+'/snapshot.o',d+'/canonical.o',str(p/'test-abi-layout.c'),'-o',d+'/abi'],check=True);q=subprocess.run([d+'/abi'],capture_output=True,text=True);(p/'test-abi-layout.log').write_text(q.stdout+q.stderr);q.check_returncode();print(q.stdout.strip())
