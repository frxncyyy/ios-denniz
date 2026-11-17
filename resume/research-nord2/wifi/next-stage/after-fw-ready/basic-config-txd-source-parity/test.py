#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,os
P=Path(__file__).resolve().parent
R=P.parents[5]
S=P/'proposed/shim/nord2-xnu';C=R/'shim/nord2-xnu'
flags=[os.getenv('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-I'+str(S),'-I'+str(C)]
files=[S/'wifi_basic_config.c']+[C/(x+'.c') for x in ('wifi_basic_resources','wifi_basic_metadata','wifi_basic_primitives')]
files += [C/(x+'.c') for x in ('wifi_capability','wifi_capability_dispatcher','wifi_main_initializer','wifi_main_dmashdl','wifi_main_init_plan','wifi_main_transport','wifi_main_layout','wifi_dma_preflight','wifi_main_command','dma_pool_contract')]
with tempfile.TemporaryDirectory(prefix='nbc-private-') as d:
 logs=[]
 for name in ('current-owner','faults','opaque-writeback'):
  out=Path(d)/name
  subprocess.run(flags+[str(P/('tests/test-'+name+'.c'))]+list(map(str,files))+['-o',str(out)],check=True)
  result=subprocess.run([str(out)],capture_output=True,text=True)
  logs.append(result.stdout+result.stderr)
  (P/'host-test-output.log').write_text('\n'.join(logs))
  if result.returncode:print(result.stdout[-6000:]);print(result.stderr);result.check_returncode()
  print(result.stdout.splitlines()[-1])
