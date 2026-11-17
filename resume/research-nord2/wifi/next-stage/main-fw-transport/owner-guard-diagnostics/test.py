#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent;r=p.parents[5];s=r/'shim/nord2-xnu'
with tempfile.TemporaryDirectory() as d:
 cmd=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DNFA_HOST_TEST','-I'+str(p),'-I'+str(s),str(p/'test-native.c')]+[str(s/('wifi_main_'+n+'.c')) for n in ['initializer','dmashdl','init_plan','transport','command','layout']]+[str(s/'wifi_dma_preflight.c'),str(s/'dma_pool_contract.c'),'-lcrypto','-o',d+'/test']
 subprocess.run(cmd,check=True);q=subprocess.run([d+'/test'],capture_output=True,text=True);(p/'test-native.log').write_text(q.stdout+q.stderr);q.check_returncode();print(q.stdout.splitlines()[-2])
