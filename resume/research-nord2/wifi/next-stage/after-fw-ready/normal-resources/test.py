#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,hashlib,os
p=Path(__file__).resolve().parent
repo=p.parents[5]
board=repo/'research/nord2/wifi/calibration/WIFI'
assert hashlib.sha256(board.read_bytes()).hexdigest()=='0000000000000000000000000000000000000000000000000000000000000000'
with tempfile.TemporaryDirectory(prefix='normal-resources-') as d:
 for name,sources,args in [('resources',[p/'resources.c',p/'test.c'],[]),('address',[p.parent/'normal-init/normal_init.c',p/'test-address.c'],[str(board)])]:
  out=Path(d)/name
  subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*[str(x) for x in sources],'-o',str(out)],check=True)
  subprocess.run([str(out),*args],check=True,env={**os.environ,'ASAN_OPTIONS':'halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
