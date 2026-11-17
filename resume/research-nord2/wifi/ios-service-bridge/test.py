#!/usr/bin/env python3
from pathlib import Path
import tempfile,subprocess,os
p=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='ios-wifi-contract-') as t:
 out=t+'/test';subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'control_contract.c'),str(p/'test-control.c'),'-o',out],check=True)
 subprocess.run([out],check=True,env={**os.environ,'ASAN_OPTIONS':'halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
