#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parent;R=P
while not (R/'shim/nord2-xnu').is_dir():R=R.parent
S=P/'proposed/shim/nord2-xnu';C=R/'shim/nord2-xnu'
with tempfile.TemporaryDirectory() as d:
 for name in ['cap','basic']:
  files=[C/(x+'.c') for x in ['wifi_capability','wifi_main_command','dma_pool_contract']]
  if name=='basic':files += [C/(x+'.c') for x in ['wifi_basic_resources','wifi_basic_metadata','wifi_basic_primitives']]
  e=Path(d)/name;subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(S),'-I'+str(C),str(P/('test-'+name+'.c'))]+list(map(str,files))+['-o',str(e)],check=True);subprocess.run([str(e)],check=True)
print('PASS actual CAP parser65536 inner values; actual BASIC lease/drain/rearm65536 inner values,24 malformed refusals/noaftererrorcallbacks/noextraIO')
