#!/usr/bin/env python3
from pathlib import Path
import os,subprocess,tempfile
P=Path(__file__).resolve().parent;R=P.parents[4];S=P/'proposed/shim/nord2-xnu';C=R/'shim/nord2-xnu'
flags=[os.getenv('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(S),'-I'+str(C)]
files=[S/'wifi_normal_transport.c',S/'wifi_basic_config.c']+[C/(n+'.c') for n in ('wifi_basic_resources','wifi_basic_metadata','wifi_basic_primitives','wifi_capability','wifi_capability_dispatcher','wifi_main_initializer','wifi_main_dmashdl','wifi_main_init_plan','wifi_main_transport','wifi_main_layout','wifi_dma_preflight','wifi_main_command','dma_pool_contract')]
with tempfile.TemporaryDirectory(prefix='nct-host-') as d:
 logs=[]
 # Instrument only callback failure returns. All native authority/identity/
 # register/memory guards remain the actual candidate, no mocked owner.
 adapter=(S/'wifi_capability_bind.inc').read_text()
 for name in ('owner','read','write','mread','mwrite','sync','delay','now'):
  import re
  pattern=r'(static int ncd_native_'+name+r'\([^)]*\)\s*\{)'
  adapter,n=re.subn(pattern,r'\1if(nct_test_fail())return -1;',adapter,count=1)
  assert n==1,name
 adapter=adapter.replace('return nwfn_io.reg_write(&nwfn_native,p,v);','int r=nwfn_io.reg_write(&nwfn_native,p,v);if(!r&&nct_test_after(p))return -1;return r;')
 (Path(d)/'wifi_capability_bind.inc').write_text(adapter)
 for name in ('session','faults'):
  out=Path(d)/name;subprocess.run(flags[:1]+(['-I'+d] if name=='faults' else [])+flags[1:]+[str(P/('tests/test-'+name+'.c'))]+list(map(str,files))+['-o',str(out)],check=True)
  r=subprocess.run([str(out)],capture_output=True,text=True);logs.append(r.stdout+r.stderr);(P/'host-test-output.log').write_text('\n'.join(logs))
  if r.returncode:print(r.stdout[-4000:]);print(r.stderr);r.check_returncode()
  print(r.stdout.splitlines()[-1])
